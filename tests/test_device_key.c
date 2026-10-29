#include "device_key.h"
#include "unity.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

/* The check reads every byte: a key one byte away from erased or zeroed flash is still a key. */
static void test_accepts_a_provisioned_key(void)
{
    static const struct {
        const char *name;
        uint8_t fill;
        uint8_t last;
    } cases[] = {
        {"a provisioned key", 0x5Au, 0xA5u},
        {"erased but for its last byte", 0xFFu, 0xFEu},
        {"zeroed but for its last byte", 0x00u, 0x01u},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        uint8_t key[DEVICE_KEY_SIZE];

        (void)memset(key, cases[i].fill, sizeof key);
        key[DEVICE_KEY_SIZE - 1u] = cases[i].last;
        TEST_ASSERT_TRUE_MESSAGE(device_key_provisioned(key), cases[i].name);
    }
}

/* Erased flash reads as all ones; writing can only clear bits, so all zeros is what someone who
 * can write but not read would leave. Neither is a key the factory provisioned. */
static void test_refuses_an_erased_or_zeroed_key(void)
{
    static const struct {
        const char *name;
        uint8_t fill;
    } cases[] = {{"erased", 0xFFu}, {"zeroed", 0x00u}};

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        uint8_t key[DEVICE_KEY_SIZE];

        (void)memset(key, cases[i].fill, sizeof key);
        TEST_ASSERT_FALSE_MESSAGE(device_key_provisioned(key), cases[i].name);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_accepts_a_provisioned_key);
    RUN_TEST(test_refuses_an_erased_or_zeroed_key);
    return UNITY_END();
}
