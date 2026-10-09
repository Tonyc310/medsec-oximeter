#include "plx.h"
#include "unity.h"

#include <stdint.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_encodes_readings_as_the_standard_bytes_and_decodes_them_back(void)
{
    static const oximetry_reading_t reading = {
        .valid = true, .spo2_percent = 94u, .pulse_bpm = 90u};
    static const oximetry_reading_t none = {.valid = false};
    /* Flags, then SpO2 and pulse rate as little-endian SFLOATs; 0x07FF is "not a number". */
    static const uint8_t reading_bytes[PLX_CONTINUOUS_SIZE] = {0x00, 0x5E, 0x00, 0x5A, 0x00};
    static const uint8_t none_bytes[PLX_CONTINUOUS_SIZE] = {0x00, 0xFF, 0x07, 0xFF, 0x07};
    uint8_t encoded[PLX_CONTINUOUS_SIZE];
    oximetry_reading_t decoded;

    plx_encode_continuous(&reading, encoded);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(reading_bytes, encoded, PLX_CONTINUOUS_SIZE);
    TEST_ASSERT_TRUE(plx_decode_continuous(encoded, sizeof encoded, &decoded));
    TEST_ASSERT_TRUE(decoded.valid);
    TEST_ASSERT_EQUAL_UINT8(94u, decoded.spo2_percent);
    TEST_ASSERT_EQUAL_UINT16(90u, decoded.pulse_bpm);

    plx_encode_continuous(&none, encoded);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(none_bytes, encoded, PLX_CONTINUOUS_SIZE);
    TEST_ASSERT_TRUE(plx_decode_continuous(encoded, sizeof encoded, &decoded));
    TEST_ASSERT_FALSE(decoded.valid);
}

static void test_decodes_other_devices_and_rejects_malformed_measurements(void)
{
    /* SpO2 975 x 10^-1 (exponent 0xF) rounds to 98; a Measurement Status field follows. */
    static const uint8_t other_device[] = {0x04, 0xCF, 0xF3, 0x48, 0x00, 0x00, 0x00};
    static const uint8_t too_short[] = {0x00, 0x5E, 0x00, 0x5A};
    static const uint8_t field_missing[] = {0x04, 0x5E, 0x00, 0x5A, 0x00};
    static const uint8_t reserved_flag[] = {0x20, 0x5E, 0x00, 0x5A, 0x00};
    static const uint8_t over_100_percent[] = {0x00, 0x65, 0x00, 0x5A, 0x00};
    oximetry_reading_t decoded;

    TEST_ASSERT_TRUE(plx_decode_continuous(other_device, sizeof other_device, &decoded));
    TEST_ASSERT_TRUE(decoded.valid);
    TEST_ASSERT_EQUAL_UINT8(98u, decoded.spo2_percent);
    TEST_ASSERT_EQUAL_UINT16(72u, decoded.pulse_bpm);

    TEST_ASSERT_FALSE(plx_decode_continuous(too_short, sizeof too_short, &decoded));
    TEST_ASSERT_FALSE(plx_decode_continuous(field_missing, sizeof field_missing, &decoded));
    TEST_ASSERT_FALSE(plx_decode_continuous(reserved_flag, sizeof reserved_flag, &decoded));
    TEST_ASSERT_FALSE(plx_decode_continuous(over_100_percent, sizeof over_100_percent, &decoded));
    /* A rejected measurement leaves the last good reading in place. */
    TEST_ASSERT_EQUAL_UINT8(98u, decoded.spo2_percent);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encodes_readings_as_the_standard_bytes_and_decodes_them_back);
    RUN_TEST(test_decodes_other_devices_and_rejects_malformed_measurements);
    return UNITY_END();
}
