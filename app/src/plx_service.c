#include "plx_service.h"

#include "admission.h"
#include "medsec_bt.h"
#include "plx.h"

#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(plx_service, LOG_LEVEL_INF);

/* PLX Features: a 16-bit Supported Features field, none of the optional ones. */
static const uint8_t features[] = {0x00, 0x00};

static ssize_t read_features(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                             uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, features, sizeof features);
}

/* Only a central paired at level 4, authenticated LE Secure Connections, can subscribe, so only
 * the gateway hears the patient's readings (threats T-07, T-08). */
BT_GATT_SERVICE_DEFINE(pulse_oximeter_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_POS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_GATT_PLX_CM, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CCC(NULL, BT_GATT_PERM_READ | MEDSEC_PERM_WRITE_L4),
                       BT_GATT_CHARACTERISTIC(BT_UUID_GATT_PLX_F, BT_GATT_CHRC_READ,
                                              MEDSEC_PERM_READ_L4, read_features, NULL, NULL));

/* attrs[0] declares the service; attrs[1] is the Continuous Measurement characteristic. */
#define CONTINUOUS_MEASUREMENT (&pulse_oximeter_service.attrs[1])

int plx_service_start(void)
{
    const int err = bt_enable(NULL);

    if (err != 0) {
        return err;
    }
    /* The bond with the gateway, kept in flash through a reset. Without it the gateway pairs
     * again, so monitoring goes on either way. */
    const int load_err = settings_load_subtree("bt");

    if (load_err != 0) {
        LOG_ERR("could not load the bond: %d", load_err);
    }
    admission_start();
    return 0;
}

void plx_service_send(const oximetry_reading_t *reading)
{
    uint8_t value[PLX_CONTINUOUS_SIZE];

    plx_encode_continuous(reading, value);
    const int err = bt_gatt_notify(NULL, CONTINUOUS_MEASUREMENT, value, sizeof value);

    /* -ENOTCONN only means no central is listening yet. */
    if ((err != 0) && (err != -ENOTCONN)) {
        LOG_WRN("notification failed: %d", err);
    }
}
