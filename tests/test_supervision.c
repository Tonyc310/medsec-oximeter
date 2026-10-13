#include "supervision.h"
#include "unity.h"

#include <stdint.h>

static supervision_t supervision;

void setUp(void)
{
    supervision_init(&supervision, 0u);
}

void tearDown(void)
{
}

static void test_not_stalled_while_samples_keep_arriving(void)
{
    supervision_sample(&supervision, 500u);
    TEST_ASSERT_FALSE(supervision_stalled(&supervision, 1499u));
}

static void test_stalled_once_a_second_passes_without_a_sample(void)
{
    supervision_sample(&supervision, 500u);
    TEST_ASSERT_TRUE(supervision_stalled(&supervision, 1500u));
}

/* So the technical alarm clears when the sensor recovers. */
static void test_a_new_sample_clears_the_stall(void)
{
    TEST_ASSERT_TRUE(supervision_stalled(&supervision, 1500u));
    supervision_sample(&supervision, 1600u);
    TEST_ASSERT_FALSE(supervision_stalled(&supervision, 1600u));
}

/* Zephyr's 32-bit uptime wraps after 49.7 days, well within a monitoring session. */
static void test_counts_across_the_millisecond_counter_wrapping(void)
{
    supervision_sample(&supervision, UINT32_MAX - 200u);
    TEST_ASSERT_FALSE(supervision_stalled(&supervision, UINT32_MAX - 100u)); /* 100 ms on */
    TEST_ASSERT_FALSE(supervision_stalled(&supervision, 798u));              /* 999 ms on */
    TEST_ASSERT_TRUE(supervision_stalled(&supervision, 799u));               /* 1000 ms on */
}

/* The sensor starts well after boot: no false alarm before its first sample is due. */
static void test_times_a_stall_from_the_start_until_the_first_sample(void)
{
    supervision_init(&supervision, 5000u);
    TEST_ASSERT_FALSE(supervision_stalled(&supervision, 5999u));
    TEST_ASSERT_TRUE(supervision_stalled(&supervision, 6000u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_not_stalled_while_samples_keep_arriving);
    RUN_TEST(test_stalled_once_a_second_passes_without_a_sample);
    RUN_TEST(test_a_new_sample_clears_the_stall);
    RUN_TEST(test_counts_across_the_millisecond_counter_wrapping);
    RUN_TEST(test_times_a_stall_from_the_start_until_the_first_sample);
    return UNITY_END();
}
