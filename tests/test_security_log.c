#include "security_log.h"
#include "unity.h"

#include <stdint.h>

static security_log_t security;

void setUp(void)
{
    security_log_init(&security);
}

void tearDown(void)
{
}

/* Puts back a record as flash would hold it. */
static void restore(uint32_t sequence)
{
    const security_record_t record = {.sequence = sequence, .event = SECURITY_EVENT_START};
    uint8_t stored[SECURITY_LOG_RECORD_SIZE];

    security_log_encode(&record, stored);
    security_log_restore(&security, stored, sizeof stored);
}

/* Event codes are part of the format, so they're pinned here as numbers. */
static void test_encodes_a_record_as_the_bytes_the_gateway_reads(void)
{
    const security_record_t record = {
        .sequence = 0x01020304u,
        .boot = 0x0506u,
        .uptime_ms = 0x0708090Au,
        .event = SECURITY_EVENT_LIMITS_ACCEPTED,
        .detail = {95u, 40u, 0u, 130u, 0u},
    };
    static const uint8_t expected[SECURITY_LOG_RECORD_SIZE] = {
        0x04, 0x03, 0x02, 0x01, /* sequence */
        0x06, 0x05,             /* boot */
        0x0A, 0x09, 0x08, 0x07, /* uptime_ms */
        0x04,                   /* limits accepted */
        95,   40,   0,    130,  0, 0, 0, 0, 0,
    };
    uint8_t encoded[SECURITY_LOG_RECORD_SIZE];

    security_log_encode(&record, encoded);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, encoded, SECURITY_LOG_RECORD_SIZE);
}

static void test_numbers_records_from_1_on_a_new_device(void)
{
    security_record_t record = {0};

    TEST_ASSERT_EQUAL_UINT32(0u, security_log_next(&security, &record));
    TEST_ASSERT_EQUAL_UINT32(1u, record.sequence);
    TEST_ASSERT_EQUAL_UINT32(1u, security_log_next(&security, &record));
    TEST_ASSERT_EQUAL_UINT32(2u, record.sequence);
}

/* Flash keeps records in slots, not in order. */
static void test_continues_after_the_highest_record_found_in_flash(void)
{
    security_record_t record = {0};

    restore(5u);
    restore(9u);
    restore(7u);
    (void)security_log_next(&security, &record);
    TEST_ASSERT_EQUAL_UINT32(10u, record.sequence);
}

static void test_replaces_the_oldest_record_once_full(void)
{
    security_record_t record = {0};

    restore(SECURITY_LOG_CAPACITY); /* records 1 to 64 fill slots 0 to 63 */
    TEST_ASSERT_EQUAL_UINT32(0u, security_log_next(&security, &record));
    TEST_ASSERT_EQUAL_UINT32(SECURITY_LOG_CAPACITY + 1u, record.sequence);
}

/* Whatever corrupted them, they mustn't move the numbering. */
static void test_ignores_malformed_records_found_in_flash(void)
{
    static const struct {
        const char *name;
        size_t length;
        uint32_t sequence;
        uint8_t event;
    } cases[] = {
        {"too short", SECURITY_LOG_RECORD_SIZE - 1u, 50u, SECURITY_EVENT_START},
        {"too long", SECURITY_LOG_RECORD_SIZE + 1u, 50u, SECURITY_EVENT_START},
        {"unknown event", SECURITY_LOG_RECORD_SIZE, 50u, (uint8_t)(SECURITY_EVENT_LAST + 1u)},
        {"no event", SECURITY_LOG_RECORD_SIZE, 50u, 0u},
        {"numbered 0", SECURITY_LOG_RECORD_SIZE, 0u, SECURITY_EVENT_START},
    };
    security_record_t record = {0};

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        const security_record_t malformed = {.sequence = cases[i].sequence,
                                             .event = cases[i].event};
        uint8_t stored[SECURITY_LOG_RECORD_SIZE + 1u] = {0};

        security_log_encode(&malformed, stored);
        TEST_ASSERT_FALSE_MESSAGE(security_log_decode(stored, cases[i].length, &record),
                                  cases[i].name);
        security_log_restore(&security, stored, cases[i].length);
    }
    (void)security_log_next(&security, &record);
    TEST_ASSERT_EQUAL_UINT32(1u, record.sequence);
}

/* The gateway's view of a record, field by field. */
static void test_decodes_a_record_back_to_its_fields(void)
{
    const security_record_t original = {
        .sequence = 0x01020304u,
        .boot = 0x0506u,
        .uptime_ms = 0x0708090Au,
        .event = SECURITY_EVENT_CONNECTED,
        .detail = {1u, 0xDDu, 0xCCu, 0xBBu, 0xAAu, 0x00u, 0xC0u},
    };
    uint8_t stored[SECURITY_LOG_RECORD_SIZE];
    security_record_t decoded = {0};

    security_log_encode(&original, stored);
    TEST_ASSERT_TRUE(security_log_decode(stored, sizeof stored, &decoded));
    TEST_ASSERT_EQUAL_UINT32(original.sequence, decoded.sequence);
    TEST_ASSERT_EQUAL_UINT16(original.boot, decoded.boot);
    TEST_ASSERT_EQUAL_UINT32(original.uptime_ms, decoded.uptime_ms);
    TEST_ASSERT_EQUAL_UINT8(original.event, decoded.event);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(original.detail, decoded.detail, SECURITY_LOG_DETAIL_SIZE);
}

/* The newest event code is a known one. */
static void test_restores_a_record_of_the_newest_event(void)
{
    const security_record_t newest = {.sequence = 4u, .event = SECURITY_EVENT_UPDATE_PENDING};
    uint8_t stored[SECURITY_LOG_RECORD_SIZE];
    security_record_t record = {0};

    TEST_ASSERT_EQUAL_UINT8(12u, SECURITY_EVENT_UPDATE_PENDING);
    security_log_encode(&newest, stored);
    security_log_restore(&security, stored, sizeof stored);
    (void)security_log_next(&security, &record);
    TEST_ASSERT_EQUAL_UINT32(5u, record.sequence);
}

static void test_reads_the_records_back_oldest_first(void)
{
    restore(3u);
    TEST_ASSERT_EQUAL_UINT32(3u, security_log_count(&security));
    TEST_ASSERT_EQUAL_UINT32(0u, security_log_slot(&security, 0u));
    TEST_ASSERT_EQUAL_UINT32(2u, security_log_slot(&security, 2u));

    /* Records 7 to 70 remain: 7 is in slot 6, and 70, the newest, in slot 5. */
    restore(70u);
    TEST_ASSERT_EQUAL_UINT32(SECURITY_LOG_CAPACITY, security_log_count(&security));
    TEST_ASSERT_EQUAL_UINT32(6u, security_log_slot(&security, 0u));
    TEST_ASSERT_EQUAL_UINT32(5u, security_log_slot(&security, SECURITY_LOG_CAPACITY - 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encodes_a_record_as_the_bytes_the_gateway_reads);
    RUN_TEST(test_decodes_a_record_back_to_its_fields);
    RUN_TEST(test_numbers_records_from_1_on_a_new_device);
    RUN_TEST(test_continues_after_the_highest_record_found_in_flash);
    RUN_TEST(test_replaces_the_oldest_record_once_full);
    RUN_TEST(test_ignores_malformed_records_found_in_flash);
    RUN_TEST(test_restores_a_record_of_the_newest_event);
    RUN_TEST(test_reads_the_records_back_oldest_first);
    return UNITY_END();
}
