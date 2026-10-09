#include "alarm.h"

/* The settings range a bedside monitor typically offers. They keep out nonsense, not malice:
 * an SpO2 limit of 50% is a legitimate setting and also silences a hypoxia alarm, so stopping a
 * forged write is a job for authentication, not for these checks. */
#define MIN_SPO2_LIMIT 50u
#define MAX_SPO2_LIMIT 99u
#define MIN_PULSE_LIMIT OXIMETRY_MIN_PULSE_BPM
#define MAX_PULSE_LIMIT OXIMETRY_MAX_PULSE_BPM

#define DEFAULT_SPO2_LOW 90u
#define DEFAULT_PULSE_LOW 50u
#define DEFAULT_PULSE_HIGH 120u

alarm_limits_t alarm_default_limits(void)
{
    return (alarm_limits_t){
        .spo2_low_percent = DEFAULT_SPO2_LOW,
        .pulse_low_bpm = DEFAULT_PULSE_LOW,
        .pulse_high_bpm = DEFAULT_PULSE_HIGH,
    };
}

uint8_t alarm_check(const alarm_limits_t *limits, const oximetry_reading_t *reading)
{
    uint8_t alarms = 0u;

    if (!reading->valid) {
        return ALARM_NO_READING;
    }
    if (reading->spo2_percent < limits->spo2_low_percent) {
        alarms |= ALARM_SPO2_LOW;
    }
    if (reading->pulse_bpm < limits->pulse_low_bpm) {
        alarms |= ALARM_PULSE_LOW;
    }
    if (reading->pulse_bpm > limits->pulse_high_bpm) {
        alarms |= ALARM_PULSE_HIGH;
    }
    return alarms;
}

void alarm_limits_encode(const alarm_limits_t *limits, uint8_t out[ALARM_LIMITS_SIZE])
{
    out[0] = limits->spo2_low_percent;
    out[1] = (uint8_t)(limits->pulse_low_bpm & 0xFFu);
    out[2] = (uint8_t)(limits->pulse_low_bpm >> 8);
    out[3] = (uint8_t)(limits->pulse_high_bpm & 0xFFu);
    out[4] = (uint8_t)(limits->pulse_high_bpm >> 8);
}

bool alarm_limits_decode(const uint8_t *data, size_t length, alarm_limits_t *limits)
{
    if ((data == NULL) || (limits == NULL) || (length != ALARM_LIMITS_SIZE)) {
        return false;
    }

    const alarm_limits_t decoded = {
        .spo2_low_percent = data[0],
        .pulse_low_bpm = (uint16_t)((uint16_t)data[1] | (uint16_t)((uint16_t)data[2] << 8)),
        .pulse_high_bpm = (uint16_t)((uint16_t)data[3] | (uint16_t)((uint16_t)data[4] << 8)),
    };

    if ((decoded.spo2_low_percent < MIN_SPO2_LIMIT) ||
        (decoded.spo2_low_percent > MAX_SPO2_LIMIT) || (decoded.pulse_low_bpm < MIN_PULSE_LIMIT) ||
        (decoded.pulse_high_bpm > MAX_PULSE_LIMIT) ||
        (decoded.pulse_low_bpm >= decoded.pulse_high_bpm)) {
        return false;
    }
    *limits = decoded;
    return true;
}
