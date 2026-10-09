#include "oximetry.h"
#include "unity.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define TWO_PI 6.2831853f
#define RED_LEVEL 110000.0f
#define INFRARED_LEVEL 130000.0f
#define INFRARED_PULSE 2000.0f /* a 1.5% perfusion index */

static oximetry_t oximetry;
static oximetry_reading_t reading;
static uint32_t elapsed; /* samples since oximetry_init, so the wave stays continuous */

static void start(void)
{
    oximetry_init(&oximetry);
    elapsed = 0u;
}

/* Feeds `seconds` of a steady pulse, a cosine around each light level, and returns the number
 * of readings. The red pulse is set to give `spo2` on the calibration line SpO2 = 110 - 25 R. */
static uint32_t feed_pulse(uint32_t seconds, float bpm, float spo2, float infrared_pulse)
{
    const float ratio = (110.0f - spo2) / 25.0f;
    const float red_pulse = ratio * (infrared_pulse / INFRARED_LEVEL) * RED_LEVEL;
    uint32_t readings = 0u;

    for (uint32_t n = 0u; n < (seconds * OXIMETRY_SAMPLE_RATE_HZ); n++) {
        const float time = (float)elapsed / (float)OXIMETRY_SAMPLE_RATE_HZ;
        const float wave = 0.5f * cosf(TWO_PI * (bpm / 60.0f) * time);
        const oximetry_sample_t sample = {
            .red = (uint32_t)lroundf(RED_LEVEL + (red_pulse * wave)),
            .infrared = (uint32_t)lroundf(INFRARED_LEVEL + (infrared_pulse * wave)),
        };

        elapsed++;
        if (oximetry_add_sample(&oximetry, sample, &reading)) {
            readings++;
        }
    }
    return readings;
}

void setUp(void)
{
    start();
}

void tearDown(void)
{
}

static void test_reports_spo2_and_pulse_once_a_second_from_the_fourth(void)
{
    static const struct {
        float bpm;
        uint8_t spo2;
    } patients[] = {{45.0f, 99u}, {72.0f, 97u}, {150.0f, 85u}};

    for (size_t i = 0u; i < (sizeof patients / sizeof patients[0]); i++) {
        const float bpm = patients[i].bpm;
        const float spo2 = (float)patients[i].spo2;

        start();
        TEST_ASSERT_EQUAL_UINT32(0u, feed_pulse(3u, bpm, spo2, INFRARED_PULSE));
        TEST_ASSERT_EQUAL_UINT32(2u, feed_pulse(2u, bpm, spo2, INFRARED_PULSE));
        TEST_ASSERT_TRUE(reading.valid);
        TEST_ASSERT_UINT_WITHIN(1u, (uint32_t)bpm, reading.pulse_bpm);
        TEST_ASSERT_EQUAL_UINT8(patients[i].spo2, reading.spo2_percent);
    }
}

static void test_reports_nothing_without_a_strong_steady_pulse(void)
{
    uint32_t seed = 1u;

    /* A pulse 0.04% of the light level: too weak to measure. */
    (void)feed_pulse(5u, 72.0f, 97.0f, 50.0f);
    TEST_ASSERT_FALSE(reading.valid);

    /* Noise as strong as a real pulse, but with no rhythm in it. */
    start();
    for (uint32_t n = 0u; n < (5u * OXIMETRY_SAMPLE_RATE_HZ); n++) {
        seed = (seed * 1664525u) + 1013904223u; /* a fixed-seed LCG, so the noise repeats */
        const uint32_t noise = seed >> 21;      /* 0 to 2047 */
        const oximetry_sample_t sample = {
            .red = (uint32_t)RED_LEVEL + noise,
            .infrared = (uint32_t)INFRARED_LEVEL + noise,
        };

        (void)oximetry_add_sample(&oximetry, sample, &reading);
    }
    TEST_ASSERT_FALSE(reading.valid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reports_spo2_and_pulse_once_a_second_from_the_fourth);
    RUN_TEST(test_reports_nothing_without_a_strong_steady_pulse);
    return UNITY_END();
}
