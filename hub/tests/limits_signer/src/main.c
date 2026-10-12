#include "limits_auth.h"
#include "limits_crypto.h"
#include "limits_signer.h"

#include <zephyr/ztest.h>

#include <stdint.h>

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
    zassert_true(limits_auth_apply(&oximeter, command, sizeof command, &limits));
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
    zassert_false(limits_auth_apply(&oximeter, command, sizeof command, &limits));
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
