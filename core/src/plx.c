#include "plx.h"

/* SFLOAT values with special meanings, all with a zero exponent. */
#define SFLOAT_NAN 0x07FFu
#define SFLOAT_NRES 0x0800u /* not representable at this resolution */
#define SFLOAT_PLUS_INFINITY 0x07FEu
#define SFLOAT_MINUS_INFINITY 0x0802u
#define SFLOAT_RESERVED 0x0801u
#define SFLOAT_MANTISSA_MASK 0x0FFFu
#define SFLOAT_MANTISSA_SIGN 0x0800u
#define SFLOAT_EXPONENT_SHIFT 12u
#define SFLOAT_EXPONENT_SIGN 0x8u

/* Bits 5-7 of the flags are reserved; a device that sets them speaks a version we don't know. */
#define RESERVED_FLAGS 0xE0u

/* The highest pulse rate any oximeter reports; anything above is a malformed measurement. */
#define MAX_PULSE_BPM 300
#define MAX_SPO2_PERCENT 100

_Static_assert(OXIMETRY_MAX_PULSE_BPM < SFLOAT_PLUS_INFINITY,
               "a pulse rate fits the mantissa with a zero exponent");

/* Each optional field's presence flag and its size in bytes, in the order they follow. */
static const struct {
    uint8_t flag;
    uint8_t size;
} optional_fields[] = {
    {0x01u, 4u}, /* SpO2PR-Fast: SpO2 and pulse rate */
    {0x02u, 4u}, /* SpO2PR-Slow */
    {0x04u, 2u}, /* Measurement Status */
    {0x08u, 3u}, /* Device and Sensor Status */
    {0x10u, 2u}, /* Pulse Amplitude Index */
};

static void put_le16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)(value >> 8);
}

static uint16_t get_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8));
}

static bool is_special(uint16_t raw)
{
    return (raw == SFLOAT_NAN) || (raw == SFLOAT_NRES) || (raw == SFLOAT_PLUS_INFINITY) ||
           (raw == SFLOAT_MINUS_INFINITY) || (raw == SFLOAT_RESERVED);
}

/* The value rounded to a whole number, half away from zero. Only for non-special values. */
static int64_t sfloat_to_integer(uint16_t raw)
{
    const uint16_t mantissa_bits = raw & SFLOAT_MANTISSA_MASK;
    const uint16_t exponent_bits = (uint16_t)(raw >> SFLOAT_EXPONENT_SHIFT);
    /* Both fields are two's complement: sign-extend them by hand. */
    const int64_t mantissa = ((mantissa_bits & SFLOAT_MANTISSA_SIGN) != 0u)
                                 ? (int64_t)mantissa_bits - 0x1000
                                 : (int64_t)mantissa_bits;
    const int32_t exponent = ((exponent_bits & SFLOAT_EXPONENT_SIGN) != 0u)
                                 ? (int32_t)exponent_bits - 0x10
                                 : (int32_t)exponent_bits;
    int64_t scale = 1;

    for (int32_t i = 0; i < ((exponent < 0) ? -exponent : exponent); i++) {
        scale *= 10;
    }
    if (exponent >= 0) {
        return mantissa * scale; /* at most 2047 * 10^7: well inside 64 bits */
    }
    return (mantissa + ((mantissa >= 0) ? (scale / 2) : -(scale / 2))) / scale;
}

void plx_encode_continuous(const oximetry_reading_t *reading, uint8_t out[PLX_CONTINUOUS_SIZE])
{
    out[0] = 0u; /* no optional fields */
    put_le16(&out[1], (uint16_t)(reading->valid ? reading->spo2_percent : SFLOAT_NAN));
    put_le16(&out[3], (uint16_t)(reading->valid ? reading->pulse_bpm : SFLOAT_NAN));
}

bool plx_decode_continuous(const uint8_t *data, size_t length, oximetry_reading_t *reading)
{
    size_t expected = PLX_CONTINUOUS_SIZE;

    if ((data == NULL) || (reading == NULL) || (length < PLX_CONTINUOUS_SIZE) ||
        ((data[0] & RESERVED_FLAGS) != 0u)) {
        return false;
    }
    for (size_t i = 0u; i < (sizeof optional_fields / sizeof optional_fields[0]); i++) {
        if ((data[0] & optional_fields[i].flag) != 0u) {
            expected += optional_fields[i].size;
        }
    }
    if (length != expected) {
        return false;
    }

    const uint16_t spo2_raw = get_le16(&data[1]);
    const uint16_t pulse_raw = get_le16(&data[3]);

    if (is_special(spo2_raw) || is_special(pulse_raw)) {
        *reading = (oximetry_reading_t){.valid = false};
        return true;
    }

    const int64_t spo2 = sfloat_to_integer(spo2_raw);
    const int64_t pulse = sfloat_to_integer(pulse_raw);

    if ((spo2 < 0) || (spo2 > MAX_SPO2_PERCENT) || (pulse < 0) || (pulse > MAX_PULSE_BPM)) {
        return false;
    }
    *reading = (oximetry_reading_t){
        .valid = true,
        .spo2_percent = (uint8_t)spo2,
        .pulse_bpm = (uint16_t)pulse,
    };
    return true;
}
