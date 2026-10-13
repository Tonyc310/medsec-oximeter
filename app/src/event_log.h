#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include "security_log.h"

#include <stddef.h>
#include <stdint.h>

/** Loads the log from flash and records the start-up, with the reset cause. Call before Bluetooth
 * starts. Returns 0 or -errno. */
int event_log_start(void);

/** Records an event with up to SECURITY_LOG_DETAIL_SIZE bytes of detail, from any thread.
 * Returns 0 or -errno. */
int event_log_record(security_event_t event, const void *detail, size_t length);

/** Copies the log into `out`, oldest record first, and returns the number of bytes copied. */
size_t event_log_read(uint8_t *out, size_t size);

#endif
