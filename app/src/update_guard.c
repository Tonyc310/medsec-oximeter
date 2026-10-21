#include "update_guard.h"

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>

/* Until the running image passes its self-test, only that test decides whether it stays: a
 * gateway that confirmed it, as standard update tools do, could keep a broken image for good. So
 * while it's on trial no image-state change is accepted over SMP. A gateway marks an update for
 * its test boot before the reset, while the image running then is confirmed. */
static enum mgmt_cb_return on_command(uint32_t event, enum mgmt_cb_return prev_status, int32_t *rc,
                                      uint16_t *group, bool *abort_more, void *data,
                                      size_t data_size)
{
    const struct mgmt_evt_op_cmd_arg *command = data;

    ARG_UNUSED(event);
    ARG_UNUSED(prev_status);
    ARG_UNUSED(group);
    ARG_UNUSED(abort_more);
    ARG_UNUSED(data_size);
    if ((command->group == MGMT_GROUP_ID_IMAGE) && (command->id == IMG_MGMT_ID_STATE) &&
        (command->op == MGMT_OP_WRITE) && !boot_is_img_confirmed()) {
        *rc = MGMT_ERR_EACCESSDENIED;
        return MGMT_CB_ERROR_RC;
    }
    return MGMT_CB_OK;
}

static struct mgmt_callback command_hook = {
    .callback = on_command,
    .event_id = MGMT_EVT_OP_CMD_RECV,
};

void update_guard_init(void)
{
    mgmt_callback_register(&command_hook);
}
