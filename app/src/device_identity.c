#include "device_identity.h"

#include <errno.h>
#include <hal/nrf_uicr.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/byteorder.h>

/* The key is the first eight of UICR's customer registers, little-endian: out of every flash
 * partition, so updates and settings never touch it, and unreadable by a debugger once the
 * debug port is locked. tools/provision.py writes it. */
#define KEY_WORDS (DEVICE_KEY_SIZE / sizeof(uint32_t))

bool device_identity_key(uint8_t key[DEVICE_KEY_SIZE])
{
    for (size_t i = 0u; i < KEY_WORDS; i++) {
        sys_put_le32(NRF_UICR->CUSTOMER[i], &key[i * sizeof(uint32_t)]);
    }
    return device_key_provisioned(key);
}

int device_identity_id(uint8_t id[DEVICE_ID_SIZE])
{
    const ssize_t length = hwinfo_get_device_id(id, DEVICE_ID_SIZE);

    return (length == (ssize_t)DEVICE_ID_SIZE) ? 0 : -EIO;
}
