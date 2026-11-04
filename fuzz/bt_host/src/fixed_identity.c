#include "device_identity.h"

#include <string.h>

/* native_sim has no UICR or FICR: the oximeter the Renode tests provision, with a key per purpose
 * that is neither erased nor zeroed, so every service starts as it does on a provisioned device. */
static const uint8_t device_id[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

bool device_identity_key(device_key_purpose_t purpose, uint8_t key[DEVICE_KEY_SIZE])
{
    (void)memset(key, 0xA0 + (int)purpose, DEVICE_KEY_SIZE);
    return true;
}

int device_identity_id(uint8_t id[DEVICE_ID_SIZE])
{
    (void)memcpy(id, device_id, sizeof device_id);
    return 0;
}
