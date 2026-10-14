#ifndef DEVICE_KEYS_H
#define DEVICE_KEYS_H

#include "device_key.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** HKDF-SHA256 (RFC 5869) of `ikm` with `salt` and `info`, filling `out`. False if it can't. */
bool device_keys_hkdf(const uint8_t *ikm, size_t ikm_length, const uint8_t *salt,
                      size_t salt_length, const uint8_t *info, size_t info_length, uint8_t *out,
                      size_t out_length);

/** The key for `purpose` of the oximeter with `device_id`: HKDF-SHA256 of `master`, with no salt
 * and the purpose's label followed by the ID as info. False if it can't. */
bool device_keys_derive(const uint8_t *master, size_t master_length, device_key_purpose_t purpose,
                        const uint8_t device_id[DEVICE_ID_SIZE], uint8_t key[DEVICE_KEY_SIZE]);

#endif
