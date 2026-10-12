#include "limits_crypto.h"

#include <psa/crypto.h>

#define ALGORITHM PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), LIMITS_AUTH_TAG_SIZE)

static psa_key_id_t key_id = PSA_KEY_ID_NULL;

bool limits_crypto_init(const uint8_t *key, size_t length)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    /* The old key goes first, so a failure below leaves no key loaded rather than the old one.
     * Destroying when there's no key is harmless. */
    (void)psa_destroy_key(key_id);
    key_id = PSA_KEY_ID_NULL;
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    /* The oximeter only checks tags; it never makes them. */
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attributes, ALGORITHM);
    return psa_import_key(&attributes, key, length, &key_id) == PSA_SUCCESS;
}

static bool random_bytes(uint8_t *out, size_t length)
{
    return psa_generate_random(out, length) == PSA_SUCCESS;
}

/* psa_mac_verify compares in constant time, so a tag can't be guessed a byte at a time. */
static bool verify(const uint8_t *message, size_t length, const uint8_t *tag)
{
    return psa_mac_verify(key_id, ALGORITHM, message, length, tag, LIMITS_AUTH_TAG_SIZE) ==
           PSA_SUCCESS;
}

const limits_auth_crypto_t limits_crypto = {.random = random_bytes, .verify = verify};
