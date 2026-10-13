#include "limits_auth.h"
#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Random bytes count up, so each challenge is known and differs from the last. */
static uint8_t next_random;
static bool random_fails;

static bool fake_random(uint8_t *out, size_t length)
{
    if (random_fails) {
        return false;
    }
    for (size_t i = 0u; i < length; i++) {
        out[i] = next_random++;
    }
    return true;
}

/* Not a real MAC: FNV-1a, a non-cryptographic hash in which every byte changes the result, seeded
 * with each tag byte's index. */
static void fake_mac(const uint8_t *message, size_t length, uint8_t tag[LIMITS_AUTH_TAG_SIZE])
{
    for (size_t i = 0u; i < LIMITS_AUTH_TAG_SIZE; i++) {
        uint32_t hash = 2166136261u ^ (uint32_t)i;

        for (size_t j = 0u; j < length; j++) {
            hash = (hash ^ message[j]) * 16777619u;
        }
        tag[i] = (uint8_t)(hash >> 24);
    }
}

static bool fake_verify(const uint8_t *message, size_t length, const uint8_t *tag)
{
    uint8_t expected[LIMITS_AUTH_TAG_SIZE];

    fake_mac(message, length, expected);
    return memcmp(expected, tag, sizeof expected) == 0;
}

static const limits_auth_crypto_t fake_crypto = {.random = fake_random, .verify = fake_verify};
static const uint8_t raised[ALARM_LIMITS_SIZE] = {95u, 40u, 0u, 130u, 0u};
static limits_auth_t auth;
static alarm_limits_t limits;

void setUp(void)
{
    next_random = 0x10u;
    random_fails = false;
    limits_auth_init(&auth, &fake_crypto);
    limits = alarm_default_limits();
}

void tearDown(void)
{
}

/* The challenge is the next 16 random bytes, starting at `first`. */
static void expect_challenge(uint8_t first)
{
    uint8_t expected[LIMITS_AUTH_CHALLENGE_SIZE];

    for (size_t i = 0u; i < LIMITS_AUTH_CHALLENGE_SIZE; i++) {
        expected[i] = (uint8_t)(first + i);
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, limits_auth_challenge(&auth),
                                 LIMITS_AUTH_CHALLENGE_SIZE);
}

/* Makes a command the way the backend will: the limits, then the tag over the current challenge
 * followed by the limits. */
static void sign(const uint8_t limits_bytes[ALARM_LIMITS_SIZE],
                 uint8_t command[LIMITS_AUTH_COMMAND_SIZE])
{
    uint8_t message[LIMITS_AUTH_CHALLENGE_SIZE + ALARM_LIMITS_SIZE];

    memcpy(message, limits_auth_challenge(&auth), LIMITS_AUTH_CHALLENGE_SIZE);
    memcpy(&message[LIMITS_AUTH_CHALLENGE_SIZE], limits_bytes, ALARM_LIMITS_SIZE);
    memcpy(command, limits_bytes, ALARM_LIMITS_SIZE);
    fake_mac(message, sizeof message, &command[ALARM_LIMITS_SIZE]);
}

static void test_takes_each_challenge_from_the_random_source(void)
{
    expect_challenge(0x10u);
}

/* The app asks for one on every connection, so a command made on an earlier connection is stale. */
static void test_issues_a_new_challenge_on_request(void)
{
    limits_auth_new_challenge(&auth);
    expect_challenge(0x20u);
}

static void test_accepts_limits_signed_over_the_current_challenge(void)
{
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];

    sign(raised, command);
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_ACCEPTED,
                          limits_auth_apply(&auth, command, sizeof command, &limits));
    TEST_ASSERT_EQUAL_UINT8(95u, limits.spo2_low_percent);
    TEST_ASSERT_EQUAL_UINT16(40u, limits.pulse_low_bpm);
    TEST_ASSERT_EQUAL_UINT16(130u, limits.pulse_high_bpm);
}

static void test_rejects_a_tag_made_over_different_limits(void)
{
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];

    sign(raised, command);
    command[0] = 50u; /* the SpO2 limit, after signing */
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_BAD_TAG,
                          limits_auth_apply(&auth, command, sizeof command, &limits));
}

static void test_rejects_a_tag_made_for_another_challenge(void)
{
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];

    sign(raised, command);
    limits_auth_new_challenge(&auth);
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_BAD_TAG,
                          limits_auth_apply(&auth, command, sizeof command, &limits));
}

static void test_never_accepts_the_same_command_twice(void)
{
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];

    sign(raised, command);
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_ACCEPTED,
                          limits_auth_apply(&auth, command, sizeof command, &limits));
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_BAD_TAG,
                          limits_auth_apply(&auth, command, sizeof command, &limits));
}

/* So every guess at a tag is made against a fresh challenge. */
static void test_replaces_the_challenge_after_a_rejected_attempt(void)
{
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];

    sign(raised, command);
    command[0] = 50u;
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_BAD_TAG,
                          limits_auth_apply(&auth, command, sizeof command, &limits));
    expect_challenge(0x20u);
}

static void test_keeps_the_limits_in_force_when_it_rejects_a_command(void)
{
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];

    sign(raised, command);
    command[0] = 50u;
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_BAD_TAG,
                          limits_auth_apply(&auth, command, sizeof command, &limits));
    /* Still the defaults. */
    TEST_ASSERT_EQUAL_UINT8(90u, limits.spo2_low_percent);
    TEST_ASSERT_EQUAL_UINT16(50u, limits.pulse_low_bpm);
    TEST_ASSERT_EQUAL_UINT16(120u, limits.pulse_high_bpm);
}

static void test_rejects_a_command_of_the_wrong_length(void)
{
    uint8_t too_short[LIMITS_AUTH_COMMAND_SIZE - 1u];
    uint8_t too_long[LIMITS_AUTH_COMMAND_SIZE + 1u] = {0};

    sign(raised, too_long);
    memcpy(too_short, too_long, sizeof too_short);
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_WRONG_LENGTH,
                          limits_auth_apply(&auth, too_short, sizeof too_short, &limits));
    sign(raised, too_long);
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_WRONG_LENGTH,
                          limits_auth_apply(&auth, too_long, sizeof too_long, &limits));
}

/* Authentication says who sent the limits, not that they're sensible. */
static void test_still_rejects_authentic_limits_out_of_range(void)
{
    static const uint8_t spo2_limit_100[ALARM_LIMITS_SIZE] = {100u, 50u, 0u, 120u, 0u};
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];

    sign(spo2_limit_100, command);
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_INVALID_LIMITS,
                          limits_auth_apply(&auth, command, sizeof command, &limits));
}

/* A challenge that couldn't be replaced mustn't stay usable: that would let a command be replayed.
 */
static void test_rejects_every_command_when_no_new_challenge_can_be_made(void)
{
    uint8_t command[LIMITS_AUTH_COMMAND_SIZE];

    sign(raised, command);
    random_fails = true;
    limits_auth_new_challenge(&auth);
    TEST_ASSERT_EQUAL_INT(LIMITS_AUTH_NO_CHALLENGE,
                          limits_auth_apply(&auth, command, sizeof command, &limits));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_takes_each_challenge_from_the_random_source);
    RUN_TEST(test_issues_a_new_challenge_on_request);
    RUN_TEST(test_accepts_limits_signed_over_the_current_challenge);
    RUN_TEST(test_rejects_a_tag_made_over_different_limits);
    RUN_TEST(test_rejects_a_tag_made_for_another_challenge);
    RUN_TEST(test_never_accepts_the_same_command_twice);
    RUN_TEST(test_replaces_the_challenge_after_a_rejected_attempt);
    RUN_TEST(test_keeps_the_limits_in_force_when_it_rejects_a_command);
    RUN_TEST(test_rejects_a_command_of_the_wrong_length);
    RUN_TEST(test_still_rejects_authentic_limits_out_of_range);
    RUN_TEST(test_rejects_every_command_when_no_new_challenge_can_be_made);
    return UNITY_END();
}
