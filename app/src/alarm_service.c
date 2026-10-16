#include "alarm_service.h"

#include "alarm.h"
#include "device_identity.h"
#include "event_log.h"
#include "limits_auth.h"
#include "limits_crypto.h"
#include "limits_store.h"
#include "medsec_bt.h"

#include <errno.h>
#include <mbedtls/platform_util.h>
#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

LOG_MODULE_REGISTER(alarm_service, LOG_LEVEL_INF);

static const struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(MEDSEC_ALARM_SERVICE_VAL);
static const struct bt_uuid_128 limits_uuid = BT_UUID_INIT_128(MEDSEC_ALARM_LIMITS_VAL);
static const struct bt_uuid_128 state_uuid = BT_UUID_INIT_128(MEDSEC_ALARM_STATE_VAL);
static const struct bt_uuid_128 challenge_uuid = BT_UUID_INIT_128(MEDSEC_ALARM_CHALLENGE_VAL);
static const struct bt_uuid_128 log_uuid = BT_UUID_INIT_128(MEDSEC_SECURITY_LOG_VAL);
static const struct bt_uuid_128 device_id_uuid = BT_UUID_INIT_128(MEDSEC_DEVICE_ID_VAL);

/* LED1 on the DK. */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* The limits are written from the Bluetooth thread and read from the sensor thread. */
static struct k_spinlock lock;
static alarm_limits_t limits;
/* Readings come from the sensor's thread, and "no reading" from the supervisor's when the sensor
 * stops. */
static K_MUTEX_DEFINE(update_lock);
static uint8_t state; /* ALARM_* bits, under update_lock */

/* Used only on the Bluetooth receive thread (CONFIG_BT_RECV_WORKQ_BT), which runs the GATT and
 * connection callbacks, after alarm_service_init: no lock needed. */
static limits_auth_t auth;

static ssize_t read_limits(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                           uint16_t len, uint16_t offset)
{
    uint8_t value[ALARM_LIMITS_SIZE];
    const k_spinlock_key_t key = k_spin_lock(&lock);

    alarm_limits_encode(&limits, value);
    k_spin_unlock(&lock, key);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof value);
}

static ssize_t write_limits(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags)
{
    alarm_limits_t requested;

    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);
    if (offset != 0u) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    const limits_auth_result_t result = limits_auth_apply(&auth, buf, len, &requested);

    if (result != LIMITS_AUTH_ACCEPTED) {
        const uint8_t reason = (uint8_t)result;

        LOG_WRN("alarm limits change refused (reason %d)", (int)result);
        (void)event_log_record(SECURITY_EVENT_LIMITS_REFUSED, &reason, sizeof reason);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    uint8_t encoded[ALARM_LIMITS_SIZE];

    alarm_limits_encode(&requested, encoded);
    /* Saved before it applies: a change a reset would undo is refused, so the clinician knows. */
    const int err = limits_store_save(&requested);

    if (err != 0) {
        LOG_ERR("alarm limits change refused: saving it failed (%d)", err);
        (void)event_log_record(SECURITY_EVENT_LIMITS_UNSAVED, encoded, sizeof encoded);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    const k_spinlock_key_t key = k_spin_lock(&lock);

    limits = requested;
    k_spin_unlock(&lock, key);
    LOG_INF("alarm limits set: SpO2 below %u%%, pulse below %u or above %u bpm",
            requested.spo2_low_percent, requested.pulse_low_bpm, requested.pulse_high_bpm);
    (void)event_log_record(SECURITY_EVENT_LIMITS_ACCEPTED, encoded, sizeof encoded);
    return (ssize_t)len;
}

static ssize_t read_state(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                          uint16_t len, uint16_t offset)
{
    const uint8_t value = state;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof value);
}

static ssize_t read_challenge(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, limits_auth_challenge(&auth),
                             LIMITS_AUTH_CHALLENGE_SIZE);
}

/* The whole log, oldest entry first; a central reads it in pieces, at increasing offsets. */
static ssize_t read_log(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                        uint16_t len, uint16_t offset)
{
    static uint8_t entries[SECURITY_LOG_CAPACITY * SECURITY_LOG_ENTRY_SIZE];
    const size_t length = event_log_read(entries, sizeof entries);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, entries, (uint16_t)length);
}

static ssize_t read_device_id(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset)
{
    uint8_t id[DEVICE_ID_SIZE];

    if (device_identity_id(id) != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset, id, sizeof id);
}

/* Only a central paired at level 4 reaches the limits, the alarms and the log; even then only a
 * holder of the limits key can change the limits, and only once per challenge (threats T-01 to
 * T-03). The device ID stays open: a central needs it to pair. */
BT_GATT_SERVICE_DEFINE(alarm_service, BT_GATT_PRIMARY_SERVICE(&service_uuid),
                       BT_GATT_CHARACTERISTIC(&limits_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                                              MEDSEC_PERM_READ_L4 | MEDSEC_PERM_WRITE_L4,
                                              read_limits, write_limits, NULL),
                       BT_GATT_CHARACTERISTIC(&state_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              MEDSEC_PERM_READ_L4, read_state, NULL, NULL),
                       BT_GATT_CCC(NULL, BT_GATT_PERM_READ | MEDSEC_PERM_WRITE_L4),
                       BT_GATT_CHARACTERISTIC(&challenge_uuid.uuid, BT_GATT_CHRC_READ,
                                              MEDSEC_PERM_READ_L4, read_challenge, NULL, NULL),
                       BT_GATT_CHARACTERISTIC(&log_uuid.uuid, BT_GATT_CHRC_READ,
                                              MEDSEC_PERM_READ_L4, read_log, NULL, NULL),
                       BT_GATT_CHARACTERISTIC(&device_id_uuid.uuid, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ, read_device_id, NULL, NULL));

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err != 0u) {
        return;
    }
    /* A command made on an earlier connection is never valid on this one. */
    limits_auth_new_challenge(&auth);

    const bt_addr_le_t *central = bt_conn_get_dst(conn);
    uint8_t detail[1u + sizeof central->a.val] = {central->type};

    (void)memcpy(&detail[1], central->a.val, sizeof central->a.val);
    (void)event_log_record(SECURITY_EVENT_CONNECTED, detail, sizeof detail);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    (void)event_log_record(SECURITY_EVENT_DISCONNECTED, &reason, sizeof reason);
}

BT_CONN_CB_DEFINE(alarm_connection_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

/* attrs[3] is the alarm state characteristic: after the service and the limits' two attributes. */
#define ALARM_STATE (&alarm_service.attrs[3])

static void log_alarms(uint8_t alarms)
{
    if (alarms == 0u) {
        LOG_INF("alarms cleared");
    }
    if ((alarms & ALARM_SPO2_LOW) != 0u) {
        LOG_WRN("ALARM: SpO2 low");
    }
    if ((alarms & ALARM_PULSE_LOW) != 0u) {
        LOG_WRN("ALARM: pulse low");
    }
    if ((alarms & ALARM_PULSE_HIGH) != 0u) {
        LOG_WRN("ALARM: pulse high");
    }
    if ((alarms & ALARM_NO_READING) != 0u) {
        LOG_WRN("ALARM: no pulse found, check the sensor");
    }
}

int alarm_service_init(void)
{
    /* Monitoring matters more than the stored limits: if they can't be read, the defaults hold. */
    const int err = limits_store_load(&limits);

    if (err != 0) {
        LOG_ERR("stored alarm limits unreadable (%d): using the defaults", err);
    }
    LOG_INF("alarm limits: SpO2 below %u%%, pulse below %u or above %u bpm",
            limits.spo2_low_percent, limits.pulse_low_bpm, limits.pulse_high_bpm);
    uint8_t key[DEVICE_KEY_SIZE];
    const bool provisioned = device_identity_key(DEVICE_KEY_LIMITS, key);

    /* Without a key, crypto still starts, for the challenges, but loads none: every change is
     * then refused, as no tag can match. */
    const bool loaded = limits_crypto_init(key, provisioned ? sizeof key : 0u);

    mbedtls_platform_zeroize(key, sizeof key);
    if (!provisioned) {
        LOG_ERR("no device key provisioned: the alarm limits can't be changed");
        (void)event_log_record(SECURITY_EVENT_NO_DEVICE_KEY, NULL, 0u);
    } else if (!loaded) {
        return -EIO;
    }
    limits_auth_init(&auth, &limits_crypto);
    if (!gpio_is_ready_dt(&led)) {
        return -ENODEV;
    }
    return gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

void alarm_service_update(const oximetry_reading_t *reading)
{
    const k_spinlock_key_t key = k_spin_lock(&lock);
    const alarm_limits_t current = limits;

    k_spin_unlock(&lock, key);

    const uint8_t alarms = alarm_check(&current, reading);

    (void)k_mutex_lock(&update_lock, K_FOREVER);
    (void)gpio_pin_set_dt(&led, (alarms != 0u) ? 1 : 0);
    if (alarms != state) {
        state = alarms;
        log_alarms(alarms);

        const int err = bt_gatt_notify(NULL, ALARM_STATE, &state, sizeof state);

        /* -ENOTCONN only means no central is listening. */
        if ((err != 0) && (err != -ENOTCONN)) {
            LOG_WRN("alarm notification failed: %d", err);
        }
    }
    k_mutex_unlock(&update_lock);
}
