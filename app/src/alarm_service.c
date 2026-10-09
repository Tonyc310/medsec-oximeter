#include "alarm_service.h"

#include "alarm.h"
#include "medsec_bt.h"

#include <errno.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

LOG_MODULE_REGISTER(alarm_service, LOG_LEVEL_INF);

static const struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(MEDSEC_ALARM_SERVICE_VAL);
static const struct bt_uuid_128 limits_uuid = BT_UUID_INIT_128(MEDSEC_ALARM_LIMITS_VAL);
static const struct bt_uuid_128 state_uuid = BT_UUID_INIT_128(MEDSEC_ALARM_STATE_VAL);

/* LED1 on the DK. */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* The limits are written from the Bluetooth thread and read from the sensor thread. */
static struct k_spinlock lock;
static alarm_limits_t limits;
static uint8_t state; /* ALARM_* bits; written only from the sensor thread */

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
    if (!alarm_limits_decode(buf, len, &requested)) {
        LOG_WRN("alarm limits rejected (%u bytes)", len);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    const k_spinlock_key_t key = k_spin_lock(&lock);

    limits = requested;
    k_spin_unlock(&lock, key);
    LOG_INF("alarm limits set: SpO2 below %u%%, pulse below %u or above %u bpm",
            requested.spo2_low_percent, requested.pulse_low_bpm, requested.pulse_high_bpm);
    return (ssize_t)len;
}

static ssize_t read_state(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                          uint16_t len, uint16_t offset)
{
    const uint8_t value = state;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof value);
}

/* Open in the baseline: any device in range can change the limits (threats T-01 to T-03). */
BT_GATT_SERVICE_DEFINE(
    alarm_service, BT_GATT_PRIMARY_SERVICE(&service_uuid),
    BT_GATT_CHARACTERISTIC(&limits_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_limits, write_limits, NULL),
    BT_GATT_CHARACTERISTIC(&state_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, read_state, NULL, NULL),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

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
    limits = alarm_default_limits();
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

    (void)gpio_pin_set_dt(&led, (alarms != 0u) ? 1 : 0);
    if (alarms == state) {
        return;
    }
    state = alarms;
    log_alarms(alarms);

    const int err = bt_gatt_notify(NULL, ALARM_STATE, &state, sizeof state);

    /* -ENOTCONN only means no central is listening. */
    if ((err != 0) && (err != -ENOTCONN)) {
        LOG_WRN("alarm notification failed: %d", err);
    }
}
