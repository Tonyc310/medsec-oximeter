#include "pairing.h"

#include "pairing_seal.h"

#include <errno.h>
#include <mbedtls/platform_util.h>
#include <string.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(pairing, LOG_LEVEL_INF);

BUILD_ASSERT(sizeof(struct bt_le_oob_sc_data) == PAIRING_DATA_SIZE,
             "the oximeter seals exactly its LE Secure Connections out-of-band data");

/* Used on the Bluetooth receive thread only, from the read until pairing ends. The sealed data
 * may arrive in pieces. */
static uint8_t oximeter_id[DEVICE_ID_SIZE];
static uint8_t sealed[PAIRING_SEALED_SIZE];
static size_t sealed_length;
static struct bt_gatt_read_params sealed_read;
static struct bt_le_oob_sc_data oximeter_oob;

/* Set from the Bluetooth receive thread, read from the shell's: the oximeter that no longer knows
 * this hub's bond, until the hub pairs with it again. */
static bt_addr_le_t forgotten_by;
static bool forgotten;

static uint8_t on_sealed(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
                         const void *data, uint16_t length)
{
    ARG_UNUSED(params);
    if (err != 0u) {
        LOG_WRN("couldn't read the oximeter's pairing data (ATT error 0x%02x)", err);
        return BT_GATT_ITER_STOP;
    }
    if (data != NULL) {
        if (length > (sizeof sealed - sealed_length)) {
            LOG_WRN("the oximeter's pairing data is too long");
            return BT_GATT_ITER_STOP;
        }
        (void)memcpy(&sealed[sealed_length], data, length);
        sealed_length += length;
        return BT_GATT_ITER_CONTINUE;
    }
    if ((sealed_length != sizeof sealed) ||
        !pairing_open(oximeter_id, sealed, (uint8_t *)&oximeter_oob)) {
        LOG_WRN("couldn't open the oximeter's pairing data");
        return BT_GATT_ITER_STOP;
    }

    const int start_err = bt_conn_set_security(conn, BT_SECURITY_L4);

    if (start_err != 0) {
        LOG_ERR("pairing didn't start: %d", start_err);
    }
    return BT_GATT_ITER_STOP;
}

void pairing_start(struct bt_conn *conn, uint16_t sealed_handle,
                   const uint8_t device_id[DEVICE_ID_SIZE], const uint8_t key[DEVICE_KEY_SIZE])
{
    if (bt_le_bond_exists(BT_ID_DEFAULT, bt_conn_get_dst(conn))) {
        LOG_INF("using the oximeter's bond");

        const int err = bt_conn_set_security(conn, BT_SECURITY_L4);

        if (err != 0) {
            LOG_ERR("encrypting with the bond didn't start: %d", err);
        }
        return;
    }
    if (!pairing_open_init(key, DEVICE_KEY_SIZE)) {
        LOG_ERR("couldn't load the oximeter's pairing key");
        return;
    }
    (void)memcpy(oximeter_id, device_id, sizeof oximeter_id);
    sealed_length = 0u;
    sealed_read.func = on_sealed;
    sealed_read.handle_count = 1u;
    sealed_read.single.handle = sealed_handle;
    sealed_read.single.offset = 0u;

    const int err = bt_gatt_read(conn, &sealed_read);

    if (err != 0) {
        LOG_ERR("reading the oximeter's pairing data failed: %d", err);
    }
}

/* The oximeter's data only: it has none of the hub's, and doesn't need it. Checking its data
 * proves the oximeter is the one that sealed it; pairing with it proves to the oximeter that the
 * hub could open it. */
static void on_oob_data_request(struct bt_conn *conn, struct bt_conn_oob_info *info)
{
    ARG_UNUSED(info);

    const int err = bt_le_oob_set_sc_data(conn, NULL, &oximeter_oob);

    if (err != 0) {
        LOG_ERR("couldn't give pairing the oximeter's data: %d", err);
        (void)bt_conn_auth_cancel(conn);
    }
}

/* The out-of-band data is a secret only until pairing ends. */
static void on_pairing_complete(struct bt_conn *conn, bool bonded)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(bonded);
    mbedtls_platform_zeroize(&oximeter_oob, sizeof oximeter_oob);
    LOG_INF("paired with the oximeter");
}

static void on_pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    ARG_UNUSED(conn);
    mbedtls_platform_zeroize(&oximeter_oob, sizeof oximeter_oob);
    LOG_WRN("pairing failed (reason %d)", reason);
}

static struct bt_conn_auth_cb auth_callbacks = {
    .oob_data_request = on_oob_data_request,
};

static struct bt_conn_auth_info_cb auth_info_callbacks = {
    .pairing_complete = on_pairing_complete,
    .pairing_failed = on_pairing_failed,
};

/* An oximeter reset for a new patient has no key for the bond: encryption with it fails, key
 * missing. The hub pairs again only when the backend allows it, as it does for the next patient's
 * gateway alone. */
static void on_security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    ARG_UNUSED(level);
    if (err == BT_SECURITY_ERR_PIN_OR_KEY_MISSING) {
        bt_addr_le_copy(&forgotten_by, bt_conn_get_dst(conn));
        forgotten = true;
        LOG_WRN("the oximeter has forgotten this hub");
    }
}

BT_CONN_CB_DEFINE(pairing_connection_callbacks) = {
    .security_changed = on_security_changed,
};

/* The backend's go-ahead, which the hub stands in for. Dropping the bond also ends the link, and
 * the next connection pairs with the oximeter's sealed data. */
static int cmd_pair(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (!forgotten) {
        shell_error(sh, "no oximeter has forgotten this hub");
        return -ENOENT;
    }
    forgotten = false;

    const int err = bt_unpair(BT_ID_DEFAULT, &forgotten_by);

    if (err != 0) {
        shell_error(sh, "couldn't drop the oximeter's bond: %d", err);
        return err;
    }
    LOG_INF("pairing with the oximeter again");
    return 0;
}

SHELL_CMD_REGISTER(pair, NULL, "Pair again with the oximeter that has forgotten this hub",
                   cmd_pair);

int pairing_init(void)
{
    /* Every pairing the hub starts uses the oximeter's out-of-band data. */
    bt_le_oob_set_sc_flag(true);

    const int err = bt_conn_auth_cb_register(&auth_callbacks);

    return (err != 0) ? err : bt_conn_auth_info_cb_register(&auth_info_callbacks);
}
