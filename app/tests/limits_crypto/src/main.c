#include "limits_crypto.h"

#include <psa/crypto.h>
#include <zephyr/ztest.h>

#include <stddef.h>
#include <stdint.h>

/* RFC 4231's HMAC-SHA-256 test cases, with each tag cut to its first 128 bits. */
typedef struct {
    const char *name;
    const uint8_t *key;
    size_t key_length;
    const char *data;
    uint8_t tag[LIMITS_AUTH_TAG_SIZE];
} rfc4231_case_t;

static const uint8_t key_0b[20] = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                   0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
static const uint8_t key_jefe[4] = {'J', 'e', 'f', 'e'};
static const uint8_t key_0c[20] = {0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
                                   0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c};

static const rfc4231_case_t cases[] = {
    {"test case 1",
     key_0b,
     sizeof key_0b,
     "Hi There",
     {0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1,
      0x2b}},
    /* A key shorter than the hash output. */
    {"test case 2",
     key_jefe,
     sizeof key_jefe,
     "what do ya want for nothing?",
     {0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75,
      0xc7}},
    /* The RFC's own test of truncation to 128 bits. */
    {"test case 5",
     key_0c,
     sizeof key_0c,
     "Test With Truncation",
     {0xa3, 0xb6, 0x16, 0x74, 0x73, 0x10, 0x0e, 0xe0, 0x6e, 0x0c, 0x79, 0x6c, 0x29, 0x55, 0x55,
      0x2b}},
};

static bool verify(const rfc4231_case_t *c, const uint8_t tag[LIMITS_AUTH_TAG_SIZE])
{
    return limits_crypto.verify((const uint8_t *)c->data, strlen(c->data), tag);
}

ZTEST(limits_crypto, test_accepts_rfc4231_tags_cut_to_128_bits)
{
    for (size_t i = 0u; i < ARRAY_SIZE(cases); i++) {
        zassert_true(limits_crypto_init(cases[i].key, cases[i].key_length), "%s", cases[i].name);
        zassert_true(verify(&cases[i], cases[i].tag), "%s", cases[i].name);
    }
}

ZTEST(limits_crypto, test_rejects_a_tag_with_one_bit_changed)
{
    uint8_t tag[LIMITS_AUTH_TAG_SIZE];

    memcpy(tag, cases[2].tag, sizeof tag);
    tag[LIMITS_AUTH_TAG_SIZE - 1u] ^= 0x01u;
    zassert_false(verify(&cases[2], tag));
}

/* A failed load must not leave the previous key usable. */
ZTEST(limits_crypto, test_rejects_every_tag_after_a_key_fails_to_load)
{
    zassert_false(limits_crypto_init(cases[2].key, 0u));
    zassert_false(verify(&cases[2], cases[2].tag));
}

ZTEST(limits_crypto, test_returns_fresh_random_bytes)
{
    uint8_t first[LIMITS_AUTH_CHALLENGE_SIZE];
    uint8_t second[LIMITS_AUTH_CHALLENGE_SIZE];

    zassert_true(limits_crypto.random(first, sizeof first));
    zassert_true(limits_crypto.random(second, sizeof second));
    zassert_true(memcmp(first, second, sizeof first) != 0);
}

/* PSA has a fixed number of key slots: a replaced key must free its slot. */
ZTEST(limits_crypto, test_replaces_the_key_without_using_up_key_slots)
{
    for (int i = 0; i <= CONFIG_MBEDTLS_PSA_KEY_SLOT_COUNT; i++) {
        zassert_true(limits_crypto_init(cases[2].key, cases[2].key_length), "load %d", i);
    }
    zassert_true(verify(&cases[2], cases[2].tag));
}

/* limits_auth refuses every command until it gets a new challenge, which relies on this. */
ZTEST(limits_crypto, test_reports_failure_when_the_generator_is_not_running)
{
    uint8_t challenge[LIMITS_AUTH_CHALLENGE_SIZE];

    mbedtls_psa_crypto_free();
    zassert_false(limits_crypto.random(challenge, sizeof challenge));
}

/* Every test starts with test case 5's key loaded. */
static void load_key(void *fixture)
{
    ARG_UNUSED(fixture);
    zassert_true(limits_crypto_init(cases[2].key, cases[2].key_length));
}

ZTEST_SUITE(limits_crypto, NULL, NULL, load_key, NULL, NULL);
