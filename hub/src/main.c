#include "alarm.h"
#include "device_keys.h"
#include "limits_auth.h"
#include "limits_signer.h"
#include "log_verifier.h"
#include "masters.h"
#include "medsec_bt.h"
#include "pairing.h"
#include "plx.h"
#include "security_log.h"

#include <errno.h>
#include <mbedtls/platform_util.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(hub, LOG_LEVEL_INF);

/* A stand-in for the bedside gateway: it finds a pulse oximeter, logs its readings and alarms,
 * and sets its alarm limits from the shell, signing each change as the backend will. */

/* Discovery reads its UUID after the call that starts it has returned, and the BT_UUID_* macros
 * make temporaries, so these live at file scope. */
static const struct bt_uuid_16 continuous_measurement = BT_UUID_INIT_16(BT_UUID_GATT_PLX_CM_VAL);
static const struct bt_uuid_128 alarm_limits = BT_UUID_INIT_128(MEDSEC_ALARM_LIMITS_VAL);
static const struct bt_uuid_128 alarm_state = BT_UUID_INIT_128(MEDSEC_ALARM_STATE_VAL);
static const struct bt_uuid_128 alarm_challenge = BT_UUID_INIT_128(MEDSEC_ALARM_CHALLENGE_VAL);
static const struct bt_uuid_128 security_log = BT_UUID_INIT_128(MEDSEC_SECURITY_LOG_VAL);
static const struct bt_uuid_128 device_id = BT_UUID_INIT_128(MEDSEC_DEVICE_ID_VAL);
static const struct bt_uuid_128 sealed_pairing_data =
    BT_UUID_INIT_128(MEDSEC_SEALED_PAIRING_DATA_VAL);

static struct bt_conn *oximeter;
static struct bt_gatt_discover_params discover_params;
static uint16_t measurement_handle;
static uint16_t limits_handle;
static uint16_t state_handle;
static uint16_t challenge_handle;
static uint16_t log_handle;
static uint16_t device_id_handle;
static uint16_t sealed_handle;
static struct bt_gatt_read_params device_id_read;

/* Each subscription finds its own CCC descriptor, so each needs its own discovery parameters. */
static struct bt_gatt_subscribe_params measurement_subscription;
static struct bt_gatt_discover_params measurement_ccc_discovery;
static struct bt_gatt_subscribe_params alarm_subscription;
static struct bt_gatt_discover_params alarm_ccc_discovery;

/* A limits change takes two steps: read the oximeter's challenge, then write the limits signed
 * over it. The parameters and data must stay valid until the oximeter answers each step. */
static alarm_limits_t requested;
static struct bt_gatt_read_params challenge_read;
static struct bt_gatt_write_params write_params;
static uint8_t command[LIMITS_AUTH_COMMAND_SIZE];

static void start_scan(void);

/* Everything here came over the air, so it goes through the strict decoder before use. */
static uint8_t on_measurement(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                              const void *data, uint16_t length)
{
    oximetry_reading_t reading;

    ARG_UNUSED(conn);
    if (data == NULL) {
        params->value_handle = 0u;
        return BT_GATT_ITER_STOP;
    }
    if (!plx_decode_continuous(data, length, &reading)) {
        LOG_WRN("malformed measurement (%u bytes) ignored", length);
    } else if (reading.valid) {
        LOG_INF("SpO2 %u%%, pulse %u bpm", reading.spo2_percent, reading.pulse_bpm);
    } else {
        LOG_INF("no reading");
    }
    return BT_GATT_ITER_CONTINUE;
}

static uint8_t on_alarm(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                        const void *data, uint16_t length)
{
    ARG_UNUSED(conn);
    if (data == NULL) {
        params->value_handle = 0u;
        return BT_GATT_ITER_STOP;
    }
    if (length != 1u) {
        LOG_WRN("malformed alarm state (%u bytes) ignored", length);
        return BT_GATT_ITER_CONTINUE;
    }

    const uint8_t alarms = *(const uint8_t *)data;

    if (alarms == 0u) {
        LOG_INF("oximeter alarms cleared");
    }
    if ((alarms & ALARM_SPO2_LOW) != 0u) {
        LOG_WRN("ALARM from oximeter: SpO2 low");
    }
    if ((alarms & ALARM_PULSE_LOW) != 0u) {
        LOG_WRN("ALARM from oximeter: pulse low");
    }
    if ((alarms & ALARM_PULSE_HIGH) != 0u) {
        LOG_WRN("ALARM from oximeter: pulse high");
    }
    if ((alarms & ALARM_NO_READING) != 0u) {
        LOG_WRN("ALARM from oximeter: no pulse found");
    }
    return BT_GATT_ITER_CONTINUE;
}

static void subscribe(struct bt_conn *conn, struct bt_gatt_subscribe_params *subscription,
                      struct bt_gatt_discover_params *ccc_discovery, uint16_t value_handle,
                      bt_gatt_notify_func_t notify)
{
    subscription->value_handle = value_handle;
    subscription->ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
    subscription->end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    subscription->disc_params = ccc_discovery;
    subscription->value = BT_GATT_CCC_NOTIFY;
    subscription->notify = notify;

    const int err = bt_gatt_subscribe(conn, subscription);

    if ((err != 0) && (err != -EALREADY)) {
        LOG_ERR("subscribing failed: %d", err);
    }
}

/* Standing in for the backend, the hub derives the keys the factory gave this oximeter: one to sign
 * its limit changes, one to check its log, one to open its pairing data. Only that device's keys
 * are loaded; the masters stay with the hub. */
static uint8_t on_device_id(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
                            const void *data, uint16_t length)
{
    uint8_t key[DEVICE_KEY_SIZE];

    ARG_UNUSED(conn);
    ARG_UNUSED(params);
    if ((err != 0u) || (data == NULL) || (length != DEVICE_ID_SIZE)) {
        if (err != 0u) {
            LOG_WRN("couldn't read the oximeter's ID (ATT error 0x%02x)", err);
        }
        return BT_GATT_ITER_STOP;
    }

    const uint8_t *id = data;
    bool ready =
        device_keys_derive(signing_master, sizeof signing_master, DEVICE_KEY_LIMITS, id, key) &&
        limits_signer_init(key, sizeof key);

    ready = ready &&
            device_keys_derive(signing_master, sizeof signing_master, DEVICE_KEY_LOG, id, key) &&
            log_verifier_init(key, sizeof key);

    if (ready) {
        LOG_INF("signing for oximeter %02x%02x%02x%02x%02x%02x%02x%02x", id[0], id[1], id[2], id[3],
                id[4], id[5], id[6], id[7]);
    } else {
        LOG_ERR("couldn't derive the oximeter's keys");
    }
    if (device_keys_derive(pairing_master, sizeof pairing_master, DEVICE_KEY_PAIRING, id, key)) {
        pairing_start(conn, sealed_handle, id, key);
    } else {
        LOG_ERR("couldn't derive the oximeter's pairing key");
    }
    mbedtls_platform_zeroize(key, sizeof key);
    return BT_GATT_ITER_STOP;
}

static void read_device_id(struct bt_conn *conn)
{
    device_id_read.func = on_device_id;
    device_id_read.handle_count = 1u;
    device_id_read.single.handle = device_id_handle;
    device_id_read.single.offset = 0u;

    const int err = bt_gatt_read(conn, &device_id_read);

    if (err != 0) {
        LOG_ERR("reading the oximeter's ID failed: %d", err);
    }
}

/* One pass over every characteristic, noting the seven the hub uses; then learn the oximeter's ID,
 * to pair with it and sign for it. */
static uint8_t on_characteristic(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params)
{
    ARG_UNUSED(params);
    if (attr == NULL) {
        if ((measurement_handle == 0u) || (limits_handle == 0u) || (state_handle == 0u) ||
            (challenge_handle == 0u) || (log_handle == 0u) || (device_id_handle == 0u) ||
            (sealed_handle == 0u)) {
            LOG_WRN("not a pulse oximeter this hub knows");
            return BT_GATT_ITER_STOP;
        }
        read_device_id(conn);
        return BT_GATT_ITER_STOP;
    }

    const struct bt_gatt_chrc *characteristic = attr->user_data;

    if (bt_uuid_cmp(characteristic->uuid, &continuous_measurement.uuid) == 0) {
        measurement_handle = characteristic->value_handle;
    } else if (bt_uuid_cmp(characteristic->uuid, &alarm_limits.uuid) == 0) {
        limits_handle = characteristic->value_handle;
    } else if (bt_uuid_cmp(characteristic->uuid, &alarm_state.uuid) == 0) {
        state_handle = characteristic->value_handle;
    } else if (bt_uuid_cmp(characteristic->uuid, &alarm_challenge.uuid) == 0) {
        challenge_handle = characteristic->value_handle;
    } else if (bt_uuid_cmp(characteristic->uuid, &security_log.uuid) == 0) {
        log_handle = characteristic->value_handle;
    } else if (bt_uuid_cmp(characteristic->uuid, &device_id.uuid) == 0) {
        device_id_handle = characteristic->value_handle;
    } else if (bt_uuid_cmp(characteristic->uuid, &sealed_pairing_data.uuid) == 0) {
        sealed_handle = characteristic->value_handle;
    }
    return BT_GATT_ITER_CONTINUE;
}

static void on_written(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(params);
    if (err != 0u) {
        LOG_WRN("oximeter rejected the alarm limits (ATT error 0x%02x)", err);
    } else {
        LOG_INF("oximeter accepted the alarm limits");
    }
}

static uint8_t on_challenge(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
                            const void *data, uint16_t length)
{
    ARG_UNUSED(params);
    /* A failed read reports its error with no data; one that went through ends with no data. */
    if ((data == NULL) && (err == 0u)) {
        return BT_GATT_ITER_STOP;
    }
    if ((err != 0u) || (length != LIMITS_AUTH_CHALLENGE_SIZE)) {
        LOG_WRN("couldn't read the oximeter's challenge (ATT error 0x%02x, %u bytes)", err, length);
        return BT_GATT_ITER_STOP;
    }
    if (!limits_signer_sign(data, &requested, command)) {
        LOG_ERR("signing the alarm limits failed");
        return BT_GATT_ITER_STOP;
    }

    write_params.handle = limits_handle;
    write_params.offset = 0u;
    write_params.data = command;
    write_params.length = sizeof command;
    write_params.func = on_written;

    const int write_err = bt_gatt_write(conn, &write_params);

    if (write_err != 0) {
        LOG_ERR("sending the alarm limits failed: %d", write_err);
    }
    return BT_GATT_ITER_STOP;
}

static int parse_limit(const struct shell *sh, const char *text, unsigned long *value)
{
    int err = 0;

    *value = shell_strtoul(text, 10, &err);
    if ((err != 0) || (*value > UINT16_MAX)) {
        shell_error(sh, "%s isn't a number", text);
        return -EINVAL;
    }
    return 0;
}

static int cmd_limits(const struct shell *sh, size_t argc, char **argv)
{
    unsigned long spo2_low = 0u;
    unsigned long pulse_low = 0u;
    unsigned long pulse_high = 0u;

    ARG_UNUSED(argc);
    if ((parse_limit(sh, argv[1], &spo2_low) != 0) || (parse_limit(sh, argv[2], &pulse_low) != 0) ||
        (parse_limit(sh, argv[3], &pulse_high) != 0) || (spo2_low > UINT8_MAX)) {
        return -EINVAL;
    }

    const alarm_limits_t entered = {
        .spo2_low_percent = (uint8_t)spo2_low,
        .pulse_low_bpm = (uint16_t)pulse_low,
        .pulse_high_bpm = (uint16_t)pulse_high,
    };
    uint8_t encoded[ALARM_LIMITS_SIZE];
    alarm_limits_t checked;

    alarm_limits_encode(&entered, encoded);
    /* The oximeter checks them too; checking here gives a clearer message. */
    if (!alarm_limits_decode(encoded, sizeof encoded, &checked)) {
        shell_error(sh, "a monitor wouldn't accept those limits");
        return -EINVAL;
    }
    if ((oximeter == NULL) || (limits_handle == 0u)) {
        shell_error(sh, "no oximeter connected");
        return -ENOTCONN;
    }

    requested = checked;
    challenge_read.func = on_challenge;
    challenge_read.handle_count = 1u;
    challenge_read.single.handle = challenge_handle;
    challenge_read.single.offset = 0u;

    const int err = bt_gatt_read(oximeter, &challenge_read);

    if (err != 0) {
        shell_error(sh, "reading the challenge failed: %d", err);
        return err;
    }
    return 0;
}

/* The log arrives in pieces, as long as the link allows; it's printed once it's all here. */
static struct bt_gatt_read_params log_read;
static uint8_t log_entries[SECURITY_LOG_CAPACITY * SECURITY_LOG_ENTRY_SIZE];
static size_t log_length;

static const char *refusal(uint8_t reason)
{
    switch (reason) {
    case LIMITS_AUTH_WRONG_LENGTH:
        return "wrong length";
    case LIMITS_AUTH_NO_CHALLENGE:
        return "no fresh challenge";
    case LIMITS_AUTH_INVALID_LIMITS:
        return "limits a monitor wouldn't accept";
    case LIMITS_AUTH_BAD_TAG:
        return "tag doesn't match";
    default:
        return "unknown reason";
    }
}

static void describe(const security_record_t *record, char *text, size_t size)
{
    const uint8_t *detail = record->detail;
    alarm_limits_t limits;

    switch (record->event) {
    case SECURITY_EVENT_START:
        (void)snprintf(text, size, "started (reset cause 0x%08x)",
                       (unsigned int)sys_get_le32(detail));
        break;
    case SECURITY_EVENT_CONNECTED:
        (void)snprintf(text, size, "connected: %02X:%02X:%02X:%02X:%02X:%02X (%s)", detail[6],
                       detail[5], detail[4], detail[3], detail[2], detail[1],
                       (detail[0] == BT_ADDR_LE_PUBLIC) ? "public" : "random");
        break;
    case SECURITY_EVENT_DISCONNECTED:
        (void)snprintf(text, size, "disconnected (reason 0x%02x)", detail[0]);
        break;
    case SECURITY_EVENT_LIMITS_ACCEPTED:
        if (alarm_limits_decode(detail, ALARM_LIMITS_SIZE, &limits)) {
            (void)snprintf(text, size,
                           "limits accepted: SpO2 below %u%%, pulse below %u or above %u bpm",
                           limits.spo2_low_percent, limits.pulse_low_bpm, limits.pulse_high_bpm);
        } else {
            (void)snprintf(text, size, "limits accepted: malformed record");
        }
        break;
    case SECURITY_EVENT_LIMITS_REFUSED:
        (void)snprintf(text, size, "limits refused: %s", refusal(detail[0]));
        break;
    case SECURITY_EVENT_LIMITS_UNSAVED:
        (void)snprintf(text, size, "limits refused: couldn't save them");
        break;
    case SECURITY_EVENT_SENSOR_STOPPED:
        (void)snprintf(text, size, "sensor stopped sending samples");
        break;
    case SECURITY_EVENT_SENSOR_RESUMED:
        (void)snprintf(text, size, "sensor sending samples again");
        break;
    case SECURITY_EVENT_NO_DEVICE_KEY:
        (void)snprintf(text, size, "no device key provisioned");
        break;
    default:
        (void)snprintf(text, size, "unknown event %u", record->event);
        break;
    }
}

/* Every record is shown, verified or not: one that fails is what the backend most needs to see. */
static void print_log(void)
{
    unsigned int count = 0u;
    unsigned int verified = 0u;

    for (size_t at = 0u; (at + SECURITY_LOG_ENTRY_SIZE) <= log_length;
         at += SECURITY_LOG_ENTRY_SIZE) {
        security_record_t record;
        char text[80];

        count++;
        if (!security_log_decode(&log_entries[at], SECURITY_LOG_RECORD_SIZE, &record)) {
            LOG_WRN("malformed security record ignored");
            continue;
        }

        const bool genuine = log_verifier_check(&log_entries[at]);

        verified += genuine ? 1u : 0u;
        describe(&record, text, sizeof text);
        LOG_INF("#%u boot %u +%u.%03u s: %s (%s)", (unsigned int)record.sequence, record.boot,
                (unsigned int)(record.uptime_ms / 1000u), (unsigned int)(record.uptime_ms % 1000u),
                text, genuine ? "MAC ok" : "MAC doesn't match");
    }
    LOG_INF("%u security records, %u verified", count, verified);
}

static uint8_t on_log(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
                      const void *data, uint16_t length)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(params);
    if (err != 0u) {
        LOG_WRN("reading the security log failed (ATT error 0x%02x)", err);
        return BT_GATT_ITER_STOP;
    }
    if (data == NULL) {
        print_log();
        return BT_GATT_ITER_STOP;
    }
    if ((log_length + length) > sizeof log_entries) {
        LOG_WRN("security log longer than expected; the rest is ignored");
        return BT_GATT_ITER_STOP;
    }
    (void)memcpy(&log_entries[log_length], data, length);
    log_length += length;
    return BT_GATT_ITER_CONTINUE;
}

static int cmd_log(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if ((oximeter == NULL) || (log_handle == 0u)) {
        shell_error(sh, "no oximeter connected");
        return -ENOTCONN;
    }
    log_length = 0u;
    log_read.func = on_log;
    log_read.handle_count = 1u;
    log_read.single.handle = log_handle;
    log_read.single.offset = 0u;

    const int err = bt_gatt_read(oximeter, &log_read);

    if (err != 0) {
        shell_error(sh, "reading the security log failed: %d", err);
    }
    return err;
}

SHELL_CMD_REGISTER(log, NULL, "Print the oximeter's security event log, oldest first", cmd_log);

SHELL_CMD_ARG_REGISTER(limits, NULL,
                       "Set the oximeter's alarm limits: limits <SpO2 low %> <pulse low bpm> "
                       "<pulse high bpm>",
                       cmd_limits, 4, 0);

static bool lists_pulse_oximeter_service(struct bt_data *data, void *found)
{
    if ((data->type != BT_DATA_UUID16_ALL) && (data->type != BT_DATA_UUID16_SOME)) {
        return true;
    }
    for (uint8_t i = 0u; (i + 1u) < data->data_len; i += 2u) {
        if (sys_get_le16(&data->data[i]) == BT_UUID_POS_VAL) {
            *(bool *)found = true;
            return false;
        }
    }
    return true;
}

static void on_device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                            struct net_buf_simple *ad)
{
    bool found = false;

    ARG_UNUSED(rssi);
    if ((oximeter != NULL) || (type != BT_GAP_ADV_TYPE_ADV_IND)) {
        return;
    }
    bt_data_parse(ad, lists_pulse_oximeter_service, &found);
    if (!found || (bt_le_scan_stop() != 0)) {
        return;
    }

    const int err =
        bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &oximeter);

    if (err != 0) {
        LOG_ERR("connecting failed: %d", err);
        start_scan();
    }
}

static void start_scan(void)
{
    const int err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, on_device_found);

    if (err != 0) {
        LOG_ERR("scanning failed: %d", err);
    } else {
        LOG_INF("scanning for a pulse oximeter");
    }
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err != 0u) {
        LOG_WRN("connection failed (0x%02x)", err);
        bt_conn_unref(oximeter);
        oximeter = NULL;
        start_scan();
        return;
    }
    LOG_INF("connected to a pulse oximeter");

    measurement_handle = 0u;
    limits_handle = 0u;
    state_handle = 0u;
    discover_params.uuid = NULL;
    discover_params.func = on_characteristic;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    const int discover_err = bt_gatt_discover(conn, &discover_params);

    if (discover_err != 0) {
        LOG_ERR("discovery failed: %d", discover_err);
    }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    LOG_INF("disconnected (reason 0x%02x)", reason);
    bt_conn_unref(oximeter);
    oximeter = NULL;
    start_scan();
}

/* The oximeter sends readings and alarms only once paired, at level 4. */
static void on_security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    if ((err != BT_SECURITY_ERR_SUCCESS) || (level < BT_SECURITY_L4)) {
        LOG_WRN("not paired with the oximeter (security level %d, error %d)", level, err);
        return;
    }
    LOG_INF("link to the oximeter at security level %d", level);
    subscribe(conn, &measurement_subscription, &measurement_ccc_discovery, measurement_handle,
              on_measurement);
    subscribe(conn, &alarm_subscription, &alarm_ccc_discovery, state_handle, on_alarm);
    LOG_INF("subscribed to readings and alarms");
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .security_changed = on_security_changed,
};

int main(void)
{
    LOG_INF("hub started");
    if (pairing_init() != 0) {
        LOG_ERR("could not set up pairing");
        return 0;
    }
    if (bt_enable(NULL) != 0) {
        LOG_ERR("could not start Bluetooth");
        return 0;
    }
    start_scan();
    return 0;
}
