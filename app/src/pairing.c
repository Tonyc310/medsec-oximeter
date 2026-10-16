#include "pairing.h"

#include "device_identity.h"
#include "medsec_bt.h"
#include "pairing_seal.h"

#include <mbedtls/platform_util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pairing, LOG_LEVEL_INF);

BUILD_ASSERT(sizeof(struct bt_le_oob_sc_data) == PAIRING_DATA_SIZE,
             "the oximeter seals exactly its LE Secure Connections out-of-band data");

static const struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(MEDSEC_PAIRING_SERVICE_VAL);
static const struct bt_uuid_128 sealed_uuid = BT_UUID_INIT_128(MEDSEC_SEALED_PAIRING_DATA_VAL);

/* Used on the Bluetooth receive thread only, which runs the GATT and pairing callbacks. */
static struct bt_le_oob oob;
static bool oob_ready;
static uint8_t sealed[PAIRING_SEALED_SIZE];

/* New out-of-band data for each read from the start, sealed so only the backend can open it. A
 * central reads it in pieces when its link is short. */
static ssize_t read_sealed(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                           uint16_t len, uint16_t offset)
{
    if (offset == 0u) {
        uint8_t id[DEVICE_ID_SIZE];

        oob_ready = (bt_le_oob_get_local(BT_ID_DEFAULT, &oob) == 0) &&
                    (device_identity_id(id) == 0) &&
                    pairing_seal(id, (const uint8_t *)&oob.le_sc_data, sealed);
    }
    if (!oob_ready) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset, sealed, sizeof sealed);
}

/* With the device ID, the only thing a central can read before it pairs. */
BT_GATT_SERVICE_DEFINE(pairing_service, BT_GATT_PRIMARY_SERVICE(&service_uuid),
                       BT_GATT_CHARACTERISTIC(&sealed_uuid.uuid, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ, read_sealed, NULL, NULL));

/* Pairing then checks that the central knew this data's random value, so only a central that
 * could open the seal can pair. */
static void on_oob_data_request(struct bt_conn *conn, struct bt_conn_oob_info *info)
{
    ARG_UNUSED(info);
    if (!oob_ready || (bt_le_oob_set_sc_data(conn, &oob.le_sc_data, NULL) != 0)) {
        (void)bt_conn_auth_cancel(conn);
    }
}

/* Each pairing gets data of its own. */
static void forget_oob(void)
{
    mbedtls_platform_zeroize(&oob, sizeof oob);
    oob_ready = false;
}

static void on_pairing_complete(struct bt_conn *conn, bool bonded)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(bonded);
    forget_oob();
    LOG_INF("paired");
}

static void on_pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    ARG_UNUSED(conn);
    forget_oob();
    LOG_WRN("pairing failed (reason %d)", reason);
}

static struct bt_conn_auth_cb auth_callbacks = {
    .oob_data_request = on_oob_data_request,
};

static struct bt_conn_auth_info_cb auth_info_callbacks = {
    .pairing_complete = on_pairing_complete,
    .pairing_failed = on_pairing_failed,
};

int pairing_init(void)
{
    const int err = bt_conn_auth_cb_register(&auth_callbacks);

    return (err != 0) ? err : bt_conn_auth_info_cb_register(&auth_info_callbacks);
}
