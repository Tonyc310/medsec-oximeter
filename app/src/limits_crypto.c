#include "limits_crypto.h"

#include "medsec_crypto.h"

#include <psa/crypto.h>

static psa_key_id_t key_id = PSA_KEY_ID_NULL;

bool limits_crypto_init(const uint8_t *key, size_t length)
{
    /* The oximeter only checks these tags; it never makes them. */
    return medsec_mac_key_load(&key_id, key, length, PSA_KEY_USAGE_VERIFY_MESSAGE);
}

static bool random_bytes(uint8_t *out, size_t length)
{
    return psa_generate_random(out, length) == PSA_SUCCESS;
}

/* psa_mac_verify compares in constant time, so a tag can't be guessed a byte at a time. */
static bool verify(const uint8_t *message, size_t length, const uint8_t *tag)
{
    return psa_mac_verify(key_id, MEDSEC_MAC, message, length, tag, LIMITS_AUTH_TAG_SIZE) ==
           PSA_SUCCESS;
}

const limits_auth_crypto_t limits_crypto = {.random = random_bytes, .verify = verify};
