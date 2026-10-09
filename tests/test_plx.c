#include "plx.h"
#include "unity.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    uint8_t bytes[PLX_CONTINUOUS_SIZE + 1u];
    size_t length;
} measurement_t;

static const oximetry_reading_t last_good = {.valid = true, .spo2_percent = 97u, .pulse_bpm = 72u};
static oximetry_reading_t decoded;

void setUp(void)
{
    decoded = last_good;
}

void tearDown(void)
{
}

static bool decode(const measurement_t *m)
{
    return plx_decode_continuous(m->bytes, m->length, &decoded);
}

/* Flags, then SpO2 and pulse rate as little-endian SFLOATs with a zero exponent. */
static void test_encodes_a_reading_with_no_optional_fields(void)
{
    static const oximetry_reading_t reading = {
        .valid = true, .spo2_percent = 94u, .pulse_bpm = 90u};
    static const uint8_t expected[PLX_CONTINUOUS_SIZE] = {0x00, 0x5E, 0x00, 0x5A, 0x00};
    uint8_t encoded[PLX_CONTINUOUS_SIZE];

    plx_encode_continuous(&reading, encoded);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, encoded, PLX_CONTINUOUS_SIZE);
}

static void test_encodes_no_reading_as_not_a_number(void)
{
    static const oximetry_reading_t none = {.valid = false};
    static const uint8_t expected[PLX_CONTINUOUS_SIZE] = {0x00, 0xFF, 0x07, 0xFF, 0x07};
    uint8_t encoded[PLX_CONTINUOUS_SIZE];

    plx_encode_continuous(&none, encoded);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, encoded, PLX_CONTINUOUS_SIZE);
}

static void test_decodes_readings_up_to_the_highest_an_oximeter_reports(void)
{
    static const struct {
        measurement_t measurement;
        uint8_t spo2;
        uint16_t pulse;
    } cases[] = {
        {{"typical", {0x00, 0x5E, 0x00, 0x5A, 0x00}, 5u}, 94u, 90u},
        {{"highest", {0x00, 0x64, 0x00, 0x2C, 0x01}, 5u}, 100u, 300u},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        const char *const name = cases[i].measurement.name;

        TEST_ASSERT_TRUE_MESSAGE(decode(&cases[i].measurement), name);
        TEST_ASSERT_TRUE_MESSAGE(decoded.valid, name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(cases[i].spo2, decoded.spo2_percent, name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(cases[i].pulse, decoded.pulse_bpm, name);
    }
}

static void test_decodes_a_special_value_in_either_field_as_no_reading(void)
{
    static const measurement_t cases[] = {
        {"SpO2 not a number", {0x00, 0xFF, 0x07, 0x5A, 0x00}, 5u},
        {"pulse not a number", {0x00, 0x5E, 0x00, 0xFF, 0x07}, 5u},
        {"not representable", {0x00, 0x00, 0x08, 0x5A, 0x00}, 5u},
        {"plus infinity", {0x00, 0xFE, 0x07, 0x5A, 0x00}, 5u},
        {"minus infinity", {0x00, 0x02, 0x08, 0x5A, 0x00}, 5u},
        {"reserved", {0x00, 0x01, 0x08, 0x5A, 0x00}, 5u},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        decoded = last_good;
        TEST_ASSERT_TRUE_MESSAGE(decode(&cases[i]), cases[i].name);
        TEST_ASSERT_FALSE_MESSAGE(decoded.valid, cases[i].name);
    }
}

static void test_decodes_values_with_a_power_of_ten(void)
{
    /* SpO2 975 x 10^-1 rounds half away from zero to 98; pulse 9 x 10^1 is 90. */
    static const measurement_t scaled = {"scaled", {0x00, 0xCF, 0xF3, 0x09, 0x10}, 5u};

    TEST_ASSERT_TRUE(decode(&scaled));
    TEST_ASSERT_EQUAL_UINT8(98u, decoded.spo2_percent);
    TEST_ASSERT_EQUAL_UINT16(90u, decoded.pulse_bpm);
}

static void test_skips_the_optional_fields_its_flags_announce(void)
{
    /* All five follow the reading: SpO2PR-Fast and SpO2PR-Slow (4 bytes each), Measurement
     * Status (2), Device and Sensor Status (3) and Pulse Amplitude Index (2). */
    static const uint8_t all_fields[] = {0x1F, 0x5E, 0x00, 0x5A, 0x00, 0, 0, 0, 0, 0,
                                         0,    0,    0,    0,    0,    0, 0, 0, 0, 0};

    TEST_ASSERT_TRUE(plx_decode_continuous(all_fields, sizeof all_fields, &decoded));
    TEST_ASSERT_EQUAL_UINT8(94u, decoded.spo2_percent);
    TEST_ASSERT_EQUAL_UINT16(90u, decoded.pulse_bpm);
}

static void test_rejects_a_length_that_does_not_match_its_flags(void)
{
    static const measurement_t cases[] = {
        {"too short", {0x00, 0x5E, 0x00, 0x5A}, 4u},
        {"a byte too long", {0x00, 0x5E, 0x00, 0x5A, 0x00, 0x00}, 6u},
        {"announced field missing", {0x04, 0x5E, 0x00, 0x5A, 0x00}, 5u},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(decode(&cases[i]), cases[i].name);
    }
}

/* A device that sets them speaks a version of the standard this decoder doesn't know. */
static void test_rejects_reserved_flags(void)
{
    static const measurement_t cases[] = {
        {"flag bit 5", {0x20, 0x5E, 0x00, 0x5A, 0x00}, 5u},
        {"flag bit 6", {0x40, 0x5E, 0x00, 0x5A, 0x00}, 5u},
        {"flag bit 7", {0x80, 0x5E, 0x00, 0x5A, 0x00}, 5u},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(decode(&cases[i]), cases[i].name);
    }
}

static void test_rejects_values_no_oximeter_reports(void)
{
    static const measurement_t cases[] = {
        {"SpO2 of 101%", {0x00, 0x65, 0x00, 0x5A, 0x00}, 5u},
        {"pulse of 301 bpm", {0x00, 0x5E, 0x00, 0x2D, 0x01}, 5u},
        {"negative SpO2", {0x00, 0xFF, 0x0F, 0x5A, 0x00}, 5u},
        {"negative pulse", {0x00, 0x5E, 0x00, 0xFF, 0x0F}, 5u},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(decode(&cases[i]), cases[i].name);
    }
}

static void test_keeps_the_last_reading_when_it_rejects_a_measurement(void)
{
    static const measurement_t over_100_percent = {
        "SpO2 of 101%", {0x00, 0x65, 0x00, 0x5A, 0x00}, 5u};

    TEST_ASSERT_FALSE(decode(&over_100_percent));
    TEST_ASSERT_TRUE(decoded.valid);
    TEST_ASSERT_EQUAL_UINT8(97u, decoded.spo2_percent);
    TEST_ASSERT_EQUAL_UINT16(72u, decoded.pulse_bpm);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encodes_a_reading_with_no_optional_fields);
    RUN_TEST(test_encodes_no_reading_as_not_a_number);
    RUN_TEST(test_decodes_readings_up_to_the_highest_an_oximeter_reports);
    RUN_TEST(test_decodes_a_special_value_in_either_field_as_no_reading);
    RUN_TEST(test_decodes_values_with_a_power_of_ten);
    RUN_TEST(test_skips_the_optional_fields_its_flags_announce);
    RUN_TEST(test_rejects_a_length_that_does_not_match_its_flags);
    RUN_TEST(test_rejects_reserved_flags);
    RUN_TEST(test_rejects_values_no_oximeter_reports);
    RUN_TEST(test_keeps_the_last_reading_when_it_rejects_a_measurement);
    return UNITY_END();
}
