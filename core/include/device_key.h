#ifndef DEVICE_KEY_H
#define DEVICE_KEY_H

#include <stdbool.h>
#include <stdint.h>

/* Each oximeter's own limits key, provisioned at the factory, and the ID the backend derives it
 * from: FICR DEVICEID, big-endian, as hwinfo_get_device_id gives it. */
#define DEVICE_ID_SIZE 8u
#define DEVICE_KEY_SIZE 32u

/** True when `key` was provisioned: neither erased flash nor zeroed. */
bool device_key_provisioned(const uint8_t key[DEVICE_KEY_SIZE]);

#endif
