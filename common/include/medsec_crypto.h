#ifndef MEDSEC_CRYPTO_H
#define MEDSEC_CRYPTO_H

#include "limits_auth.h"

#include <psa/crypto.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The MAC on alarm-limit changes and on security log records: HMAC-SHA256 cut to 128 bits. Every
 * key that makes or checks one takes it from here. */
#define MEDSEC_MAC PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), LIMITS_AUTH_TAG_SIZE)

/** Loads a MEDSEC_MAC key allowed only `usage`, replacing the one in `*id`. Returns false, with no
 * key loaded, if it can't. */
bool medsec_mac_key_load(psa_key_id_t *id, const uint8_t *key, size_t length,
                         psa_key_usage_t usage);

#endif
