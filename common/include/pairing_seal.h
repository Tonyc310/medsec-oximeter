#ifndef PAIRING_SEAL_H
#define PAIRING_SEAL_H

#include "device_key.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The oximeter's out-of-band pairing data: LE Secure Connections' random value and confirmation,
 * 16 bytes each. Sealed, it's a fresh nonce, the data encrypted, and AES-CCM's tag. */
#define PAIRING_DATA_SIZE 32u
#define PAIRING_SEAL_NONCE_SIZE 13u
#define PAIRING_SEAL_TAG_SIZE 16u
#define PAIRING_SEALED_SIZE (PAIRING_SEAL_NONCE_SIZE + PAIRING_DATA_SIZE + PAIRING_SEAL_TAG_SIZE)

/** Loads the device's pairing key for sealing only, replacing any key loaded before. Returns false,
 * with no key loaded, if it can't. */
bool pairing_seal_init(const uint8_t *key, size_t length);

/** Loads a device's pairing key for opening only, replacing any key loaded before. Returns false,
 * with no key loaded, if it can't. */
bool pairing_open_init(const uint8_t *key, size_t length);

/** Seals `data` for whoever holds the pairing key, bound to `device_id`. */
bool pairing_seal(const uint8_t device_id[DEVICE_ID_SIZE], const uint8_t data[PAIRING_DATA_SIZE],
                  uint8_t sealed[PAIRING_SEALED_SIZE]);

/** Opens what pairing_seal sealed for `device_id` under the same key. Returns false, and nothing in
 * `data` to use, if it doesn't authenticate. */
bool pairing_open(const uint8_t device_id[DEVICE_ID_SIZE],
                  const uint8_t sealed[PAIRING_SEALED_SIZE], uint8_t data[PAIRING_DATA_SIZE]);

#endif
