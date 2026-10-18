#ifndef SECURITY_LOG_H
#define SECURITY_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Security events, as the gateway reads them: 20 bytes each, little-endian. The oximeter has no
 * clock, so a record is timed by the boot it happened in and the milliseconds since that boot;
 * the gateway, which knows the date, stamps records when it reads them. Sequence numbers never
 * repeat, so a gap shows a lost record. */
#define SECURITY_LOG_RECORD_SIZE 20u
#define SECURITY_LOG_DETAIL_SIZE 9u
/* Each record is kept and sent with a tag: a MAC under the device's log key, over its 20 bytes. */
#define SECURITY_LOG_TAG_SIZE 16u
#define SECURITY_LOG_ENTRY_SIZE (SECURITY_LOG_RECORD_SIZE + SECURITY_LOG_TAG_SIZE)
/* The newest records kept; older ones are overwritten. */
#define SECURITY_LOG_CAPACITY 64u

/* The codes are part of the format: never renumber them. */
typedef enum {
    SECURITY_EVENT_START = 1,           /* detail: the reset cause, as hwinfo flags (LE32) */
    SECURITY_EVENT_CONNECTED = 2,       /* detail: the central's address type, then address */
    SECURITY_EVENT_DISCONNECTED = 3,    /* detail: the HCI reason */
    SECURITY_EVENT_LIMITS_ACCEPTED = 4, /* detail: the limits, as alarm_limits_encode sends them */
    SECURITY_EVENT_LIMITS_REFUSED = 5,  /* detail: a limits_auth_result_t */
    SECURITY_EVENT_LIMITS_UNSAVED =
        6, /* detail: the limits, refused because they couldn't be saved */
    SECURITY_EVENT_SENSOR_STOPPED = 7,
    SECURITY_EVENT_SENSOR_RESUMED = 8,
    SECURITY_EVENT_NO_DEVICE_KEY = 9, /* at start-up: none provisioned, so no limits change works */
    SECURITY_EVENT_PAIRED = 10,       /* detail: the central's address type, then address */
    SECURITY_EVENT_PAIRING_REFUSED = 11, /* detail: Zephyr's bt_security_err, then the address */
} security_event_t;

#define SECURITY_EVENT_LAST SECURITY_EVENT_PAIRING_REFUSED

typedef struct {
    uint32_t sequence; /* from 1 */
    uint16_t boot;
    uint32_t uptime_ms;
    uint8_t event; /* a security_event_t */
    uint8_t detail[SECURITY_LOG_DETAIL_SIZE];
} security_record_t;

/* Where the numbering has got to. The members are private. */
typedef struct {
    uint32_t last_sequence; /* 0 while the log is empty */
} security_log_t;

/** Starts an empty log. */
void security_log_init(security_log_t *security);

/** Notes a record found in flash at start-up, so numbering carries on after it. Records that
 * aren't well formed are ignored. */
void security_log_restore(security_log_t *security, const uint8_t *data, size_t length);

/** Numbers the next record and returns the slot it's kept in: the oldest once the log is full. */
uint32_t security_log_next(security_log_t *security, security_record_t *record);

/** How many records the log holds: at most SECURITY_LOG_CAPACITY. */
uint32_t security_log_count(const security_log_t *security);

/** The slot of the `index`th record, counting from the oldest. */
uint32_t security_log_slot(const security_log_t *security, uint32_t index);

/** Decodes a record. Returns false, and leaves `record` untouched, for anything but a 20-byte
 * record of a known event, numbered from 1. */
bool security_log_decode(const uint8_t *data, size_t length, security_record_t *record);

void security_log_encode(const security_record_t *record, uint8_t out[SECURITY_LOG_RECORD_SIZE]);

#endif
