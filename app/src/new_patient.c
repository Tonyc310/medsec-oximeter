#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(new_patient, LOG_LEVEL_INF);

/* Before the oximeter goes to the next patient, staff hold Button 1 for 5 s: it forgets the
 * gateway it was paired with and starts again, so a gateway must pair afresh, which the backend
 * allows only for the next patient's. Bluetooth and flash calls need the system work queue's
 * stack, not the input thread's. */
static void reset_for_new_patient(struct k_work *work)
{
    ARG_UNUSED(work);
    LOG_WRN("reset for a new patient: forgetting the gateway");
    if (bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY) != 0) {
        LOG_ERR("could not forget the gateway");
    }
    /* The log goes out before the restart that would cut it off. */
    log_panic();
    sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DEFINE(reset_work, reset_for_new_patient);

/* The overlay's long press reports Delete: the code only names the event. */
static void on_input(struct input_event *event, void *user_data)
{
    ARG_UNUSED(user_data);
    if ((event->code == INPUT_KEY_DELETE) && (event->value != 0)) {
        (void)k_work_submit(&reset_work);
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(new_patient_press)), on_input, NULL);
