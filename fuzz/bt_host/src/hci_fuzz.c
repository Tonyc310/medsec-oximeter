#define DT_DRV_COMPAT vnd_bt_hci_fuzz

#include "hci_fuzz.h"

#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/device.h>
#include <zephyr/drivers/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include <string.h>

/* Room for any command's return parameters; those of a command not answered below are zeros. */
#define RETURN_MAX 64u
#define ACL_SIZE 251u
#define ACL_COUNT 4u
#define CONTROLLER_STACK_SIZE 2048

static bt_hci_recv_t host_recv;
static const struct device *controller;

static void to_host(struct net_buf *buf)
{
    (void)host_recv(controller, buf);
}

static void complete(uint16_t opcode, const void *parameters, uint8_t length)
{
    struct net_buf *buf = bt_hci_cmd_complete_create(opcode, length);

    net_buf_add_mem(buf, parameters, length);
    to_host(buf);
}

static void event(uint8_t code, const void *parameters, uint8_t length)
{
    struct net_buf *buf = bt_hci_evt_create(code, length);

    net_buf_add_mem(buf, parameters, length);
    to_host(buf);
}

static void le_event(uint8_t subevent, const void *parameters, uint8_t length)
{
    struct net_buf *buf = bt_hci_evt_create(BT_HCI_EVT_LE_META_EVENT, 1u + length);

    net_buf_add_u8(buf, subevent);
    net_buf_add_mem(buf, parameters, length);
    to_host(buf);
}

/* What the controller says about itself: Bluetooth 5.4, LE only, with encryption, data length
 * extension and the 2M PHY, as the nRF52840's has. */
static void answer(uint16_t opcode)
{
    uint8_t zeros[RETURN_MAX] = {0};

    switch (opcode) {
    case BT_HCI_OP_READ_LOCAL_VERSION_INFO: {
        const struct bt_hci_rp_read_local_version_info version = {
            .hci_version = BT_HCI_VERSION_5_4,
            .hci_revision = 0u,
            .lmp_version = BT_HCI_VERSION_5_4,
            .manufacturer = sys_cpu_to_le16(0xFFFFu),
        };

        complete(opcode, &version, sizeof version);
        break;
    }
    case BT_HCI_OP_READ_SUPPORTED_COMMANDS: {
        struct bt_hci_rp_read_supported_commands commands;

        (void)memset(&commands, 0xFF, sizeof commands);
        commands.status = BT_HCI_ERR_SUCCESS;
        /* No P-256 commands, so the host does that itself, as the oximeter's does. */
        commands.commands[34] &= (uint8_t)~(BIT(1) | BIT(2));
        complete(opcode, &commands, sizeof commands);
        break;
    }
    case BT_HCI_OP_READ_LOCAL_FEATURES: {
        struct bt_hci_rp_read_local_features features = {0};

        /* LE, and no BR/EDR. */
        features.features[4] = BIT(5) | BIT(6);
        complete(opcode, &features, sizeof features);
        break;
    }
    case BT_HCI_OP_READ_BD_ADDR: {
        const struct bt_hci_rp_read_bd_addr address = {
            .bdaddr = {{0x11, 0x22, 0x33, 0x44, 0x55, 0x66}}};

        complete(opcode, &address, sizeof address);
        break;
    }
    case BT_HCI_OP_LE_READ_LOCAL_FEATURES: {
        struct bt_hci_rp_le_read_local_features features = {0};

        features.features[0] = BIT(BT_LE_FEAT_BIT_ENC) | BIT(BT_LE_FEAT_BIT_DLE);
        features.features[1] = BIT(BT_LE_FEAT_BIT_PHY_2M - 8);
        complete(opcode, &features, sizeof features);
        break;
    }
    case BT_HCI_OP_LE_READ_BUFFER_SIZE: {
        const struct bt_hci_rp_le_read_buffer_size size = {
            .le_max_len = sys_cpu_to_le16(ACL_SIZE),
            .le_max_num = ACL_COUNT,
        };

        complete(opcode, &size, sizeof size);
        break;
    }
    case BT_HCI_OP_LE_READ_MAX_DATA_LEN: {
        const struct bt_hci_rp_le_read_max_data_len length = {
            .max_tx_octets = sys_cpu_to_le16(ACL_SIZE),
            .max_tx_time = sys_cpu_to_le16(2120u),
            .max_rx_octets = sys_cpu_to_le16(ACL_SIZE),
            .max_rx_time = sys_cpu_to_le16(2120u),
        };

        complete(opcode, &length, sizeof length);
        break;
    }
    case BT_HCI_OP_LE_READ_FAL_SIZE: {
        const struct bt_hci_rp_le_read_fal_size size = {.fal_size = 8u};

        complete(opcode, &size, sizeof size);
        break;
    }
    default:
        complete(opcode, zeros, sizeof zeros);
        break;
    }
}

/* The host waits for each packet it sends to be reported sent before it sends more. */
static atomic_t sent;
static uint16_t sent_handle;

static void report_sent(struct k_work *work)
{
    ARG_UNUSED(work);
    const atomic_val_t count = atomic_clear(&sent);
    struct {
        uint8_t handles;
        uint16_t handle;
        uint16_t count;
    } __packed completed = {.handles = 1u,
                            .handle = sys_cpu_to_le16(sent_handle),
                            .count = sys_cpu_to_le16((uint16_t)count)};

    if (count > 0) {
        event(BT_HCI_EVT_NUM_COMPLETED_PACKETS, &completed, sizeof completed);
    }
}

static K_WORK_DEFINE(sent_work, report_sent);

/* The reports come from a thread of the controller's own, above every host thread, as the
 * nRF52840's controller sends its events: the host and the oximeter wait on the system work queue
 * for commands to complete, and the host can't send them while packets wait to be reported. */
static K_THREAD_STACK_DEFINE(controller_stack, CONTROLLER_STACK_SIZE);
static struct k_work_q controller_queue;

static int fuzz_open(const struct device *dev, bt_hci_recv_t recv)
{
    controller = dev;
    host_recv = recv;
    k_work_queue_start(&controller_queue, controller_stack, K_THREAD_STACK_SIZEOF(controller_stack),
                       K_PRIO_COOP(CONFIG_BT_DRIVER_RX_HIGH_PRIO), NULL);
    return 0;
}

static int fuzz_send(const struct device *dev, struct net_buf *buf)
{
    ARG_UNUSED(dev);
    const uint8_t type = net_buf_pull_u8(buf);

    if (type == BT_HCI_H4_CMD) {
        const struct bt_hci_cmd_hdr *header = (const void *)buf->data;

        answer(sys_le16_to_cpu(header->opcode));
    } else if (type == BT_HCI_H4_ACL) {
        const struct bt_hci_acl_hdr *header = (const void *)buf->data;

        sent_handle = bt_acl_handle(sys_le16_to_cpu(header->handle));
        (void)atomic_inc(&sent);
        (void)k_work_submit_to_queue(&controller_queue, &sent_work);
    }
    net_buf_unref(buf);
    return 0;
}

void hci_fuzz_connect(uint16_t handle)
{
    const struct bt_hci_evt_le_enh_conn_complete connected = {
        .handle = sys_cpu_to_le16(handle),
        .role = BT_HCI_ROLE_PERIPHERAL,
        .peer_addr = {.type = BT_ADDR_LE_RANDOM, .a = {{0x01, 0x02, 0x03, 0x04, 0x05, 0xC6}}},
        .interval = sys_cpu_to_le16(24u),
        .supv_timeout = sys_cpu_to_le16(42u),
    };

    le_event(BT_HCI_EVT_LE_ENH_CONN_COMPLETE, &connected, sizeof connected);
}

void hci_fuzz_disconnect(uint16_t handle)
{
    const struct bt_hci_evt_disconn_complete disconnected = {
        .handle = sys_cpu_to_le16(handle),
        .reason = BT_HCI_ERR_REMOTE_USER_TERM_CONN,
    };

    event(BT_HCI_EVT_DISCONN_COMPLETE, &disconnected, sizeof disconnected);
}

void hci_fuzz_acl(uint16_t handle, bool start, const uint8_t *data, size_t length)
{
    struct net_buf *buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_FOREVER);
    struct bt_hci_acl_hdr *header = net_buf_add(buf, sizeof *header);
    const size_t taken = MIN(length, (size_t)ACL_SIZE);

    header->handle =
        sys_cpu_to_le16(bt_acl_handle_pack(handle, start ? BT_ACL_START : BT_ACL_CONT));
    header->len = sys_cpu_to_le16((uint16_t)taken);
    net_buf_add_mem(buf, data, taken);
    to_host(buf);
}

static DEVICE_API(bt_hci, fuzz_api) = {
    .open = fuzz_open,
    .send = fuzz_send,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
                      &fuzz_api);
