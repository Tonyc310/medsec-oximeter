#ifndef LOG_MAC_H
#define LOG_MAC_H

#include "security_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Loads the device's log key, replacing any loaded before. Returns false, with no key loaded, if
 * it can't. */
bool log_mac_init(const uint8_t *key, size_t length);

/** Tags `data` with MEDSEC_MAC under the log key. False, and no tag, without a key. */
bool log_mac_tag(const uint8_t *data, size_t length, uint8_t tag[SECURITY_LOG_TAG_SIZE]);

#endif
