#include "limits_signer.h"

#include "medsec_crypto.h"

#include <psa/crypto.h>

static psa_key_id_t key_id = PSA_KEY_ID_NULL;

bool limits_signer_init(const uint8_t *key, size_t length)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    /* The old key goes first, so a failure below leaves no key loaded rather than the old one. */
    (void)psa_destroy_key(key_id);
    key_id = PSA_KEY_ID_NULL;
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    /* The hub only makes tags, standing in for the backend that will. */
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, MEDSEC_LIMITS_MAC);
    return psa_import_key(&attributes, key, length, &key_id) == PSA_SUCCESS;
}

bool limits_signer_sign(const uint8_t challenge[LIMITS_AUTH_CHALLENGE_SIZE],
                        const alarm_limits_t *limits, uint8_t command[LIMITS_AUTH_COMMAND_SIZE])
{
    uint8_t message[LIMITS_AUTH_MESSAGE_SIZE];
    size_t tag_length;

    limits_auth_message(challenge, limits, message);
    alarm_limits_encode(limits, command);
    return psa_mac_compute(key_id, MEDSEC_LIMITS_MAC, message, sizeof message,
                           &command[ALARM_LIMITS_SIZE], LIMITS_AUTH_TAG_SIZE,
                           &tag_length) == PSA_SUCCESS;
}
