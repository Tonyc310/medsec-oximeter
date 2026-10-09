#include "oximetry.h"
#include "unity.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define TWO_PI 6.2831853f
#define RED_LEVEL 110000.0f
#define INFRARED_LEVEL 130000.0f
#define SECONDS(s) ((s) * OXIMETRY_SAMPLE_RATE_HZ)

typedef struct {
    float bpm;
    float spo2;           /* sets the red pulse through the calibration line SpO2 = 110 - 25 R */
    float infrared_pulse; /* ADC counts, peak to peak */
} patient_t;

/* 2000 on 130000 is a 1.5% perfusion index. */
static const patient_t healthy = {.bpm = 72.0f, .spo2 = 97.0f, .infrared_pulse = 2000.0f};

static oximetry_t oximetry;
static oximetry_reading_t reading;
static uint32_t elapsed; /* samples since oximetry_init, so the wave stays continuous */

static void start(void)
{
    oximetry_init(&oximetry);
    reading = (oximetry_reading_t){.valid = false};
    elapsed = 0u;
}

void setUp(void)
{
    start();
}

void tearDown(void)
{
}

/* Feeds `samples` of a steady pulse, a cosine around each light level, and returns the number of
 * readings. */
static uint32_t feed(const patient_t *patient, uint32_t samples)
{
    const float ratio = (110.0f - patient->spo2) / 25.0f;
    const float red_pulse = ratio * (patient->infrared_pulse / INFRARED_LEVEL) * RED_LEVEL;
    uint32_t readings = 0u;

    for (uint32_t n = 0u; n < samples; n++) {
        const float time = (float)elapsed / (float)OXIMETRY_SAMPLE_RATE_HZ;
        const float wave = 0.5f * cosf(TWO_PI * (patient->bpm / 60.0f) * time);
        const oximetry_sample_t sample = {
            .red = (uint32_t)lroundf(RED_LEVEL + (red_pulse * wave)),
            .infrared = (uint32_t)lroundf(INFRARED_LEVEL + (patient->infrared_pulse * wave)),
        };

        elapsed++;
        if (oximetry_add_sample(&oximetry, sample, &reading)) {
            readings++;
        }
    }
    return readings;
}

static void test_reports_once_a_second_from_the_fourth_second(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, feed(&healthy, SECONDS(4u) - 1u));
    TEST_ASSERT_EQUAL_UINT32(1u, feed(&healthy, 1u));
    TEST_ASSERT_EQUAL_UINT32(0u, feed(&healthy, SECONDS(1u) - 1u));
    TEST_ASSERT_EQUAL_UINT32(1u, feed(&healthy, 1u));
}

static void test_measures_pulse_rates_across_its_range(void)
{
    static const struct {
        const char *name;
        float bpm;
    } rates[] = {
        {"30 bpm", 30.0f},   {"45 bpm", 45.0f},   {"72 bpm", 72.0f},
        {"180 bpm", 180.0f}, {"240 bpm", 240.0f},
    };

    for (size_t i = 0u; i < (sizeof rates / sizeof rates[0]); i++) {
        patient_t patient = healthy;

        patient.bpm = rates[i].bpm;
        start();
        (void)feed(&patient, SECONDS(4u));
        TEST_ASSERT_TRUE_MESSAGE(reading.valid, rates[i].name);
        /* Within 1 bpm, as promised. At 180 bpm a beat is 33.3 samples: that needs the
         * interpolation between whole lags. */
        TEST_ASSERT_UINT_WITHIN_MESSAGE(1u, (uint32_t)rates[i].bpm, reading.pulse_bpm,
                                        rates[i].name);
    }
}

/* Rather than a wrong number: a pulse faster than the range must not be read at twice its period,
 * as half its rate. */
static void test_reports_nothing_for_a_pulse_outside_its_range(void)
{
    static const struct {
        const char *name;
        float bpm;
    } rates[] = {{"29 bpm", 29.0f}, {"241 bpm", 241.0f}, {"300 bpm", 300.0f}};

    for (size_t i = 0u; i < (sizeof rates / sizeof rates[0]); i++) {
        patient_t patient = healthy;

        patient.bpm = rates[i].bpm;
        start();
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, feed(&patient, SECONDS(4u)), rates[i].name);
        TEST_ASSERT_FALSE_MESSAGE(reading.valid, rates[i].name);
    }
}

static void test_measures_spo2_across_its_range(void)
{
    static const struct {
        const char *name;
        uint8_t spo2;
    } levels[] = {{"70%", 70u}, {"85%", 85u}, {"97%", 97u}, {"100%", 100u}};

    for (size_t i = 0u; i < (sizeof levels / sizeof levels[0]); i++) {
        patient_t patient = healthy;

        patient.spo2 = (float)levels[i].spo2;
        start();
        (void)feed(&patient, SECONDS(4u));
        TEST_ASSERT_TRUE_MESSAGE(reading.valid, levels[i].name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(levels[i].spo2, reading.spo2_percent, levels[i].name);
    }
}

/* The calibration line passes 100% for small ratios: R = 0.24 would give 104%. */
static void test_never_reports_spo2_above_100_percent(void)
{
    patient_t patient = healthy;

    patient.spo2 = 104.0f;
    (void)feed(&patient, SECONDS(4u));
    TEST_ASSERT_EQUAL_UINT8(100u, reading.spo2_percent);
}

static void test_reports_nothing_for_a_pulse_too_weak_to_measure(void)
{
    patient_t patient = healthy;

    patient.infrared_pulse = 50.0f; /* 0.04% of the light level */
    TEST_ASSERT_EQUAL_UINT32(1u, feed(&patient, SECONDS(4u)));
    TEST_ASSERT_FALSE(reading.valid);
}

/* Noise as strong as a real pulse, but with no rhythm in it. */
static void test_reports_nothing_for_noise_without_a_rhythm(void)
{
    uint32_t seed = 1u;
    uint32_t readings = 0u;

    for (uint32_t n = 0u; n < SECONDS(4u); n++) {
        seed = (seed * 1664525u) + 1013904223u; /* a fixed-seed LCG, so the noise repeats */
        const uint32_t noise = seed >> 21;      /* 0 to 2047 */
        const oximetry_sample_t sample = {
            .red = (uint32_t)RED_LEVEL + noise,
            .infrared = (uint32_t)INFRARED_LEVEL + noise,
        };

        if (oximetry_add_sample(&oximetry, sample, &reading)) {
            readings++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(1u, readings);
    TEST_ASSERT_FALSE(reading.valid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reports_once_a_second_from_the_fourth_second);
    RUN_TEST(test_measures_pulse_rates_across_its_range);
    RUN_TEST(test_reports_nothing_for_a_pulse_outside_its_range);
    RUN_TEST(test_measures_spo2_across_its_range);
    RUN_TEST(test_never_reports_spo2_above_100_percent);
    RUN_TEST(test_reports_nothing_for_a_pulse_too_weak_to_measure);
    RUN_TEST(test_reports_nothing_for_noise_without_a_rhythm);
    return UNITY_END();
}
