#include "security_log.h"

#include <string.h>

/* Where each field sits in an encoded record. */
#define SEQUENCE_AT 0u
#define BOOT_AT 4u
#define UPTIME_AT 6u
#define EVENT_AT 10u
#define DETAIL_AT 11u

static void put_le16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *out, uint32_t value)
{
    put_le16(out, (uint16_t)(value & 0xFFFFu));
    put_le16(&out[2], (uint16_t)(value >> 16));
}

static uint32_t get_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint16_t get_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8));
}

void security_log_init(security_log_t *security)
{
    security->last_sequence = 0u;
}

void security_log_encode(const security_record_t *record, uint8_t out[SECURITY_LOG_RECORD_SIZE])
{
    put_le32(&out[SEQUENCE_AT], record->sequence);
    put_le16(&out[BOOT_AT], record->boot);
    put_le32(&out[UPTIME_AT], record->uptime_ms);
    out[EVENT_AT] = record->event;
    memcpy(&out[DETAIL_AT], record->detail, SECURITY_LOG_DETAIL_SIZE);
}

uint32_t security_log_next(security_log_t *security, security_record_t *record)
{
    security->last_sequence++;
    record->sequence = security->last_sequence;
    return (record->sequence - 1u) % SECURITY_LOG_CAPACITY;
}

bool security_log_decode(const uint8_t *data, size_t length, security_record_t *record)
{
    /* Numbering starts at 1, so a record numbered 0 is corrupt too. */
    if ((length != SECURITY_LOG_RECORD_SIZE) || (data[EVENT_AT] < (uint8_t)SECURITY_EVENT_START) ||
        (data[EVENT_AT] > (uint8_t)SECURITY_EVENT_LAST) || (get_le32(&data[SEQUENCE_AT]) == 0u)) {
        return false;
    }
    record->sequence = get_le32(&data[SEQUENCE_AT]);
    record->boot = get_le16(&data[BOOT_AT]);
    record->uptime_ms = get_le32(&data[UPTIME_AT]);
    record->event = data[EVENT_AT];
    memcpy(record->detail, &data[DETAIL_AT], SECURITY_LOG_DETAIL_SIZE);
    return true;
}

void security_log_restore(security_log_t *security, const uint8_t *data, size_t length)
{
    security_record_t record;

    if (security_log_decode(data, length, &record) && (record.sequence > security->last_sequence)) {
        security->last_sequence = record.sequence;
    }
}

uint32_t security_log_last(const security_log_t *security)
{
    return security->last_sequence;
}

uint32_t security_log_count(const security_log_t *security)
{
    return (security->last_sequence < SECURITY_LOG_CAPACITY) ? security->last_sequence
                                                             : SECURITY_LOG_CAPACITY;
}

uint32_t security_log_slot(const security_log_t *security, uint32_t index)
{
    const uint32_t oldest = security->last_sequence - security_log_count(security) + 1u;

    return (oldest + index - 1u) % SECURITY_LOG_CAPACITY;
}
