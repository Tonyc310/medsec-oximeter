#include "event_log.h"
#include "log_mac.h"
#include "medsec_crypto.h"

#include <zephyr/ztest.h>

#include <stdint.h>

/* RFC 4231 test case 1's key, standing in for a device's log key: a published test value. */
static const uint8_t log_key[20] = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};

static security_record_t records[SECURITY_LOG_CAPACITY];
static uint8_t entries[SECURITY_LOG_CAPACITY * SECURITY_LOG_ENTRY_SIZE];

/* Reads the log back as the gateway would, and returns how many records it holds. */
static uint32_t read_back(void)
{
    const size_t length = event_log_read(entries, sizeof entries);

    zassert_equal(length % SECURITY_LOG_ENTRY_SIZE, 0u);
    for (size_t i = 0u; i < (length / SECURITY_LOG_ENTRY_SIZE); i++) {
        zassert_true(security_log_decode(&entries[i * SECURITY_LOG_ENTRY_SIZE],
                                         SECURITY_LOG_RECORD_SIZE, &records[i]));
    }
    return (uint32_t)(length / SECURITY_LOG_ENTRY_SIZE);
}

/* Starting again is what a reset does: everything comes back from flash. The simulated flash
 * lasts the whole run, so this checks the last four records, wherever numbering has got to. */
ZTEST(event_log, test_records_survive_a_restart)
{
    static const uint8_t limits[] = {95u, 50u, 0u, 120u, 0u};
    static const uint8_t central[] = {1u, 0xDDu, 0xCCu, 0xBBu, 0xAAu, 0x00u, 0xC0u};

    zassert_ok(event_log_start());
    zassert_ok(event_log_record(SECURITY_EVENT_CONNECTED, central, sizeof central));
    zassert_ok(event_log_record(SECURITY_EVENT_LIMITS_ACCEPTED, limits, sizeof limits));
    zassert_ok(event_log_start());

    const uint32_t count = read_back();
    const security_record_t *last = &records[count - 4u];

    zassert_true(count >= 4u);
    zassert_equal(last[0].event, SECURITY_EVENT_START);
    zassert_equal(last[1].event, SECURITY_EVENT_CONNECTED);
    zassert_mem_equal(last[1].detail, central, sizeof central);
    zassert_equal(last[2].event, SECURITY_EVENT_LIMITS_ACCEPTED);
    zassert_mem_equal(last[2].detail, limits, sizeof limits);
    zassert_equal(last[3].event, SECURITY_EVENT_START);
    zassert_equal(last[3].boot, last[0].boot + 1u);
    for (uint32_t i = 1u; i < 4u; i++) {
        zassert_equal(last[i].sequence, last[0].sequence + i);
    }
}

/* Checked as the backend will: with its own copy of the key, through PSA, not the signer's code. */
ZTEST(event_log, test_keeps_each_record_tagged_through_a_restart)
{
    psa_key_id_t verifier = PSA_KEY_ID_NULL;

    zassert_ok(event_log_start());
    zassert_ok(event_log_record(SECURITY_EVENT_SENSOR_STOPPED, NULL, 0u));
    zassert_ok(event_log_start());

    const uint32_t count = read_back();

    zassert_true(count >= 3u);
    zassert_true(
        medsec_mac_key_load(&verifier, log_key, sizeof log_key, PSA_KEY_USAGE_VERIFY_MESSAGE));
    for (uint32_t i = 0u; i < count; i++) {
        const uint8_t *entry = &entries[i * SECURITY_LOG_ENTRY_SIZE];

        zassert_equal(psa_mac_verify(verifier, MEDSEC_MAC, entry, SECURITY_LOG_RECORD_SIZE,
                                     &entry[SECURITY_LOG_RECORD_SIZE], SECURITY_LOG_TAG_SIZE),
                      PSA_SUCCESS, "record %u", i);
    }
    (void)psa_destroy_key(verifier);
}

/* Every record either test makes is tagged with the same key. */
static void *load_log_key(void)
{
    zassert_true(log_mac_init(log_key, sizeof log_key));
    return NULL;
}

ZTEST_SUITE(event_log, NULL, load_log_key, NULL, NULL, NULL);
