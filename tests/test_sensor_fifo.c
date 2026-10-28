#include "sensor_fifo.h"
#include "unity.h"

#include <stddef.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_counts_the_samples_between_the_pointers(void)
{
    static const struct {
        const char *name;
        sensor_fifo_t fifo;
        uint32_t waiting;
    } cases[] = {
        {"behind", {.write_pointer = 10u, .read_pointer = 3u}, 7u},
        {"wrapped", {.write_pointer = 2u, .read_pointer = 30u}, 4u},
        {"empty", {.write_pointer = 5u, .read_pointer = 5u}, 0u},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(cases[i].waiting, sensor_fifo_waiting(cases[i].fifo),
                                         cases[i].name);
    }
}

/* A full FIFO's pointers meet as an empty one's do; samples it had to drop tell them apart. */
static void test_counts_a_full_fifo_once_it_loses_samples(void)
{
    const sensor_fifo_t fifo = {.write_pointer = 7u, .overflow = 3u, .read_pointer = 7u};

    TEST_ASSERT_EQUAL_UINT32(SENSOR_FIFO_DEPTH, sensor_fifo_waiting(fifo));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_counts_the_samples_between_the_pointers);
    RUN_TEST(test_counts_a_full_fifo_once_it_loses_samples);
    return UNITY_END();
}
