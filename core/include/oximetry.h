#ifndef OXIMETRY_H
#define OXIMETRY_H

#include <stdbool.h>
#include <stdint.h>

/* The rate the analysis is built for; the firmware checks its sensor runs at it. */
#define OXIMETRY_SAMPLE_RATE_HZ 100u

/* Samples are 18-bit ADC counts; larger values are clamped to this. */
#define OXIMETRY_MAX_COUNTS ((1u << 18) - 1u)

/* Pulse rates outside this range are reported as no reading. */
#define OXIMETRY_MIN_PULSE_BPM 30u
#define OXIMETRY_MAX_PULSE_BPM 240u

/* Four seconds: at least two beats even at the slowest pulse rate reported. */
#define OXIMETRY_WINDOW_SAMPLES (4u * OXIMETRY_SAMPLE_RATE_HZ)

/* The longest beat, in samples, the analysis looks for. */
#define OXIMETRY_MAX_PERIOD (60u * OXIMETRY_SAMPLE_RATE_HZ / OXIMETRY_MIN_PULSE_BPM)

/* One sample of each LED, in a struct so a call can't pass red and infrared the wrong way round:
 * swapped, they give a wrong SpO2 with nothing to show for it. */
typedef struct {
    uint32_t red;
    uint32_t infrared;
} oximetry_sample_t;

typedef struct {
    bool valid; /* false when no steady pulse was found: show no numbers rather than wrong ones */
    uint8_t spo2_percent;
    uint16_t pulse_bpm;
} oximetry_reading_t;

/* The last four seconds of samples and the analysis's working space. The members are private. */
typedef struct {
    uint32_t red[OXIMETRY_WINDOW_SAMPLES];
    uint32_t infrared[OXIMETRY_WINDOW_SAMPLES];
    uint32_t next;    /* where the next sample goes */
    uint32_t filled;  /* samples held, up to OXIMETRY_WINDOW_SAMPLES */
    uint32_t pending; /* samples added since the last reading */
    float correlation[OXIMETRY_MAX_PERIOD + 2u];
} oximetry_t;

/** Empties the window; readings start once it holds four seconds of samples. */
void oximetry_init(oximetry_t *oximetry);

/**
 * Adds one sample, taken at OXIMETRY_SAMPLE_RATE_HZ. Once a second it analyses the window,
 * fills `reading` and returns true.
 */
bool oximetry_add_sample(oximetry_t *oximetry, oximetry_sample_t sample,
                         oximetry_reading_t *reading);

#endif
