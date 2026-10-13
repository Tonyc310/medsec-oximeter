#include "event_log.h"

#include <zephyr/ztest.h>

#include <stdint.h>

static security_record_t records[SECURITY_LOG_CAPACITY];

/* Reads the log back as the gateway would, and returns how many records it holds. */
static uint32_t read_back(void)
{
    static uint8_t encoded[SECURITY_LOG_CAPACITY * SECURITY_LOG_RECORD_SIZE];
    const size_t length = event_log_read(encoded, sizeof encoded);

    zassert_equal(length % SECURITY_LOG_RECORD_SIZE, 0u);
    for (size_t i = 0u; i < (length / SECURITY_LOG_RECORD_SIZE); i++) {
        zassert_true(security_log_decode(&encoded[i * SECURITY_LOG_RECORD_SIZE],
                                         SECURITY_LOG_RECORD_SIZE, &records[i]));
    }
    return (uint32_t)(length / SECURITY_LOG_RECORD_SIZE);
}

/* Starting again is what a reset does: everything comes back from flash. */
ZTEST(event_log, test_records_survive_a_restart)
{
    static const uint8_t limits[] = {95u, 50u, 0u, 120u, 0u};
    static const uint8_t central[] = {1u, 0xDDu, 0xCCu, 0xBBu, 0xAAu, 0x00u, 0xC0u};

    zassert_ok(event_log_start());
    zassert_ok(event_log_record(SECURITY_EVENT_CONNECTED, central, sizeof central));
    zassert_ok(event_log_record(SECURITY_EVENT_LIMITS_ACCEPTED, limits, sizeof limits));
    zassert_ok(event_log_start());

    zassert_equal(read_back(), 4u);
    zassert_equal(records[0].event, SECURITY_EVENT_START);
    zassert_equal(records[0].boot, 1u);
    zassert_equal(records[1].event, SECURITY_EVENT_CONNECTED);
    zassert_mem_equal(records[1].detail, central, sizeof central);
    zassert_equal(records[2].event, SECURITY_EVENT_LIMITS_ACCEPTED);
    zassert_mem_equal(records[2].detail, limits, sizeof limits);
    zassert_equal(records[3].event, SECURITY_EVENT_START);
    zassert_equal(records[3].boot, 2u);
    for (uint32_t i = 0u; i < 4u; i++) {
        zassert_equal(records[i].sequence, i + 1u);
    }
}

ZTEST_SUITE(event_log, NULL, NULL, NULL, NULL, NULL);
