#ifndef LIMITS_CRYPTO_H
#define LIMITS_CRYPTO_H

#include "limits_auth.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Loads the limits key, replacing any loaded before. Returns false, with no key loaded, if it
 * can't. */
bool limits_crypto_init(const uint8_t *key, size_t length);

/** HMAC-SHA256 cut to LIMITS_AUTH_TAG_SIZE bytes, and random challenges, through PSA Crypto. */
extern const limits_auth_crypto_t limits_crypto;

#endif
