#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

#include "device_key.h"

#include <stdbool.h>
#include <stdint.h>

/** Copies the key for `purpose` the factory provisioned into UICR. False, with `key` holding
 * whatever UICR does, when none was. */
bool device_identity_key(device_key_purpose_t purpose, uint8_t key[DEVICE_KEY_SIZE]);

/** The chip's unique ID, which the backend derives the key from. Returns 0 or -errno. */
int device_identity_id(uint8_t id[DEVICE_ID_SIZE]);

#endif
