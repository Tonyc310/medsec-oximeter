#include "admission.h"

#include "link_admission.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(admission, LOG_LEVEL_INF);

BUILD_ASSERT(CONFIG_BT_MAX_CONN == LINK_ADMISSION_CONNECTIONS,
             "one connection for any central, one kept for the gateway");

static const struct bt_data advertising[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_POS_VAL)),
};

static const struct bt_data scan_response[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1u),
};

/* The controller takes a connection only from a central on its accept list. */
static const struct bt_le_adv_param gateway_only =
    BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_FILTER_CONN, BT_GAP_ADV_FAST_INT_MIN_1,
                         BT_GAP_ADV_FAST_INT_MAX_1, NULL);

/* The connection callbacks run on the Bluetooth receive thread, the recycled one on the system
 * work queue. */
static K_MUTEX_DEFINE(advertising_lock);

static void count_link(struct bt_conn *conn, void *data)
{
    link_state_t *links = data;
    struct bt_conn_info info;

    if ((bt_conn_get_info(conn, &info) != 0) || (info.state != BT_CONN_STATE_CONNECTED)) {
        return;
    }
    links->connected++;
    if (bt_conn_get_security(conn) >= BT_SECURITY_L4) {
        links->gateway_linked = true;
    }
}

/* The oximeter keeps one bond (CONFIG_BT_MAX_PAIRED), the gateway's. */
static void note_gateway(const struct bt_bond_info *bond, void *data)
{
    bt_addr_le_copy(data, &bond->addr);
}

static int advertise_with(const struct bt_le_adv_param *param)
{
    return bt_le_adv_start(param, advertising, ARRAY_SIZE(advertising), scan_response,
                           ARRAY_SIZE(scan_response));
}

/* Advertising stops when a central connects, and the accept list can't change while it runs, so
 * every change to the links stops it and starts it again as link_admission decides. */
static void advertise(void)
{
    link_state_t links = {0};
    bt_addr_le_t gateway;
    int err = 0;

    bt_addr_le_copy(&gateway, BT_ADDR_LE_ANY);
    (void)k_mutex_lock(&advertising_lock, K_FOREVER);
    (void)bt_le_adv_stop();
    bt_conn_foreach(BT_CONN_TYPE_LE, count_link, &links);
    bt_foreach_bond(BT_ID_DEFAULT, note_gateway, &gateway);
    links.bonded = !bt_addr_le_eq(&gateway, BT_ADDR_LE_ANY);

    const link_admission_t admitted = link_admission(links);

    if (admitted == LINK_ADMIT_ANYONE) {
        err = advertise_with(BT_LE_ADV_CONN_FAST_1);
    } else if (admitted == LINK_ADMIT_GATEWAY) {
        err = bt_le_filter_accept_list_clear();
        if (err == 0) {
            err = bt_le_filter_accept_list_add(&gateway);
        }
        if (err == 0) {
            err = advertise_with(&gateway_only);
        }
    }
    (void)k_mutex_unlock(&advertising_lock);
    if (err != 0) {
        LOG_ERR("advertising failed: %d", err);
    } else if (admitted != LINK_ADMIT_NOBODY) {
        LOG_INF("advertising to %s",
                (admitted == LINK_ADMIT_ANYONE) ? "any central" : "the gateway only");
    } else {
        LOG_INF("not advertising");
    }
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    ARG_UNUSED(conn);
    if (err == 0u) {
        LOG_INF("central connected");
    }
    advertise();
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    LOG_INF("central disconnected (reason 0x%02x)", reason);
}

/* Reaching level 4 makes a central the gateway, and then nobody else needs a connection. */
static void on_security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(level);
    ARG_UNUSED(err);
    advertise();
}

/* A connectable advertiser needs a free connection object, and a disconnected central's is free
 * only once it's recycled. */
static void on_recycled(void)
{
    advertise();
}

BT_CONN_CB_DEFINE(admission_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .security_changed = on_security_changed,
    .recycled = on_recycled,
};

void admission_start(void)
{
    advertise();
}
