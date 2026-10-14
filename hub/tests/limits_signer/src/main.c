#include "device_keys.h"
#include "limits_auth.h"
#include "limits_crypto.h"
#include "limits_signer.h"

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

    zassert_true(device_keys_derive(shared_key, sizeof shared_key, first, first_key));
    zassert_true(device_keys_derive(shared_key, sizeof shared_key, second, second_key));
    zassert_true(device_keys_derive(shared_key, sizeof shared_key, first, first_again));
    zassert_true(memcmp(first_key, second_key, sizeof first_key) != 0);
    zassert_mem_equal(first_key, first_again, sizeof first_key);
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

    zassert_true(device_keys_derive(shared_key, sizeof shared_key, oximeter_id, key));
    zassert_true(limits_crypto_init(key, sizeof key));
    limits_auth_init(&oximeter, &limits_crypto);

    zassert_true(limits_signer_init(key, sizeof key));
    zassert_true(limits_signer_sign(limits_auth_challenge(&oximeter), &raised, command));
    zassert_equal(limits_auth_apply(&oximeter, command, sizeof command, &limits),
                  LIMITS_AUTH_ACCEPTED);

    zassert_true(device_keys_derive(shared_key, sizeof shared_key, other_id, key));
    zassert_true(limits_signer_init(key, sizeof key));
    zassert_true(limits_signer_sign(limits_auth_challenge(&oximeter), &raised, command));
    zassert_equal(limits_auth_apply(&oximeter, command, sizeof command, &limits),
                  LIMITS_AUTH_BAD_TAG);
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
