#include "device_identity.h"

#include <errno.h>
#include <hal/nrf_uicr.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/byteorder.h>

/* The keys are in UICR's customer registers, eight words each in purpose order, little-endian: out
 * of every flash partition, so updates and settings never touch them, and unreadable by a
 * debugger once the debug port is locked. tools/provision.py writes them. */
#define KEY_WORDS (DEVICE_KEY_SIZE / sizeof(uint32_t))

bool device_identity_key(device_key_purpose_t purpose, uint8_t key[DEVICE_KEY_SIZE])
{
    const size_t first = (size_t)purpose * KEY_WORDS;

    for (size_t i = 0u; i < KEY_WORDS; i++) {
        sys_put_le32(NRF_UICR->CUSTOMER[first + i], &key[i * sizeof(uint32_t)]);
    }
    return device_key_provisioned(key);
}

int device_identity_id(uint8_t id[DEVICE_ID_SIZE])
{
    const ssize_t length = hwinfo_get_device_id(id, DEVICE_ID_SIZE);

    return (length == (ssize_t)DEVICE_ID_SIZE) ? 0 : -EIO;
}
