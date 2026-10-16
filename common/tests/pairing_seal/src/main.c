#include "pairing_seal.h"

#include <zephyr/ztest.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint8_t key[DEVICE_KEY_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
static const uint8_t other_key[DEVICE_KEY_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x20};
static const uint8_t device_id[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
static const uint8_t other_device_id[DEVICE_ID_SIZE] = {0x01, 0x23, 0x45, 0x67,
                                                        0x89, 0xab, 0xcd, 0xee};
static const uint8_t data[PAIRING_DATA_SIZE] = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf};

static void seal(uint8_t sealed[PAIRING_SEALED_SIZE])
{
    zassert_true(pairing_seal_init(key, sizeof key));
    zassert_true(pairing_seal(device_id, data, sealed));
}

ZTEST(pairing_seal, test_opens_what_it_sealed)
{
    uint8_t sealed[PAIRING_SEALED_SIZE];
    uint8_t opened[PAIRING_DATA_SIZE];

    seal(sealed);
    zassert_true(pairing_open_init(key, sizeof key));
    zassert_true(pairing_open(device_id, sealed, opened));
    zassert_mem_equal(opened, data, sizeof opened);
}

/* Only the backend, which can derive this oximeter's key, opens its pairing data, and only as the
 * oximeter sealed it. */
ZTEST(pairing_seal, test_refuses_what_it_cannot_authenticate)
{
    static const struct {
        const char *name;
        const uint8_t *key;
        bool change_a_byte;
        const uint8_t *device_id;
    } cases[] = {
        {"another key", other_key, false, device_id},
        {"a changed byte", key, true, device_id},
        {"another device", key, false, other_device_id},
    };
    uint8_t sealed[PAIRING_SEALED_SIZE];
    uint8_t opened[PAIRING_DATA_SIZE];

    for (size_t i = 0u; i < ARRAY_SIZE(cases); i++) {
        seal(sealed);
        if (cases[i].change_a_byte) {
            sealed[PAIRING_SEAL_NONCE_SIZE] ^= 0x01u;
        }
        zassert_true(pairing_open_init(cases[i].key, DEVICE_KEY_SIZE), "%s", cases[i].name);
        zassert_false(pairing_open(cases[i].device_id, sealed, opened), "%s", cases[i].name);
    }
}

/* AES-CCM gives nothing away only while no nonce repeats under a key. */
ZTEST(pairing_seal, test_seals_with_a_fresh_nonce_each_time)
{
    uint8_t first[PAIRING_SEALED_SIZE];
    uint8_t second[PAIRING_SEALED_SIZE];

    seal(first);
    seal(second);
    zassert_true(memcmp(first, second, PAIRING_SEAL_NONCE_SIZE) != 0);
}

ZTEST_SUITE(pairing_seal, NULL, NULL, NULL, NULL, NULL);
