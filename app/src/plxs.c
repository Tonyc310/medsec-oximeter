#include "plxs.h"

#include "plx.h"

#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(plxs, LOG_LEVEL_INF);

/* PLX Features: a 16-bit Supported Features field, none of the optional ones. */
static const uint8_t features[] = {0x00, 0x00};

static ssize_t read_features(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                             uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, features, sizeof features);
}

/* The baseline is deliberately open: no pairing, no encryption, so anyone in range can connect
 * and listen. The threat model starts from here; Phase 4 adds the controls. */
BT_GATT_SERVICE_DEFINE(pulse_oximeter_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_POS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_GATT_PLX_CM, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CHARACTERISTIC(BT_UUID_GATT_PLX_F, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ, read_features, NULL, NULL));

/* attrs[0] declares the service; attrs[1] is the Continuous Measurement characteristic. */
#define CONTINUOUS_MEASUREMENT (&pulse_oximeter_service.attrs[1])

static const struct bt_data advertising[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_POS_VAL)),
};

static const struct bt_data scan_response[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1u),
};

static void advertise(void)
{
    const int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertising, ARRAY_SIZE(advertising),
                                    scan_response, ARRAY_SIZE(scan_response));

    if (err != 0) {
        LOG_ERR("advertising failed: %d", err);
    } else {
        LOG_INF("advertising");
    }
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    ARG_UNUSED(conn);
    if (err == 0u) {
        LOG_INF("central connected");
    }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    LOG_INF("central disconnected (reason 0x%02x)", reason);
}

/* Connectable advertising stops when a central connects; start it again once that connection's
 * object is free, so the next central can find the device. */
static void on_recycled(void)
{
    advertise();
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .recycled = on_recycled,
};

int plxs_start(void)
{
    const int err = bt_enable(NULL);

    if (err != 0) {
        return err;
    }
    advertise();
    return 0;
}

void plxs_send(const oximetry_reading_t *reading)
{
    uint8_t value[PLX_CONTINUOUS_SIZE];

    plx_encode_continuous(reading, value);
    const int err = bt_gatt_notify(NULL, CONTINUOUS_MEASUREMENT, value, sizeof value);

    /* -ENOTCONN only means no central is listening yet. */
    if ((err != 0) && (err != -ENOTCONN)) {
        LOG_WRN("notification failed: %d", err);
    }
}
