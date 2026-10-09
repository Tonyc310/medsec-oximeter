#include "alarm.h"
#include "unity.h"

#include <stdint.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static oximetry_reading_t reading(uint8_t spo2, uint16_t pulse)
{
    return (oximetry_reading_t){.valid = true, .spo2_percent = spo2, .pulse_bpm = pulse};
}

static void test_alarms_on_each_limit_and_on_no_reading(void)
{
    const alarm_limits_t limits = alarm_default_limits();
    const oximetry_reading_t no_reading = {.valid = false};
    oximetry_reading_t r;

    /* "Below 90%" means 90% itself is fine. */
    r = reading(90u, 70u);
    TEST_ASSERT_EQUAL_HEX8(0u, alarm_check(&limits, &r));
    r = reading(89u, 70u);
    TEST_ASSERT_EQUAL_HEX8(ALARM_SPO2_LOW, alarm_check(&limits, &r));
    r = reading(95u, 49u);
    TEST_ASSERT_EQUAL_HEX8(ALARM_PULSE_LOW, alarm_check(&limits, &r));
    r = reading(85u, 121u);
    TEST_ASSERT_EQUAL_HEX8(ALARM_SPO2_LOW | ALARM_PULSE_HIGH, alarm_check(&limits, &r));
    TEST_ASSERT_EQUAL_HEX8(ALARM_NO_READING, alarm_check(&limits, &no_reading));
}

static void test_accepts_settings_a_monitor_would_and_rejects_the_rest(void)
{
    static const uint8_t raised[] = {95u, 50u, 0u, 120u, 0u};
    /* 50% is a legitimate setting, and it also silences a hypoxia alarm: only authentication can
     * tell who sent it. */
    static const uint8_t silenced[] = {50u, 50u, 0u, 120u, 0u};
    static const uint8_t too_short[] = {95u, 50u, 0u, 120u};
    static const uint8_t spo2_limit_100[] = {100u, 50u, 0u, 120u, 0u};
    static const uint8_t pulse_limits_crossed[] = {95u, 120u, 0u, 50u, 0u};
    static const uint8_t pulse_limit_too_high[] = {95u, 50u, 0u, 0x2Cu, 0x01u}; /* 300 bpm */
    alarm_limits_t limits = alarm_default_limits();
    uint8_t encoded[ALARM_LIMITS_SIZE];

    TEST_ASSERT_TRUE(alarm_limits_decode(raised, sizeof raised, &limits));
    TEST_ASSERT_EQUAL_UINT8(95u, limits.spo2_low_percent);
    alarm_limits_encode(&limits, encoded);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(raised, encoded, ALARM_LIMITS_SIZE);
    TEST_ASSERT_TRUE(alarm_limits_decode(silenced, sizeof silenced, &limits));

    TEST_ASSERT_FALSE(alarm_limits_decode(too_short, sizeof too_short, &limits));
    TEST_ASSERT_FALSE(alarm_limits_decode(spo2_limit_100, sizeof spo2_limit_100, &limits));
    TEST_ASSERT_FALSE(
        alarm_limits_decode(pulse_limits_crossed, sizeof pulse_limits_crossed, &limits));
    TEST_ASSERT_FALSE(
        alarm_limits_decode(pulse_limit_too_high, sizeof pulse_limit_too_high, &limits));
    /* Rejected settings leave the last accepted ones in force. */
    TEST_ASSERT_EQUAL_UINT8(50u, limits.spo2_low_percent);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_alarms_on_each_limit_and_on_no_reading);
    RUN_TEST(test_accepts_settings_a_monitor_would_and_rejects_the_rest);
    return UNITY_END();
}
