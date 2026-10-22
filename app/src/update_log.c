#include "update_log.h"

#include "event_log.h"

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/storage/flash_map.h>

/* Set when an image-state write comes in, read when it's done: both on the SMP thread, which runs
 * one command at a time. */
static bool state_write;

/* The version from the update's signed header: zero if it can't be read, as the marking still
 * matters. */
static void record_pending(void)
{
    struct mcuboot_img_header header = {0};

    (void)boot_read_bank_header(PARTITION_ID(slot1_partition), &header, sizeof header);
    const struct mcuboot_img_sem_ver *const version = &header.h.v1.sem_ver;
    const uint8_t detail[] = {version->major,
                              version->minor,
                              (uint8_t)version->revision,
                              (uint8_t)(version->revision >> 8),
                              (uint8_t)version->build_num,
                              (uint8_t)(version->build_num >> 8),
                              (uint8_t)(version->build_num >> 16),
                              (uint8_t)(version->build_num >> 24)};

    (void)event_log_record(SECURITY_EVENT_UPDATE_PENDING, detail, sizeof detail);
}

/* Zephyr reports no event when an image is marked for a test boot, so once an image-state write is
 * done the log asks MCUboot whether one is now due. */
static enum mgmt_cb_return on_command(uint32_t event, enum mgmt_cb_return prev_status, int32_t *rc,
                                      uint16_t *group, bool *abort_more, void *data,
                                      size_t data_size)
{
    const struct mgmt_evt_op_cmd_arg *command = data;

    ARG_UNUSED(prev_status);
    ARG_UNUSED(rc);
    ARG_UNUSED(group);
    ARG_UNUSED(abort_more);
    ARG_UNUSED(data_size);
    if (event == MGMT_EVT_OP_CMD_RECV) {
        state_write = (command->group == MGMT_GROUP_ID_IMAGE) &&
                      (command->id == IMG_MGMT_ID_STATE) && (command->op == MGMT_OP_WRITE);
    } else if (state_write && (mcuboot_swap_type() == BOOT_SWAP_TYPE_TEST)) {
        state_write = false;
        record_pending();
    }
    return MGMT_CB_OK;
}

static struct mgmt_callback command_hook = {
    .callback = on_command,
    .event_id = MGMT_EVT_OP_CMD_RECV | MGMT_EVT_OP_CMD_DONE,
};

void update_log_init(void)
{
    mgmt_callback_register(&command_hook);
}
