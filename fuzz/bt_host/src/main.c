/* Any bytes, as a central that never pairs could send them to the oximeter: each case connects,
 * delivers L2CAP frames on any channel, and leaves. The oximeter's own Bluetooth services and
 * admission answer, started as the firmware starts them. */
#include "alarm_service.h"
#include "device_identity.h"
#include "event_log.h"
#include "hci_fuzz.h"
#include "log_mac.h"
#include "paired_only.h"
#include "pairing.h"
#include "pairing_seal.h"
#include "plx_service.h"

#include <irq_ctrl.h>
#include <nsi_cpu_if.h>
#include <nsi_main_semipublic.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CONNECTION 0x0001u
/* An L2CAP frame's header: its payload's length, then its channel. */
#define L2CAP_HEADER 4u
#define PAYLOAD_MAX (251u - L2CAP_HEADER)
/* Channels with no L2CAP header added: the start of a frame as it is, header and all, or the rest
 * of one. */
#define RAW_START 0xFEu
#define RAW_REST 0xFFu

static const uint8_t *fuzz_case;
static size_t fuzz_case_size;
/* Set once the host has taken a whole case. A host that stops taking packets would otherwise leave
 * every later case unread, and the fuzzer testing nothing. */
static volatile bool fuzz_case_done;
static K_SEM_DEFINE(fuzz_case_ready, 0, 1);

static void on_fuzz_case(const void *arg)
{
    ARG_UNUSED(arg);
    k_sem_give(&fuzz_case_ready);
}

/* An L2CAP frame on `channel`, its header made to fit, so a mutated payload still arrives whole. */
static void send_frame(uint8_t channel, const uint8_t *payload, size_t length)
{
    uint8_t frame[L2CAP_HEADER + PAYLOAD_MAX];
    const size_t taken = MIN(length, (size_t)PAYLOAD_MAX);

    sys_put_le16((uint16_t)taken, &frame[0]);
    sys_put_le16(channel, &frame[2]);
    (void)memcpy(&frame[L2CAP_HEADER], payload, taken);
    hci_fuzz_acl(CONNECTION, true, frame, L2CAP_HEADER + taken);
}

/* Each packet is a channel byte, a length byte, then that many bytes; the last may be cut short. */
static void deliver(const uint8_t *data, size_t size)
{
    size_t at = 0u;

    hci_fuzz_connect(CONNECTION);
    while (at + 2u <= size) {
        const uint8_t channel = data[at];
        const uint8_t *bytes = &data[at + 2u];
        const size_t length = MIN((size_t)data[at + 1u], size - at - 2u);

        if ((channel == RAW_START) || (channel == RAW_REST)) {
            hci_fuzz_acl(CONNECTION, channel == RAW_START, bytes, length);
        } else {
            send_frame(channel, bytes, length);
        }
        at += 2u + length;
    }
    hci_fuzz_disconnect(CONNECTION);
}

/* As app/src/main.c starts them, without the sensor, the watchdog or MCUboot. */
static bool start_the_oximeter(void)
{
    static const struct mcuboot_img_sem_ver version = {.major = 1u};
    uint8_t key[DEVICE_KEY_SIZE];

    return device_identity_key(DEVICE_KEY_LOG, key) && log_mac_init(key, sizeof key) &&
           device_identity_key(DEVICE_KEY_PAIRING, key) && pairing_seal_init(key, sizeof key) &&
           (event_log_start(&version) == 0) && (alarm_service_init() == 0) &&
           (pairing_init() == 0) && (plx_service_start() == 0);
}

int main(void)
{
    paired_only_watch();
    if (!start_the_oximeter()) {
        printk("the oximeter's services didn't start\n");
        abort();
    }
    IRQ_CONNECT(CONFIG_ARCH_POSIX_FUZZ_IRQ, 0, on_fuzz_case, NULL, 0);
    irq_enable(CONFIG_ARCH_POSIX_FUZZ_IRQ);
    while (true) {
        (void)k_sem_take(&fuzz_case_ready, K_FOREVER);
        deliver(fuzz_case, fuzz_case_size);
        fuzz_case_done = true;
    }
    return 0;
}

/* libFuzzer's entry, on native_sim's host side: each case reaches Zephyr as an interrupt, and the
 * simulation runs until it has been handled. */
NATIVE_SIMULATOR_IF int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static bool booted;

    if (!booted) {
        /* The settings and the security log in simulated flash kept in RAM, not in a file. */
        static char *argv[] = {"bt_host_fuzz", "-flash_in_ram", NULL};

        nsi_init(2, argv);
        booted = true;
    }
    fuzz_case = data;
    fuzz_case_size = size;
    fuzz_case_done = false;
    hw_irq_ctrl_set_irq(CONFIG_ARCH_POSIX_FUZZ_IRQ);
    nsi_exec_for(k_ticks_to_us_ceil64(CONFIG_ARCH_POSIX_FUZZ_TICKS));
    if (!fuzz_case_done) {
        printk("the host stopped taking packets\n");
        abort();
    }
    return 0;
}
