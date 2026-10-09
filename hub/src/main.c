#include "alarm.h"
#include "medsec_bt.h"
#include "plx.h"

#include <errno.h>
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
 * and sets its alarm limits from the shell. Like the oximeter's baseline, it uses no pairing or
 * encryption yet. */

/* Discovery keeps a pointer to the UUID it compares against until it finishes, after the call
 * that started it has returned. The BT_UUID_* macros make temporaries, so these live at file
 * scope. */
static const struct bt_uuid_16 continuous_measurement = BT_UUID_INIT_16(BT_UUID_GATT_PLX_CM_VAL);
static const struct bt_uuid_128 alarm_limits = BT_UUID_INIT_128(MEDSEC_ALARM_LIMITS_VAL);
static const struct bt_uuid_128 alarm_state = BT_UUID_INIT_128(MEDSEC_ALARM_STATE_VAL);

static struct bt_conn *oximeter;
static struct bt_gatt_discover_params discover_params;
static uint16_t measurement_handle;
static uint16_t limits_handle;
static uint16_t state_handle;

/* Each subscription finds its own CCC descriptor, so each needs its own discovery parameters. */
static struct bt_gatt_subscribe_params measurement_subscription;
static struct bt_gatt_discover_params measurement_ccc_discovery;
static struct bt_gatt_subscribe_params alarm_subscription;
static struct bt_gatt_discover_params alarm_ccc_discovery;

/* A write's data must stay valid until the oximeter answers. */
static struct bt_gatt_write_params write_params;
static uint8_t limits_value[ALARM_LIMITS_SIZE];

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

/* One pass over every characteristic, noting the three the hub uses; then subscribe. */
static uint8_t on_characteristic(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params)
{
    ARG_UNUSED(params);
    if (attr == NULL) {
        if ((measurement_handle == 0u) || (limits_handle == 0u) || (state_handle == 0u)) {
            LOG_WRN("not a pulse oximeter this hub knows");
            return BT_GATT_ITER_STOP;
        }
        subscribe(conn, &measurement_subscription, &measurement_ccc_discovery, measurement_handle,
                  on_measurement);
        subscribe(conn, &alarm_subscription, &alarm_ccc_discovery, state_handle, on_alarm);
        LOG_INF("subscribed to readings and alarms");
        return BT_GATT_ITER_STOP;
    }

    const struct bt_gatt_chrc *characteristic = attr->user_data;

    if (bt_uuid_cmp(characteristic->uuid, &continuous_measurement.uuid) == 0) {
        measurement_handle = characteristic->value_handle;
    } else if (bt_uuid_cmp(characteristic->uuid, &alarm_limits.uuid) == 0) {
        limits_handle = characteristic->value_handle;
    } else if (bt_uuid_cmp(characteristic->uuid, &alarm_state.uuid) == 0) {
        state_handle = characteristic->value_handle;
    } else {
        /* Not one the hub uses. */
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

    const alarm_limits_t requested = {
        .spo2_low_percent = (uint8_t)spo2_low,
        .pulse_low_bpm = (uint16_t)pulse_low,
        .pulse_high_bpm = (uint16_t)pulse_high,
    };
    alarm_limits_t checked;

    alarm_limits_encode(&requested, limits_value);
    /* The oximeter checks them too; checking here gives a clearer message. */
    if (!alarm_limits_decode(limits_value, sizeof limits_value, &checked)) {
        shell_error(sh, "a monitor wouldn't accept those limits");
        return -EINVAL;
    }
    if ((oximeter == NULL) || (limits_handle == 0u)) {
        shell_error(sh, "no oximeter connected");
        return -ENOTCONN;
    }

    write_params.handle = limits_handle;
    write_params.offset = 0u;
    write_params.data = limits_value;
    write_params.length = sizeof limits_value;
    write_params.func = on_written;

    const int err = bt_gatt_write(oximeter, &write_params);

    if (err != 0) {
        shell_error(sh, "sending the limits failed: %d", err);
        return err;
    }
    return 0;
}

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

BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

int main(void)
{
    LOG_INF("hub started");
    if (bt_enable(NULL) != 0) {
        LOG_ERR("could not start Bluetooth");
        return 0;
    }
    start_scan();
    return 0;
}
