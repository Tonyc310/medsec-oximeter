#include "plx.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(hub, LOG_LEVEL_INF);

/* A stand-in for the bedside gateway: it finds a pulse oximeter, subscribes to its readings and
 * logs them. Like the oximeter's baseline, it uses no pairing or encryption yet. */

/* Discovery keeps a pointer to the UUID it searches for until it finishes, after the call that
 * started it has returned. The BT_UUID_* macros make temporaries, so these live at file scope. */
static const struct bt_uuid_16 pulse_oximeter_service = BT_UUID_INIT_16(BT_UUID_POS_VAL);
static const struct bt_uuid_16 continuous_measurement = BT_UUID_INIT_16(BT_UUID_GATT_PLX_CM_VAL);

static struct bt_conn *oximeter;
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_discover_params ccc_discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

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

static void subscribe(struct bt_conn *conn, const struct bt_gatt_attr *characteristic,
                      uint16_t end_handle)
{
    subscribe_params.value_handle = bt_gatt_attr_value_handle(characteristic);
    subscribe_params.ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
    subscribe_params.end_handle = end_handle;
    subscribe_params.disc_params = &ccc_discover_params;
    subscribe_params.value = BT_GATT_CCC_NOTIFY;
    subscribe_params.notify = on_measurement;

    const int err = bt_gatt_subscribe(conn, &subscribe_params);

    if ((err != 0) && (err != -EALREADY)) {
        LOG_ERR("subscribing failed: %d", err);
    } else {
        LOG_INF("subscribed to readings");
    }
}

/* First the service, then its Continuous Measurement characteristic, then subscribe. */
static uint8_t on_discovered(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params)
{
    if (attr == NULL) {
        LOG_WRN("no pulse oximeter measurements found");
        return BT_GATT_ITER_STOP;
    }
    if (params->type == BT_GATT_DISCOVER_PRIMARY) {
        params->uuid = &continuous_measurement.uuid;
        params->start_handle = attr->handle + 1u;
        params->type = BT_GATT_DISCOVER_CHARACTERISTIC;

        const int err = bt_gatt_discover(conn, params);

        if (err != 0) {
            LOG_ERR("discovery failed: %d", err);
        }
        return BT_GATT_ITER_STOP;
    }
    subscribe(conn, attr, params->end_handle);
    return BT_GATT_ITER_STOP;
}

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

    discover_params.uuid = &pulse_oximeter_service.uuid;
    discover_params.func = on_discovered;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_PRIMARY;

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
