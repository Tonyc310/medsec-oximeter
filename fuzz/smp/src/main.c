/* Any bytes, as Bluetooth writes from the paired gateway to the oximeter's SMP characteristic.
 * MCUmgr reassembles them into requests and handles them as the oximeter does, update guard and
 * all. Whatever arrives, the running image's slot is left as it was: a gateway may stage an update
 * in slot 1, never change what runs. */
#include "update_guard.h"

#include <bootutil/bootutil_public.h>
#include <bootutil/image.h>
#include <irq_ctrl.h>
#include <mgmt/mcumgr/transport/smp_reassembly.h>
#include <nsi_cpu_if.h>
#include <nsi_main_semipublic.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/mgmt/mcumgr/transport/smp.h>
#include <zephyr/storage/flash_map.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* What one ATT write carries on the oximeter's link: its 247-byte MTU less the 3-byte header. */
#define ATT_WRITE_MAX 244u
#define PROGRAM_SIZE 32u

/* Trailer writes MCUboot's library makes for MCUboot itself; its public header leaves them out. */
int boot_write_magic(const struct flash_area *fap);
int boot_write_copy_done(const struct flash_area *fap);

/* Slot 0's bytes in the simulated flash, and a copy from before the first case. */
static const uint8_t *slot0_now;
static uint8_t slot0_before[PARTITION_SIZE(slot0_partition)];

/* An update running on trial, as MCUboot leaves one after a test swap: an image MCUmgr can read (a
 * header, some program, its hash) and a trailer saying it was copied in but isn't confirmed. This
 * is the state the update guard protects. */
static void install_image_on_trial(void)
{
    struct {
        struct image_header header;
        uint8_t program[PROGRAM_SIZE];
        struct image_tlv_info info;
        struct image_tlv hash_tlv;
        uint8_t hash[IMAGE_HASH_LEN];
    } __packed image = {
        .header = {.ih_magic = IMAGE_MAGIC,
                   .ih_hdr_size = sizeof(struct image_header),
                   .ih_img_size = PROGRAM_SIZE,
                   .ih_ver = {.iv_major = 1u}},
        .info = {.it_magic = IMAGE_TLV_INFO_MAGIC,
                 .it_tlv_tot =
                     sizeof(struct image_tlv_info) + sizeof(struct image_tlv) + IMAGE_HASH_LEN},
        .hash_tlv = {.it_type = IMAGE_TLV_SHA256, .it_len = IMAGE_HASH_LEN},
    };
    const struct flash_area *slot0;

    (void)memset(image.hash, 0xAB, sizeof image.hash);
    if ((flash_area_open(PARTITION_ID(slot0_partition), &slot0) != 0) ||
        (flash_area_write(slot0, 0, &image, sizeof image) != 0) || (boot_write_magic(slot0) != 0) ||
        (boot_write_copy_done(slot0) != 0) || (mcuboot_swap_type() != BOOT_SWAP_TYPE_REVERT)) {
        printk("slot 0 isn't on trial\n");
        abort();
    }
    flash_area_close(slot0);
}

static int drop_response(struct net_buf *response)
{
    smp_packet_free(response);
    return 0;
}

static uint16_t att_payload(const struct net_buf *response)
{
    ARG_UNUSED(response);
    return ATT_WRITE_MAX;
}

static struct smp_transport transport = {
    .functions = {.output = drop_response, .get_mtu = att_payload},
};

/* A reset would end the fuzzer. */
static enum mgmt_cb_return refuse_reset(uint32_t event, enum mgmt_cb_return prev_status,
                                        int32_t *rc, uint16_t *group, bool *abort_more, void *data,
                                        size_t data_size)
{
    ARG_UNUSED(event);
    ARG_UNUSED(prev_status);
    ARG_UNUSED(group);
    ARG_UNUSED(abort_more);
    ARG_UNUSED(data);
    ARG_UNUSED(data_size);
    *rc = MGMT_ERR_EBUSY;
    return MGMT_CB_ERROR_RC;
}

static struct mgmt_callback reset_hook = {
    .callback = refuse_reset,
    .event_id = MGMT_EVT_OP_OS_MGMT_RESET,
};

static const uint8_t *fuzz_case;
static size_t fuzz_case_size;
static K_SEM_DEFINE(fuzz_case_ready, 0, 1);

static void on_fuzz_case(const void *arg)
{
    ARG_UNUSED(arg);
    k_sem_give(&fuzz_case_ready);
}

/* Each write is a length byte, then that many bytes; the last may be cut short. As on the
 * Bluetooth transport, a write MCUmgr can't take drops the request it was part of. */
static void deliver(const uint8_t *data, size_t size)
{
    size_t at = 0u;

    while (at < size) {
        const size_t length = MIN(MIN((size_t)data[at], ATT_WRITE_MAX), size - at - 1u);
        const int left = smp_reassembly_collect(&transport, &data[at + 1u], (uint16_t)length);

        if (left < 0) {
            (void)smp_reassembly_drop(&transport);
        } else if (left == 0) {
            (void)smp_reassembly_complete(&transport, false);
        }
        at += 1u + length;
    }
    /* A request left unfinished doesn't carry over into the next case. */
    (void)smp_reassembly_drop(&transport);
}

int main(void)
{
    const struct device *flash = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
    size_t flash_size;

    install_image_on_trial();
    slot0_now = (const uint8_t *)flash_simulator_get_memory(flash, &flash_size) +
                PARTITION_OFFSET(slot0_partition);
    (void)memcpy(slot0_before, slot0_now, sizeof slot0_before);
    smp_transport_init(&transport);
    update_guard_init();
    mgmt_callback_register(&reset_hook);
    IRQ_CONNECT(CONFIG_ARCH_POSIX_FUZZ_IRQ, 0, on_fuzz_case, NULL, 0);
    irq_enable(CONFIG_ARCH_POSIX_FUZZ_IRQ);
    while (true) {
        (void)k_sem_take(&fuzz_case_ready, K_FOREVER);
        deliver(fuzz_case, fuzz_case_size);
        /* MCUmgr's work queue outranks this thread, so the case has been handled by now. */
        if (memcmp(slot0_before, slot0_now, sizeof slot0_before) != 0) {
            printk("slot 0 changed\n");
            abort();
        }
    }
    return 0;
}

/* libFuzzer's entry, on native_sim's host side: each case reaches Zephyr as an interrupt, and the
 * simulation runs until it has been handled. */
NATIVE_SIMULATOR_IF int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static bool booted;

    if (!booted) {
        /* Flash in RAM: every run starts from the same flash and leaves no file behind. */
        static char *argv[] = {"smp_fuzz", "-flash_in_ram", NULL};

        nsi_init(2, argv);
        booted = true;
    }
    fuzz_case = data;
    fuzz_case_size = size;
    hw_irq_ctrl_set_irq(CONFIG_ARCH_POSIX_FUZZ_IRQ);
    nsi_exec_for(k_ticks_to_us_ceil64(CONFIG_ARCH_POSIX_FUZZ_TICKS));
    return 0;
}
