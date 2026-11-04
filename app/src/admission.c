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

/* The controller takes a connection only from a central on its accept list. */
static const struct bt_le_adv_param gateway_only =
    BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_FILTER_CONN, BT_GAP_ADV_FAST_INT_MIN_1,
                         BT_GAP_ADV_FAST_INT_MAX_1, NULL);

/* Bluetooth's own pairing timeout. The gateway pairs, or encrypts with its bond, well within it; a
 * central still below level 4 by then is a stranger holding a connection, and is dropped. */
#define PAIRING_DEADLINE_MS 30000

/* A real deadline is at least PAIRING_DEADLINE_MS of uptime. */
#define NO_DEADLINE 0

/* The connection callbacks run on the Bluetooth receive thread, the recycled one and the pairing
 * deadline on the system work queue. */
static K_MUTEX_DEFINE(links_lock);

/* Uptime by which each connection's central must reach level 4, by connection index. The connected
 * callback sets it and the disconnected one clears it: bt_conn_foreach can find a connection before
 * its connected callback runs, which would otherwise meet the last central's deadline. */
static int64_t deadline[CONFIG_BT_MAX_CONN];

/* Whom the oximeter advertises to now, under links_lock. */
static link_admission_t advertised = LINK_ADMIT_NOBODY;

static void drop_unpaired(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(pairing_deadline, drop_unpaired);

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
    return bt_le_adv_start(param, advertising, ARRAY_SIZE(advertising), NULL, 0u);
}

/* Advertising stops when a central connects, and the accept list can't change while it runs, so it
 * starts again as link_admission decides whenever that changes. Only then: stopping connectable
 * advertising frees its connection object, and recycling that object calls this again. */
static void advertise(void)
{
    link_state_t links = {0};
    bt_addr_le_t gateway;
    int err = 0;

    bt_addr_le_copy(&gateway, BT_ADDR_LE_ANY);
    (void)k_mutex_lock(&links_lock, K_FOREVER);
    bt_conn_foreach(BT_CONN_TYPE_LE, count_link, &links);
    bt_foreach_bond(BT_ID_DEFAULT, note_gateway, &gateway);
    links.bonded = !bt_addr_le_eq(&gateway, BT_ADDR_LE_ANY);

    const link_admission_t admitted = link_admission(links);

    if (admitted == advertised) {
        (void)k_mutex_unlock(&links_lock);
        return;
    }
    (void)bt_le_adv_stop();
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
    advertised = (err == 0) ? admitted : LINK_ADMIT_NOBODY;
    (void)k_mutex_unlock(&links_lock);
    if (err != 0) {
        LOG_ERR("advertising failed: %d", err);
    } else if (admitted != LINK_ADMIT_NOBODY) {
        LOG_INF("advertising to %s",
                (admitted == LINK_ADMIT_ANYONE) ? "any central" : "the gateway only");
    } else {
        LOG_INF("not advertising");
    }
}

static void drop_if_unpaired(struct bt_conn *conn, void *data)
{
    int64_t *next = data;
    const uint8_t index = bt_conn_index(conn);

    if ((deadline[index] == NO_DEADLINE) || (bt_conn_get_security(conn) >= BT_SECURITY_L4)) {
        return;
    }
    if (k_uptime_get() < deadline[index]) {
        *next = MIN(*next, deadline[index]);
        return;
    }
    deadline[index] = NO_DEADLINE;
    LOG_WRN("central didn't pair in time");
    (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}

static void drop_unpaired(struct k_work *work)
{
    int64_t next = INT64_MAX;

    ARG_UNUSED(work);
    (void)k_mutex_lock(&links_lock, K_FOREVER);
    bt_conn_foreach(BT_CONN_TYPE_LE, drop_if_unpaired, &next);
    (void)k_mutex_unlock(&links_lock);
    if (next != INT64_MAX) {
        (void)k_work_schedule(&pairing_deadline, K_TIMEOUT_ABS_MS(next));
    }
}

static void set_deadline(const struct bt_conn *conn, int64_t at)
{
    (void)k_mutex_lock(&links_lock, K_FOREVER);
    deadline[bt_conn_index(conn)] = at;
    (void)k_mutex_unlock(&links_lock);
}

/* Zephyr's connection callback types give the connection as non-const, here and below. */
/* cppcheck-suppress constParameterCallback */
static void on_connected(struct bt_conn *conn, uint8_t err)
{
    /* The controller stops advertising as it reports a connection. */
    (void)k_mutex_lock(&links_lock, K_FOREVER);
    advertised = LINK_ADMIT_NOBODY;
    (void)k_mutex_unlock(&links_lock);
    if (err == 0u) {
        LOG_INF("central connected");
        set_deadline(conn, k_uptime_get() + PAIRING_DEADLINE_MS);
        /* Every deadline is as far off as this one, so one already pending comes first. */
        (void)k_work_schedule(&pairing_deadline, K_MSEC(PAIRING_DEADLINE_MS));
    }
    advertise();
}

/* cppcheck-suppress constParameterCallback */
static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    set_deadline(conn, NO_DEADLINE);
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
