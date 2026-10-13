#include "limits_auth.h"

#include <string.h>

static void renew(limits_auth_t *auth)
{
    auth->fresh = auth->crypto->random(auth->challenge, sizeof auth->challenge);
}

void limits_auth_init(limits_auth_t *auth, const limits_auth_crypto_t *crypto)
{
    auth->crypto = crypto;
    renew(auth);
}

void limits_auth_new_challenge(limits_auth_t *auth)
{
    renew(auth);
}

const uint8_t *limits_auth_challenge(const limits_auth_t *auth)
{
    return auth->challenge;
}

void limits_auth_message(const uint8_t challenge[LIMITS_AUTH_CHALLENGE_SIZE],
                         const alarm_limits_t *limits, uint8_t message[LIMITS_AUTH_MESSAGE_SIZE])
{
    memcpy(message, challenge, LIMITS_AUTH_CHALLENGE_SIZE);
    alarm_limits_encode(limits, &message[LIMITS_AUTH_CHALLENGE_SIZE]);
}

limits_auth_result_t limits_auth_apply(limits_auth_t *auth, const uint8_t *command, size_t length,
                                       alarm_limits_t *limits)
{
    alarm_limits_t requested;
    limits_auth_result_t result;

    if (!auth->fresh) {
        result = LIMITS_AUTH_NO_CHALLENGE;
    } else if (length != LIMITS_AUTH_COMMAND_SIZE) {
        result = LIMITS_AUTH_WRONG_LENGTH;
    } else if (!alarm_limits_decode(command, ALARM_LIMITS_SIZE, &requested)) {
        result = LIMITS_AUTH_INVALID_LIMITS;
    } else {
        uint8_t message[LIMITS_AUTH_MESSAGE_SIZE];

        /* Encoding the decoded limits gives back the bytes that were sent, so the tag is checked
         * over exactly what arrived. */
        limits_auth_message(auth->challenge, &requested, message);
        result = auth->crypto->verify(message, sizeof message, &command[ALARM_LIMITS_SIZE])
                     ? LIMITS_AUTH_ACCEPTED
                     : LIMITS_AUTH_BAD_TAG;
    }

    /* Every attempt uses up the challenge, accepted or not. */
    renew(auth);
    if (result == LIMITS_AUTH_ACCEPTED) {
        *limits = requested;
    }
    return result;
}
