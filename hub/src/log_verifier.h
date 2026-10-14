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

#endif
