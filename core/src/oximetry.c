#include "oximetry.h"

#include <math.h>

/* SpO2 = 110 - 25 R, with R the ratio of ratios below: a textbook straight line. A real oximeter's
 * curve comes from a clinical study against arterial blood samples (ISO 80601-2-61), which this
 * project can't run, so its readings are illustrative. */
#define CALIBRATION_INTERCEPT 110.0f
#define CALIBRATION_SLOPE 25.0f
#define MAX_SPO2 100.0f

/* Below a 0.1% perfusion index (the pulse relative to the light level) the pulse is too weak to
 * measure reliably. */
#define MIN_PERFUSION 0.001f

/* How closely the signal a beat later must match itself (autocorrelation, 1 = identical) for the
 * pulse to count as steady. */
#define MIN_PERIODICITY 0.5f

/* The correlation peaks again at two and three beats, about as high as at one. The first peak
 * within this fraction of the highest is the beat, so a pulse isn't read as half its rate. */
#define PEAK_TOLERANCE 0.9f

#define MIN_PERIOD (60u * OXIMETRY_SAMPLE_RATE_HZ / OXIMETRY_MAX_PULSE_BPM)
#define SAMPLES_PER_MINUTE (60.0f * (float)OXIMETRY_SAMPLE_RATE_HZ)

_Static_assert(OXIMETRY_MAX_PERIOD < OXIMETRY_WINDOW_SAMPLES, "a beat fits in the window");
_Static_assert(MIN_PERIOD >= 1u, "the analysis reads one sample before the shortest beat");

typedef struct {
    int32_t mean;
    uint64_t squares; /* sum of squared deviations from the mean */
} channel_t;

/* Sample `i` of a full window, oldest first. Samples are clamped to 18 bits, so they fit. */
static int32_t sample_at(const oximetry_t *oximetry, const uint32_t *channel, uint32_t i)
{
    return (int32_t)channel[(oximetry->next + i) % OXIMETRY_WINDOW_SAMPLES];
}

/* 64-bit sums keep the statistics exact: 400 squared 18-bit deviations need about 45 bits. */
static channel_t measure(const oximetry_t *oximetry, const uint32_t *channel)
{
    const uint64_t samples = (uint64_t)OXIMETRY_WINDOW_SAMPLES;
    uint64_t sum = 0u;
    channel_t result = {0};

    for (uint32_t i = 0u; i < OXIMETRY_WINDOW_SAMPLES; i++) {
        sum += channel[i];
    }
    result.mean = (int32_t)((sum + (samples / 2u)) / samples);

    for (uint32_t i = 0u; i < OXIMETRY_WINDOW_SAMPLES; i++) {
        const int64_t deviation = (int64_t)sample_at(oximetry, channel, i) - result.mean;

        result.squares += (uint64_t)(deviation * deviation);
    }
    return result;
}

/* How alike the infrared signal is to itself `lag` samples later, from -1 to 1. */
static float correlation_at(const oximetry_t *oximetry, const channel_t *infrared, uint32_t lag)
{
    const uint32_t overlap = OXIMETRY_WINDOW_SAMPLES - lag;
    int64_t sum = 0;

    for (uint32_t i = 0u; i < overlap; i++) {
        const int64_t now = (int64_t)sample_at(oximetry, oximetry->infrared, i) - infrared->mean;
        const int64_t later =
            (int64_t)sample_at(oximetry, oximetry->infrared, i + lag) - infrared->mean;

        sum += now * later;
    }
    /* Both terms are per-sample averages, so overlaps of different lengths compare fairly. */
    return ((float)sum / (float)overlap) /
           ((float)infrared->squares / (float)OXIMETRY_WINDOW_SAMPLES);
}

/* The beat period in samples, between whole samples, or 0 when there's no steady pulse. */
static float beat_period(oximetry_t *oximetry, const channel_t *infrared)
{
    float *const correlation = oximetry->correlation;
    float highest = -1.0f;

    for (uint32_t lag = MIN_PERIOD - 1u; lag <= OXIMETRY_MAX_PERIOD + 1u; lag++) {
        correlation[lag] = correlation_at(oximetry, infrared, lag);
        if ((lag >= MIN_PERIOD) && (lag <= OXIMETRY_MAX_PERIOD) && (correlation[lag] > highest)) {
            highest = correlation[lag];
        }
    }
    if (highest < MIN_PERIODICITY) {
        return 0.0f;
    }

    for (uint32_t lag = MIN_PERIOD; lag <= OXIMETRY_MAX_PERIOD; lag++) {
        const float before = correlation[lag - 1u];
        const float peak = correlation[lag];
        const float after = correlation[lag + 1u];

        if ((peak >= (PEAK_TOLERANCE * highest)) && (peak >= before) && (peak >= after)) {
            /* A parabola through the peak and its neighbours places it between whole lags. */
            const float curvature = before - (2.0f * peak) + after;
            const float offset = (curvature < 0.0f) ? (0.5f * (before - after) / curvature) : 0.0f;

            return (float)lag + offset;
        }
    }
    return 0.0f;
}

static bool analyse(oximetry_t *oximetry, oximetry_reading_t *reading)
{
    const channel_t red = measure(oximetry, oximetry->red);
    const channel_t infrared = measure(oximetry, oximetry->infrared);

    if ((red.mean <= 0) || (infrared.mean <= 0) || (red.squares == 0u) ||
        (infrared.squares == 0u)) {
        return false;
    }

    const float perfusion =
        sqrtf((float)infrared.squares / (float)OXIMETRY_WINDOW_SAMPLES) / (float)infrared.mean;
    if (perfusion < MIN_PERFUSION) {
        return false;
    }

    const float period = beat_period(oximetry, &infrared);
    if (period <= 0.0f) {
        return false;
    }

    /* The ratio of ratios: the pulse in red light against the pulse in infrared, each relative to
     * its own level. Oxygenated blood absorbs less red, so the more oxygen, the smaller R. */
    const float ratio = sqrtf((float)red.squares / (float)infrared.squares) *
                        ((float)infrared.mean / (float)red.mean);
    const float spo2 = CALIBRATION_INTERCEPT - (CALIBRATION_SLOPE * ratio);
    if (!isfinite(spo2) || (spo2 < 0.0f)) {
        return false;
    }

    reading->spo2_percent = (uint8_t)lroundf(fminf(spo2, MAX_SPO2));
    reading->pulse_bpm = (uint16_t)lroundf(SAMPLES_PER_MINUTE / period);
    return true;
}

/* The window's samples are only read once it's full, so emptying it is resetting the counts. */
void oximetry_init(oximetry_t *oximetry)
{
    oximetry->next = 0u;
    oximetry->filled = 0u;
    oximetry->pending = 0u;
}

bool oximetry_add_sample(oximetry_t *oximetry, oximetry_sample_t sample,
                         oximetry_reading_t *reading)
{
    oximetry->red[oximetry->next] =
        (sample.red < OXIMETRY_MAX_COUNTS) ? sample.red : OXIMETRY_MAX_COUNTS;
    oximetry->infrared[oximetry->next] =
        (sample.infrared < OXIMETRY_MAX_COUNTS) ? sample.infrared : OXIMETRY_MAX_COUNTS;
    oximetry->next = (oximetry->next + 1u) % OXIMETRY_WINDOW_SAMPLES;
    if (oximetry->filled < OXIMETRY_WINDOW_SAMPLES) {
        oximetry->filled++;
    }
    oximetry->pending++;

    if ((oximetry->filled < OXIMETRY_WINDOW_SAMPLES) ||
        (oximetry->pending < OXIMETRY_SAMPLE_RATE_HZ)) {
        return false;
    }
    oximetry->pending = 0u;

    reading->valid = analyse(oximetry, reading);
    if (!reading->valid) {
        reading->spo2_percent = 0u;
        reading->pulse_bpm = 0u;
    }
    return true;
}
