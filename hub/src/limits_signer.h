#ifndef LIMITS_SIGNER_H
#define LIMITS_SIGNER_H

#include "alarm.h"
#include "limits_auth.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Loads the key the hub signs limit changes with, replacing any loaded before. Returns false, with
 * no key loaded, if it can't. */
bool limits_signer_init(const uint8_t *key, size_t length);

/** Makes the command that sets `limits`, signed over the oximeter's `challenge`. False if it can't.
 */
bool limits_signer_sign(const uint8_t challenge[LIMITS_AUTH_CHALLENGE_SIZE],
                        const alarm_limits_t *limits, uint8_t command[LIMITS_AUTH_COMMAND_SIZE]);

#endif
