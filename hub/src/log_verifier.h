#ifndef LOG_VERIFIER_H
#define LOG_VERIFIER_H

#include "security_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Loads the log key of the oximeter being read, replacing any loaded before. Returns false, with
 * no key loaded, if it can't. */
bool log_verifier_init(const uint8_t *key, size_t length);

/** True if `entry`'s tag is the MAC of its record under the loaded log key. */
bool log_verifier_check(const uint8_t entry[SECURITY_LOG_ENTRY_SIZE]);

/** True if `answer` is the oximeter's to `challenge`, tagged under the loaded log key; then
 * `newest` is its newest record's sequence number. */
bool log_verifier_check_head(const uint8_t challenge[SECURITY_LOG_CHALLENGE_SIZE],
                             const uint8_t answer[SECURITY_LOG_HEAD_ANSWER_SIZE], uint32_t *newest);

#endif
