#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include "security_log.h"

#include <stddef.h>
#include <stdint.h>
#include <zephyr/dfu/mcuboot.h>

/** Loads the log from flash and records the start-up, with the reset cause and the version that
 * runs. Call before Bluetooth starts. Returns 0 or -errno. */
int event_log_start(const struct mcuboot_img_sem_ver *running);

/** Records an event with up to SECURITY_LOG_DETAIL_SIZE bytes of detail, from any thread.
 * Returns 0 or -errno. */
int event_log_record(security_event_t event, const void *detail, size_t length);

/** The answer to a backend's `challenge`: the newest record's sequence number (LE32), then its tag
 * under the log key. Returns 0, or -EACCES without a log key. */
int event_log_answer(const uint8_t challenge[SECURITY_LOG_CHALLENGE_SIZE],
                     uint8_t answer[SECURITY_LOG_HEAD_ANSWER_SIZE]);

/** Copies the log into `out`, oldest entry first, and returns the number of bytes copied. Each
 * entry is a record and its tag: SECURITY_LOG_ENTRY_SIZE bytes. */
size_t event_log_read(uint8_t *out, size_t size);

#endif
