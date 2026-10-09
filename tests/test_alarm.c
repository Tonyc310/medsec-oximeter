#include "alarm.h"
#include "unity.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    uint8_t spo2;
    uint16_t pulse_low;
    uint16_t pulse_high;
} limits_case_t;

static alarm_limits_t limits;

void setUp(void)
{
    limits = alarm_default_limits();
}

void tearDown(void)
{
}

static uint8_t check(uint8_t spo2, uint16_t pulse)
{
    const oximetry_reading_t reading = {.valid = true, .spo2_percent = spo2, .pulse_bpm = pulse};

    return alarm_check(&limits, &reading);
}

/* Sends limits the way the hub does: SpO2, then the pulse limits little-endian. */
static bool decode(const limits_case_t *c)
{
    const uint8_t data[ALARM_LIMITS_SIZE] = {
        c->spo2,
        (uint8_t)(c->pulse_low & 0xFFu),
        (uint8_t)(c->pulse_low >> 8),
        (uint8_t)(c->pulse_high & 0xFFu),
        (uint8_t)(c->pulse_high >> 8),
    };

    return alarm_limits_decode(data, sizeof data, &limits);
}

/* The defaults alarm below 90%, and below 50 or above 120 bpm. */
static void test_no_alarm_at_the_limits(void)
{
    TEST_ASSERT_EQUAL_HEX8(0u, check(90u, 50u));
    TEST_ASSERT_EQUAL_HEX8(0u, check(90u, 120u));
}

static void test_alarms_one_step_past_each_limit(void)
{
    static const struct {
        const char *limit;
        uint8_t spo2;
        uint16_t pulse;
        uint8_t alarm;
    } cases[] = {
        {"SpO2 low", 89u, 70u, ALARM_SPO2_LOW},
        {"pulse low", 95u, 49u, ALARM_PULSE_LOW},
        {"pulse high", 95u, 121u, ALARM_PULSE_HIGH},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(cases[i].alarm, check(cases[i].spo2, cases[i].pulse),
                                       cases[i].limit);
    }
}

static void test_reports_every_alarm_a_reading_meets(void)
{
    TEST_ASSERT_EQUAL_HEX8(ALARM_SPO2_LOW | ALARM_PULSE_HIGH, check(85u, 121u));
}

static void test_raises_a_technical_alarm_when_there_is_no_reading(void)
{
    const oximetry_reading_t no_reading = {.valid = false};

    TEST_ASSERT_EQUAL_HEX8(ALARM_NO_READING, alarm_check(&limits, &no_reading));
}

static void test_decodes_limits_sent_over_the_air(void)
{
    static const uint8_t data[] = {95u, 40u, 0u, 130u, 0u};

    TEST_ASSERT_TRUE(alarm_limits_decode(data, sizeof data, &limits));
    TEST_ASSERT_EQUAL_UINT8(95u, limits.spo2_low_percent);
    TEST_ASSERT_EQUAL_UINT16(40u, limits.pulse_low_bpm);
    TEST_ASSERT_EQUAL_UINT16(130u, limits.pulse_high_bpm);
}

static void test_encodes_limits_the_way_it_decodes_them(void)
{
    static const alarm_limits_t raised = {
        .spo2_low_percent = 95u, .pulse_low_bpm = 40u, .pulse_high_bpm = 130u};
    static const uint8_t expected[ALARM_LIMITS_SIZE] = {95u, 40u, 0u, 130u, 0u};
    uint8_t encoded[ALARM_LIMITS_SIZE];

    alarm_limits_encode(&raised, encoded);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, encoded, ALARM_LIMITS_SIZE);
}

/* 50% is a legitimate setting, and it also silences a hypoxia alarm: only authentication can tell
 * who sent it. */
static void test_accepts_limits_at_the_edges_of_each_range(void)
{
    static const limits_case_t cases[] = {
        {"lowest SpO2 limit", 50u, 50u, 120u},
        {"highest SpO2 limit", 99u, 50u, 120u},
        {"widest pulse range", 90u, 30u, 240u},
        {"narrowest pulse range", 90u, 60u, 61u},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        TEST_ASSERT_TRUE_MESSAGE(decode(&cases[i]), cases[i].name);
    }
}

static void test_rejects_limits_just_outside_each_range(void)
{
    static const limits_case_t cases[] = {
        {"SpO2 limit of 49%", 49u, 50u, 120u},     {"SpO2 limit of 100%", 100u, 50u, 120u},
        {"pulse limit of 29 bpm", 90u, 29u, 120u}, {"pulse limit of 241 bpm", 90u, 50u, 241u},
        {"equal pulse limits", 90u, 60u, 60u},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(decode(&cases[i]), cases[i].name);
    }
}

static void test_rejects_limits_of_the_wrong_length(void)
{
    static const uint8_t too_short[] = {95u, 50u, 0u, 120u};
    static const uint8_t too_long[] = {95u, 50u, 0u, 120u, 0u, 0u};

    TEST_ASSERT_FALSE(alarm_limits_decode(too_short, sizeof too_short, &limits));
    TEST_ASSERT_FALSE(alarm_limits_decode(too_long, sizeof too_long, &limits));
}

static void test_keeps_the_limits_in_force_when_it_rejects_new_ones(void)
{
    static const limits_case_t rejected = {"SpO2 limit of 100%", 100u, 40u, 130u};

    TEST_ASSERT_FALSE(decode(&rejected));
    /* Still the defaults. */
    TEST_ASSERT_EQUAL_UINT8(90u, limits.spo2_low_percent);
    TEST_ASSERT_EQUAL_UINT16(50u, limits.pulse_low_bpm);
    TEST_ASSERT_EQUAL_UINT16(120u, limits.pulse_high_bpm);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_alarm_at_the_limits);
    RUN_TEST(test_alarms_one_step_past_each_limit);
    RUN_TEST(test_reports_every_alarm_a_reading_meets);
    RUN_TEST(test_raises_a_technical_alarm_when_there_is_no_reading);
    RUN_TEST(test_decodes_limits_sent_over_the_air);
    RUN_TEST(test_encodes_limits_the_way_it_decodes_them);
    RUN_TEST(test_accepts_limits_at_the_edges_of_each_range);
    RUN_TEST(test_rejects_limits_just_outside_each_range);
    RUN_TEST(test_rejects_limits_of_the_wrong_length);
    RUN_TEST(test_keeps_the_limits_in_force_when_it_rejects_new_ones);
    return UNITY_END();
}
