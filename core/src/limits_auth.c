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

bool limits_auth_apply(limits_auth_t *auth, const uint8_t *command, size_t length,
                       alarm_limits_t *limits)
{
    uint8_t message[LIMITS_AUTH_CHALLENGE_SIZE + ALARM_LIMITS_SIZE];
    bool authentic = false;

    if (auth->fresh && (length == LIMITS_AUTH_COMMAND_SIZE)) {
        memcpy(message, auth->challenge, LIMITS_AUTH_CHALLENGE_SIZE);
        memcpy(&message[LIMITS_AUTH_CHALLENGE_SIZE], command, ALARM_LIMITS_SIZE);
        authentic = auth->crypto->verify(message, sizeof message, &command[ALARM_LIMITS_SIZE]);
    }

    /* Every attempt uses up the challenge, accepted or not. */
    renew(auth);
    return authentic && alarm_limits_decode(command, ALARM_LIMITS_SIZE, limits);
}
