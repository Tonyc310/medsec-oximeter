#ifndef ALARM_H
#define ALARM_H

#include "oximetry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Alarm limits as sent over Bluetooth: SpO2 low limit, then pulse low and high limits, the pulse
 * limits little-endian. */
#define ALARM_LIMITS_SIZE 5u

/* Bits of the alarm state: which conditions are present. */
#define ALARM_SPO2_LOW 0x01u
#define ALARM_PULSE_LOW 0x02u
#define ALARM_PULSE_HIGH 0x04u
#define ALARM_NO_READING 0x08u /* a technical alarm: the sensor isn't getting a pulse */

typedef struct {
    uint8_t spo2_low_percent; /* alarm when SpO2 falls below this */
    uint16_t pulse_low_bpm;   /* alarm when the pulse rate falls below this */
    uint16_t pulse_high_bpm;  /* alarm when it rises above this */
} alarm_limits_t;

/** Typical monitor defaults: SpO2 under 90%, pulse under 50 or over 120 bpm. */
alarm_limits_t alarm_default_limits(void);

/** The alarm conditions a reading meets under `limits`, as ALARM_* bits; 0 when there are none. */
uint8_t alarm_check(const alarm_limits_t *limits, const oximetry_reading_t *reading);

/** Encodes limits for sending. */
void alarm_limits_encode(const alarm_limits_t *limits, uint8_t out[ALARM_LIMITS_SIZE]);

/**
 * Decodes limits received over the air. Returns false, and leaves `limits` untouched, for a wrong
 * length or settings a monitor wouldn't accept, such as a low pulse limit above the high one.
 */
bool alarm_limits_decode(const uint8_t *data, size_t length, alarm_limits_t *limits);

#endif
