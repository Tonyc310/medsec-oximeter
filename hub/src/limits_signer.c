#include "limits_signer.h"

#include "medsec_crypto.h"

#include <psa/crypto.h>

static psa_key_id_t key_id = PSA_KEY_ID_NULL;

bool limits_signer_init(const uint8_t *key, size_t length)
{
    /* The hub only makes these tags, standing in for the backend that will. */
    return medsec_mac_key_load(&key_id, key, length, PSA_KEY_USAGE_SIGN_MESSAGE);
}

bool limits_signer_sign(const uint8_t challenge[LIMITS_AUTH_CHALLENGE_SIZE],
                        const alarm_limits_t *limits, uint8_t command[LIMITS_AUTH_COMMAND_SIZE])
{
    uint8_t message[LIMITS_AUTH_MESSAGE_SIZE];
    size_t tag_length;

    limits_auth_message(challenge, limits, message);
    alarm_limits_encode(limits, command);
    return psa_mac_compute(key_id, MEDSEC_MAC, message, sizeof message, &command[ALARM_LIMITS_SIZE],
                           LIMITS_AUTH_TAG_SIZE, &tag_length) == PSA_SUCCESS;
}
