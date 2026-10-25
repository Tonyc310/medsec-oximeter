#include "device_keys.h"
#include "limits_auth.h"
#include "limits_crypto.h"
#include "limits_signer.h"
#include "log_mac.h"
#include "log_verifier.h"

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <stdint.h>
#include <string.h>

/* RFC 4231 test case 1's key: a published test value, not a secret. */
static const uint8_t shared_key[20] = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                       0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
static const alarm_limits_t raised = {
    .spo2_low_percent = 95u, .pulse_low_bpm = 40u, .pulse_high_bpm = 130u};
static limits_auth_t oximeter;

ZTEST(limits_signer, test_signs_commands_the_oximeter_accepts)
{
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];
    alarm_limits_t limits = alarm_default_limits();

    zassert_true(limits_signer_sign(limits_auth_challenge(&oximeter), &raised, command));
    zassert_equal(limits_auth_apply(&oximeter, command, sizeof command, &limits),
                  LIMITS_AUTH_ACCEPTED);
    zassert_equal(limits.spo2_low_percent, 95u);
    zassert_equal(limits.pulse_low_bpm, 40u);
    zassert_equal(limits.pulse_high_bpm, 130u);
}

/* A gateway that wasn't given this oximeter's key. RFC 4231 test case 2's key. */
ZTEST(limits_signer, test_commands_signed_with_another_key_are_refused)
{
    static const uint8_t other_key[4] = {'J', 'e', 'f', 'e'};
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];
    alarm_limits_t limits = alarm_default_limits();

    zassert_true(limits_signer_init(other_key, sizeof other_key));
    zassert_true(limits_signer_sign(limits_auth_challenge(&oximeter), &raised, command));
    zassert_equal(limits_auth_apply(&oximeter, command, sizeof command, &limits),
                  LIMITS_AUTH_BAD_TAG);
}

/* RFC 5869's test cases 1, with a salt and info, and 3, with neither, as the hub derives keys. */
ZTEST(limits_signer, test_derives_rfc5869_outputs)
{
    static const uint8_t ikm[22] = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
    static const uint8_t salt[13] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                     0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
    static const uint8_t info[10] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
    static const struct {
        const char *name;
        size_t salt_length;
        size_t info_length;
        uint8_t okm[42];
    } cases[] = {
        {"test case 1",
         sizeof salt,
         sizeof info,
         {0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36,
          0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56,
          0xec, 0xc4, 0xc5, 0xbf, 0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18, 0x58, 0x65}},
        {"test case 3", 0u, 0u, {0x8d, 0xa4, 0xe7, 0x75, 0xa5, 0x63, 0xc1, 0x8f, 0x71, 0x5f, 0x80,
                                 0x2a, 0x06, 0x3c, 0x5a, 0x31, 0xb8, 0xa1, 0x1f, 0x5c, 0x5e, 0xe1,
                                 0x87, 0x9e, 0xc3, 0x45, 0x4e, 0x5f, 0x3c, 0x73, 0x8d, 0x2d, 0x9d,
                                 0x20, 0x13, 0x95, 0xfa, 0xa4, 0xb6, 0x1a, 0x96, 0xc8}},
    };

    for (size_t i = 0u; i < ARRAY_SIZE(cases); i++) {
        uint8_t okm[42];

        zassert_true(device_keys_hkdf(ikm, sizeof ikm, salt, cases[i].salt_length, info,
                                      cases[i].info_length, okm, sizeof okm),
                     "%s", cases[i].name);
        zassert_mem_equal(okm, cases[i].okm, sizeof okm, "%s", cases[i].name);
    }
}

/* The same device always gets the same key: the backend re-derives what the factory provisioned. */
ZTEST(limits_signer, test_gives_each_device_its_own_key)
{
    static const uint8_t first[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    static const uint8_t second[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xee};
    uint8_t first_key[DEVICE_KEY_SIZE];
    uint8_t second_key[DEVICE_KEY_SIZE];
    uint8_t first_again[DEVICE_KEY_SIZE];

    zassert_true(
        device_keys_derive(shared_key, sizeof shared_key, DEVICE_KEY_LIMITS, first, first_key));
    zassert_true(
        device_keys_derive(shared_key, sizeof shared_key, DEVICE_KEY_LIMITS, second, second_key));
    zassert_true(
        device_keys_derive(shared_key, sizeof shared_key, DEVICE_KEY_LIMITS, first, first_again));
    zassert_true(memcmp(first_key, second_key, sizeof first_key) != 0);
    zassert_mem_equal(first_key, first_again, sizeof first_key);
}

/* One key per job: the log key and the limits key of the same device differ. */
ZTEST(limits_signer, test_derives_a_different_key_for_the_log)
{
    static const uint8_t id[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    uint8_t limits_key[DEVICE_KEY_SIZE];
    uint8_t log_key[DEVICE_KEY_SIZE];

    zassert_true(
        device_keys_derive(shared_key, sizeof shared_key, DEVICE_KEY_LIMITS, id, limits_key));
    zassert_true(device_keys_derive(shared_key, sizeof shared_key, DEVICE_KEY_LOG, id, log_key));
    zassert_true(memcmp(limits_key, log_key, sizeof log_key) != 0);
}

/* As in the field: the factory gave the oximeter the key derived for its ID, and the backend
 * derives it again to sign. A key derived for any other ID is refused. */
ZTEST(limits_signer, test_signs_with_the_key_derived_for_that_device)
{
    static const uint8_t oximeter_id[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67,
                                                        0x89, 0xab, 0xcd, 0xef};
    static const uint8_t other_id[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67,
                                                     0x89, 0xab, 0xcd, 0xee};
    uint8_t key[DEVICE_KEY_SIZE];
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];
    alarm_limits_t limits = alarm_default_limits();

    zassert_true(
        device_keys_derive(shared_key, sizeof shared_key, DEVICE_KEY_LIMITS, oximeter_id, key));
    zassert_true(limits_crypto_init(key, sizeof key));
    limits_auth_init(&oximeter, &limits_crypto);

    zassert_true(limits_signer_init(key, sizeof key));
    zassert_true(limits_signer_sign(limits_auth_challenge(&oximeter), &raised, command));
    zassert_equal(limits_auth_apply(&oximeter, command, sizeof command, &limits),
                  LIMITS_AUTH_ACCEPTED);

    zassert_true(
        device_keys_derive(shared_key, sizeof shared_key, DEVICE_KEY_LIMITS, other_id, key));
    zassert_true(limits_signer_init(key, sizeof key));
    zassert_true(limits_signer_sign(limits_auth_challenge(&oximeter), &raised, command));
    zassert_equal(limits_auth_apply(&oximeter, command, sizeof command, &limits),
                  LIMITS_AUTH_BAD_TAG);
}

static const uint8_t oximeter_id[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
static const uint8_t other_id[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xee};

/* A log entry as the oximeter with `id` makes it: a record, tagged with that device's log key. */
static void tag_as_oximeter(const uint8_t id[DEVICE_ID_SIZE],
                            uint8_t entry[SECURITY_LOG_ENTRY_SIZE])
{
    static const security_record_t record = {.sequence = 7u,
                                             .boot = 2u,
                                             .uptime_ms = 5486u,
                                             .event = SECURITY_EVENT_LIMITS_ACCEPTED,
                                             .detail = {95u, 50u, 0u, 120u, 0u}};
    uint8_t key[DEVICE_KEY_SIZE];

    zassert_true(device_keys_derive(shared_key, sizeof shared_key, DEVICE_KEY_LOG, id, key));
    zassert_true(log_mac_init(key, sizeof key));
    security_log_encode(&record, entry);
    zassert_true(log_mac_tag(entry, SECURITY_LOG_RECORD_SIZE, &entry[SECURITY_LOG_RECORD_SIZE]));
}

/* The backend checks with the log key it derives for the oximeter it's reading. */
static void check_as_backend_for(const uint8_t id[DEVICE_ID_SIZE])
{
    uint8_t key[DEVICE_KEY_SIZE];

    zassert_true(device_keys_derive(shared_key, sizeof shared_key, DEVICE_KEY_LOG, id, key));
    zassert_true(log_verifier_init(key, sizeof key));
}

ZTEST(limits_signer, test_verifies_a_record_tagged_with_the_devices_log_key)
{
    uint8_t entry[SECURITY_LOG_ENTRY_SIZE];

    tag_as_oximeter(oximeter_id, entry);
    check_as_backend_for(oximeter_id);
    zassert_true(log_verifier_check(entry));
}

/* Whatever relays the log can't change what it says. */
ZTEST(limits_signer, test_refuses_a_record_with_one_byte_changed)
{
    uint8_t entry[SECURITY_LOG_ENTRY_SIZE];

    tag_as_oximeter(oximeter_id, entry);
    entry[11] ^= 0x01u; /* the first detail byte: the SpO2 limit */
    check_as_backend_for(oximeter_id);
    zassert_false(log_verifier_check(entry));
}

ZTEST(limits_signer, test_refuses_a_record_tagged_with_another_devices_key)
{
    uint8_t entry[SECURITY_LOG_ENTRY_SIZE];

    tag_as_oximeter(other_id, entry);
    check_as_backend_for(oximeter_id);
    zassert_false(log_verifier_check(entry));
}

static const uint8_t challenge[SECURITY_LOG_CHALLENGE_SIZE] = {
    0x3c, 0x51, 0x9e, 0x07, 0xd2, 0x64, 0xab, 0x18, 0xf5, 0x2e, 0x80, 0x4d, 0x96, 0x0b, 0x73, 0xc9};

/* The answer the oximeter with `id` gives to `asked`: its newest record's sequence number,
 * tagged with that device's log key. */
static void answer_as_oximeter(const uint8_t id[DEVICE_ID_SIZE],
                               const uint8_t asked[SECURITY_LOG_CHALLENGE_SIZE], uint32_t newest,
                               uint8_t answer[SECURITY_LOG_HEAD_ANSWER_SIZE])
{
    uint8_t key[DEVICE_KEY_SIZE];
    uint8_t message[SECURITY_LOG_HEAD_MESSAGE_SIZE];

    zassert_true(device_keys_derive(shared_key, sizeof shared_key, DEVICE_KEY_LOG, id, key));
    zassert_true(log_mac_init(key, sizeof key));
    security_log_head_message(asked, newest, message);
    sys_put_le32(newest, answer);
    zassert_true(log_mac_tag(message, sizeof message, &answer[sizeof newest]));
}

ZTEST(limits_signer, test_accepts_the_oximeters_answer_to_its_challenge)
{
    uint8_t answer[SECURITY_LOG_HEAD_ANSWER_SIZE];
    uint32_t newest = 0u;

    answer_as_oximeter(oximeter_id, challenge, 42u, answer);
    check_as_backend_for(oximeter_id);
    zassert_true(log_verifier_check_head(challenge, answer, &newest));
    zassert_equal(newest, 42u);
}

/* What a gateway holding back records might send: an answer to an earlier challenge, replayed; one
 * with the number lowered to hide the newest; or another oximeter's. */
ZTEST(limits_signer, test_refuses_an_answer_it_didnt_ask_for)
{
    static const uint8_t earlier[SECURITY_LOG_CHALLENGE_SIZE] = {0x3c, 0x51, 0x9e, 0x07, 0xd2, 0x64,
                                                                 0xab, 0x18, 0xf5, 0x2e, 0x80, 0x4d,
                                                                 0x96, 0x0b, 0x73, 0xca};
    static const struct {
        const char *name;
        const uint8_t *id;
        const uint8_t *asked;
        bool lowered;
    } cases[] = {
        {"answer to an earlier challenge", oximeter_id, earlier, false},
        {"number lowered", oximeter_id, challenge, true},
        {"another oximeter's answer", other_id, challenge, false},
    };

    for (size_t i = 0u; i < ARRAY_SIZE(cases); i++) {
        uint8_t answer[SECURITY_LOG_HEAD_ANSWER_SIZE];
        uint32_t newest = 0u;

        answer_as_oximeter(cases[i].id, cases[i].asked, 42u, answer);
        if (cases[i].lowered) {
            sys_put_le32(41u, answer);
        }
        check_as_backend_for(oximeter_id);
        zassert_false(log_verifier_check_head(challenge, answer, &newest), "%s", cases[i].name);
    }
}

/* Both sides start each test holding the same key, as the oximeter and its hub do. */
static void share_key(void *fixture)
{
    ARG_UNUSED(fixture);
    zassert_true(limits_crypto_init(shared_key, sizeof shared_key));
    limits_auth_init(&oximeter, &limits_crypto);
    zassert_true(limits_signer_init(shared_key, sizeof shared_key));
}

ZTEST_SUITE(limits_signer, NULL, NULL, share_key, NULL, NULL);
