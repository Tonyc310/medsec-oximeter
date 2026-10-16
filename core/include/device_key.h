#ifndef DEVICE_KEY_H
#define DEVICE_KEY_H

#include <stdbool.h>
#include <stdint.h>

/* Each oximeter's own limits key, provisioned at the factory, and the ID the backend derives it
 * from: FICR DEVICEID, big-endian, as hwinfo_get_device_id gives it. */
#define DEVICE_ID_SIZE 8u
#define DEVICE_KEY_SIZE 32u

/* Each device has a key per job, never one key for two: they're derived with different labels, and
 * kept one after the other in UICR. */
typedef enum {
    DEVICE_KEY_LIMITS = 0,  /* checks alarm-limit changes */
    DEVICE_KEY_LOG = 1,     /* tags security log records */
    DEVICE_KEY_PAIRING = 2, /* seals its out-of-band pairing data */
} device_key_purpose_t;

/** True when `key` was provisioned: neither erased flash nor zeroed. */
bool device_key_provisioned(const uint8_t key[DEVICE_KEY_SIZE]);

#endif
