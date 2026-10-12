#ifndef LIMITS_AUTH_H
#define LIMITS_AUTH_H

#include "alarm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An alarm-limits command is the limits, then a tag over the oximeter's current challenge
 * followed by the limits. Only a holder of the limits key can make the tag, and each challenge is
 * good for one attempt, so a command can be neither forged nor replayed. */
#define LIMITS_AUTH_CHALLENGE_SIZE 16u
#define LIMITS_AUTH_TAG_SIZE 16u
#define LIMITS_AUTH_COMMAND_SIZE (ALARM_LIMITS_SIZE + LIMITS_AUTH_TAG_SIZE)
#define LIMITS_AUTH_MESSAGE_SIZE (LIMITS_AUTH_CHALLENGE_SIZE + ALARM_LIMITS_SIZE)

typedef struct {
    /** Fills `out` from a cryptographically secure source; false if it can't. */
    bool (*random)(uint8_t *out, size_t length);
    /** True if `tag` (LIMITS_AUTH_TAG_SIZE bytes) is `message`'s MAC under the limits key. */
    bool (*verify)(const uint8_t *message, size_t length, const uint8_t *tag);
} limits_auth_crypto_t;

/* The current challenge and the crypto that checks commands against it. The members are private. */
typedef struct {
    const limits_auth_crypto_t *crypto;
    uint8_t challenge[LIMITS_AUTH_CHALLENGE_SIZE];
    bool fresh; /* false when the random source failed: the old challenge mustn't be reused */
} limits_auth_t;

/**
 * The bytes a tag covers: the challenge, then the limits as alarm_limits_encode sends them. The
 * oximeter and whatever signs its commands both build them here, so they can't disagree.
 */
void limits_auth_message(const uint8_t challenge[LIMITS_AUTH_CHALLENGE_SIZE],
                         const alarm_limits_t *limits, uint8_t message[LIMITS_AUTH_MESSAGE_SIZE]);

/** Issues the first challenge. `crypto` must outlive `auth`. */
void limits_auth_init(limits_auth_t *auth, const limits_auth_crypto_t *crypto);

/** Replaces the challenge, so commands made for the old one are refused. */
void limits_auth_new_challenge(limits_auth_t *auth);

/** The challenge the next command must be signed over (LIMITS_AUTH_CHALLENGE_SIZE bytes). */
const uint8_t *limits_auth_challenge(const limits_auth_t *auth);

/**
 * Checks a command and, if it's authentic and its limits are ones alarm_limits_decode accepts,
 * stores them in `limits`. Returns whether it did; on false, `limits` is untouched. Every call
 * replaces the challenge, whatever the outcome.
 */
bool limits_auth_apply(limits_auth_t *auth, const uint8_t *command, size_t length,
                       alarm_limits_t *limits);

#endif
