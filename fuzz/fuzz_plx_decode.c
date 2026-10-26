/* Any bytes, as a PLX Continuous Measurement reaching the hub: a reading it takes is in range, and
 * one it refuses leaves the last reading as it was. */
#include "oximetry.h"
#include "plx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* The ranges plx.h promises, stated here rather than taken from plx.c so a mistake there shows. */
#define HIGHEST_SPO2_PERCENT 100u
#define HIGHEST_PULSE_BPM 300u

static bool in_range(const oximetry_reading_t *reading)
{
    return !reading->valid || ((reading->spo2_percent <= HIGHEST_SPO2_PERCENT) &&
                               (reading->pulse_bpm <= HIGHEST_PULSE_BPM));
}

static bool same(const oximetry_reading_t *a, const oximetry_reading_t *b)
{
    return (a->valid == b->valid) && (a->spo2_percent == b->spo2_percent) &&
           (a->pulse_bpm == b->pulse_bpm);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const oximetry_reading_t before = {.valid = true, .spo2_percent = 97u, .pulse_bpm = 72u};
    oximetry_reading_t reading = before;
    const bool accepted = plx_decode_continuous(data, size, &reading);

    if (accepted ? !in_range(&reading) : !same(&reading, &before)) {
        abort();
    }
    return 0;
}
