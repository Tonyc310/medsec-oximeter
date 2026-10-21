#include "update.h"

#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt_client.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt_client.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/mgmt/mcumgr/smp/smp_client.h>
#include <zephyr/mgmt/mcumgr/transport/smp.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/storage/flash_map.h>
/* Zephyr's own SMP transports collect what they receive with its reassembly helper, which only this
 * internal header declares: there's no public way in for a transport of one's own. */
#include <mgmt/mcumgr/transport/smp_reassembly.h>

LOG_MODULE_REGISTER(update, LOG_LEVEL_INF);

/* The backend's image, kept where the gateway would keep it: the hub's slot 1 partition, which the
 * hub's own firmware, at the start of flash, doesn't use. Flash is memory-mapped. */
#define STAGED ((const uint8_t *)PARTITION_ADDRESS(slot1_partition))
#define STAGED_MAX PARTITION_SIZE(slot1_partition)

/* MCUboot's slot for an update. */
#define SECONDARY_SLOT 1u

/* Set from the Bluetooth receive thread, read from the shell's while an update runs; one oximeter
 * at a time. */
static struct bt_conn *oximeter;
static uint16_t smp_handle;
static bool subscribed;

static struct smp_transport transport;
static struct smp_client_transport_entry transport_entry;
static struct smp_client_object smp_client;
static struct img_mgmt_client image_client;
static struct os_mgmt_client os_client;
static struct mcumgr_image_data images[2];
static struct bt_gatt_subscribe_params responses;
static struct bt_gatt_discover_params responses_ccc;

/* An ATT write carries the MTU less its 3-byte header. */
static uint16_t request_size(const struct net_buf *nb)
{
    ARG_UNUSED(nb);
    return (oximeter == NULL) ? 0u : (uint16_t)(bt_gatt_get_mtu(oximeter) - 3u);
}

/* A request in ATT writes of up to the size above; the oximeter puts them back together. */
static int send_request(struct net_buf *nb)
{
    int err = (oximeter == NULL) ? -ENOTCONN : 0;

    for (uint16_t sent = 0u; (err == 0) && (sent < nb->len);) {
        const uint16_t piece = MIN(nb->len - sent, request_size(nb));

        err = bt_gatt_write_without_response(oximeter, smp_handle, &nb->data[sent], piece, false);
        sent += piece;
    }
    smp_packet_free(nb);
    return err;
}

/* A response longer than one notification, such as the image list, comes in pieces: the SMP
 * header says how long it is, and it goes to the client once it's all here. */
static uint8_t on_response(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                           const void *data, uint16_t length)
{
    ARG_UNUSED(conn);
    if (data == NULL) {
        params->value_handle = 0u;
        subscribed = false;
        return BT_GATT_ITER_STOP;
    }

    const int remaining = smp_reassembly_collect(&transport, data, length);

    if (remaining < 0) {
        LOG_WRN("SMP response dropped (%d)", remaining);
        smp_reassembly_drop(&transport);
    } else if (remaining == 0) {
        (void)smp_reassembly_complete(&transport, false);
    }
    return BT_GATT_ITER_CONTINUE;
}

void update_found(struct bt_conn *conn, uint16_t handle)
{
    oximeter = conn;
    smp_handle = handle;
}

/* The oximeter answers in notifications: commands wait until their CCC write is through. */
static void on_subscribed(struct bt_conn *conn, uint8_t err,
                          struct bt_gatt_subscribe_params *params)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(params);
    if (err != BT_ATT_ERR_SUCCESS) {
        LOG_WRN("subscribing to SMP failed (ATT error 0x%02x)", err);
        return;
    }
    subscribed = true;
    LOG_INF("ready to update the oximeter");
}

void update_link_secured(struct bt_conn *conn)
{
    if (smp_handle == 0u) {
        return;
    }
    responses.value_handle = smp_handle;
    responses.ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
    responses.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    responses.disc_params = &responses_ccc;
    responses.value = BT_GATT_CCC_NOTIFY;
    responses.notify = on_response;
    responses.subscribe = on_subscribed;
    /* Made again on each link, as main.c's are. */
    atomic_set_bit(responses.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

    const int err = bt_gatt_subscribe(conn, &responses);

    if ((err != 0) && (err != -EALREADY)) {
        LOG_ERR("subscribing to SMP failed: %d", err);
    }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(reason);
    oximeter = NULL;
}

BT_CONN_CB_DEFINE(update_connection_callbacks) = {
    .disconnected = on_disconnected,
};

static bool ready(const struct shell *sh)
{
    if ((oximeter == NULL) || !subscribed) {
        shell_error(sh, "the oximeter isn't ready for updates");
        return false;
    }
    return true;
}

static const char *uploaded_hash(void)
{
    struct mcumgr_image_state state;

    if ((img_mgmt_client_state_read(&image_client, &state) != MGMT_ERR_EOK) ||
        (state.status != MGMT_ERR_EOK)) {
        return NULL;
    }
    for (int i = 0; i < state.image_list_length; i++) {
        if (state.image_list[i].slot_num == SECONDARY_SLOT) {
            return state.image_list[i].hash;
        }
    }
    return NULL;
}

/* Sends the staged image, marks it for a test boot, and resets the oximeter into it: it runs once,
 * and stays only if it passes its self-test. */
static int cmd_update(const struct shell *sh, size_t argc, char **argv)
{
    int err = 0;
    const unsigned long size = shell_strtoul(argv[1], 0, &err);

    ARG_UNUSED(argc);
    if ((err != 0) || (size == 0u) || (size > STAGED_MAX)) {
        shell_error(sh, "%s isn't the size of a staged image", argv[1]);
        return -EINVAL;
    }
    if (!ready(sh)) {
        return -ENOTCONN;
    }
    /* The shortest connection interval while the image goes: about 20 s rather than 2 min. */
    (void)bt_conn_le_param_update(oximeter, BT_LE_CONN_PARAM(6, 6, 0, 400));

    struct mcumgr_image_upload upload = {0};

    if ((img_mgmt_client_upload_init(&image_client, size, 0u, NULL) != MGMT_ERR_EOK) ||
        (img_mgmt_client_upload(&image_client, STAGED, size, &upload) != MGMT_ERR_EOK) ||
        (upload.status != MGMT_ERR_EOK)) {
        shell_error(sh, "sending the update failed (%d)", upload.status);
        return -EIO;
    }

    const char *hash = uploaded_hash();
    struct mcumgr_image_state state = {0};

    if ((hash == NULL) ||
        (img_mgmt_client_state_write(&image_client, (char *)hash, false, &state) != MGMT_ERR_EOK) ||
        (state.status != MGMT_ERR_EOK)) {
        shell_error(sh, "marking the update for a test boot failed (%d)", state.status);
        return -EIO;
    }
    if (os_mgmt_client_reset(&os_client) != MGMT_ERR_EOK) {
        shell_error(sh, "resetting the oximeter failed");
        return -EIO;
    }
    LOG_INF("the oximeter is restarting with the update");
    return 0;
}

/* Standard MCUmgr tools confirm an update from outside once it's running, and the hub can do the
 * same. This oximeter refuses: it keeps an update only once its own self-test passes. */
static int cmd_confirm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (!ready(sh)) {
        return -ENOTCONN;
    }

    struct mcumgr_image_state state = {0};
    const int err = img_mgmt_client_state_write(&image_client, NULL, true, &state);

    if ((err != MGMT_ERR_EOK) || (state.status != MGMT_ERR_EOK)) {
        LOG_WRN("the oximeter refused to keep the update (%d, %d)", err, state.status);
        return -EACCES;
    }
    LOG_INF("the oximeter keeps the image it runs for good");
    return 0;
}

SHELL_CMD_REGISTER(confirm, NULL, "Ask the oximeter to keep the image it runs for good",
                   cmd_confirm);

SHELL_CMD_ARG_REGISTER(update, NULL,
                       "Send the oximeter the update staged in slot 1: update <size in bytes>",
                       cmd_update, 2, 0);

int update_init(void)
{
    transport.functions.output = send_request;
    transport.functions.get_mtu = request_size;

    int err = smp_transport_init(&transport);

    if (err != 0) {
        return err;
    }
    transport_entry.smpt = &transport;
    transport_entry.smpt_type = SMP_BLUETOOTH_TRANSPORT;
    smp_client_transport_register(&transport_entry);
    err = smp_client_object_init(&smp_client, SMP_BLUETOOTH_TRANSPORT);
    if (err != 0) {
        return err;
    }
    img_mgmt_client_init(&image_client, &smp_client, ARRAY_SIZE(images), images);
    os_mgmt_client_init(&os_client, &smp_client);
    return 0;
}
