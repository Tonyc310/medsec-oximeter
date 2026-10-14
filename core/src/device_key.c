#include "device_key.h"

#include <stddef.h>

bool device_key_provisioned(const uint8_t key[DEVICE_KEY_SIZE])
{
    bool all_ones = true;
    bool all_zeros = true;

    for (size_t i = 0u; i < DEVICE_KEY_SIZE; i++) {
        all_ones = all_ones && (key[i] == 0xFFu);
        all_zeros = all_zeros && (key[i] == 0x00u);
    }
    return !all_ones && !all_zeros;
}
