#include "supervisor.h"

#include "event_log.h"
#include "supervision.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

/* Checked twice a second, a stall is caught within 1.5 s. Each check feeds the watchdog, so four
 * missed checks mean the system has stopped running, and the watchdog resets it. */
#define CHECK_PERIOD_MS 500
#define WATCHDOG_TIMEOUT_MS 2000u
#define STACK_SIZE 1536
/* The lowest application priority: a thread running away above it starves the check too, so
 * the watchdog catches that as well as a hang. */
#define PRIORITY K_LOWEST_APPLICATION_THREAD_PRIO

static const struct device *const watchdog = DEVICE_DT_GET(DT_ALIAS(watchdog0));

/* Samples arrive on the sensor's thread and are checked on this one. */
static struct k_spinlock lock;
static supervision_t supervision;

static supervisor_stall_handler_t stall_handler;
static int watchdog_channel;
static struct k_thread thread;
static K_THREAD_STACK_DEFINE(stack, STACK_SIZE);

void supervisor_sample(void)
{
    const k_spinlock_key_t key = k_spin_lock(&lock);

    supervision_sample(&supervision, k_uptime_get_32());
    k_spin_unlock(&lock, key);
}

static void supervise(void *unused1, void *unused2, void *unused3)
{
    ARG_UNUSED(unused1);
    ARG_UNUSED(unused2);
    bool reported = false;
    bool was_stalled = false;

    ARG_UNUSED(unused3);
    for (;;) {
        k_sleep(K_MSEC(CHECK_PERIOD_MS));
        (void)wdt_feed(watchdog, watchdog_channel);

        const k_spinlock_key_t key = k_spin_lock(&lock);
        const bool stalled = supervision_stalled(&supervision, k_uptime_get_32());

        k_spin_unlock(&lock, key);
        if (stalled != was_stalled) {
            (void)event_log_record(
                stalled ? SECURITY_EVENT_SENSOR_STOPPED : SECURITY_EVENT_SENSOR_RESUMED, NULL, 0u);
            was_stalled = stalled;
        }
        /* Reported at once, then every other check: once a second, the rate readings come at. */
        if (stalled && !reported) {
            stall_handler();
        }
        reported = stalled && !reported;
    }
}

int supervisor_start(supervisor_stall_handler_t on_stall)
{
    const struct wdt_timeout_cfg timeout = {
        .window = {.min = 0u, .max = WATCHDOG_TIMEOUT_MS},
        .callback = NULL,
        .flags = WDT_FLAG_RESET_SOC,
    };

    if (!device_is_ready(watchdog)) {
        return -ENODEV;
    }
    stall_handler = on_stall;

    const k_spinlock_key_t key = k_spin_lock(&lock);

    supervision_init(&supervision, k_uptime_get_32());
    k_spin_unlock(&lock, key);

    watchdog_channel = wdt_install_timeout(watchdog, &timeout);
    if (watchdog_channel < 0) {
        return watchdog_channel;
    }
    /* Paused while a debugger holds the processor, so stepping through code doesn't reset it. */
    const int err = wdt_setup(watchdog, WDT_OPT_PAUSE_HALTED_BY_DBG);

    if (err != 0) {
        return err;
    }
    (void)k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack), supervise, NULL, NULL, NULL,
                          PRIORITY, 0, K_NO_WAIT);
    (void)k_thread_name_set(&thread, "supervisor");
    return 0;
}
