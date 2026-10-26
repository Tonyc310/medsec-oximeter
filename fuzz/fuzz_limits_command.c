/* Any bytes, as an alarm-limits command reaching the oximeter: the limits in force afterwards are
 * either the ones before or ones a monitor accepts. */
#include "alarm.h"
#include "limits_auth.h"
#include "oximetry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The settings a monitor accepts, stated here rather than taken from alarm.c so a mistake there
 * shows. */
#define LOWEST_SPO2_LIMIT 50u
#define HIGHEST_SPO2_LIMIT 99u

/* Every tag passes, as if the sender held the limits key: a compromised backend's worst. Otherwise
 * the fuzzer would never get past the tag to the limits behind it. */
static bool accept_any_tag(const uint8_t *message, size_t length, const uint8_t *tag)
{
    (void)message;
    (void)length;
    (void)tag;
    return true;
}

static bool fixed_random(uint8_t *out, size_t length)
{
    (void)memset(out, 0x5A, length);
    return true;
}

static const limits_auth_crypto_t crypto = {.random = fixed_random, .verify = accept_any_tag};

static bool acceptable(const alarm_limits_t *limits)
{
    return (limits->spo2_low_percent >= LOWEST_SPO2_LIMIT) &&
           (limits->spo2_low_percent <= HIGHEST_SPO2_LIMIT) &&
           (limits->pulse_low_bpm >= OXIMETRY_MIN_PULSE_BPM) &&
           (limits->pulse_high_bpm <= OXIMETRY_MAX_PULSE_BPM) &&
           (limits->pulse_low_bpm < limits->pulse_high_bpm);
}

static bool same(const alarm_limits_t *a, const alarm_limits_t *b)
{
    return (a->spo2_low_percent == b->spo2_low_percent) && (a->pulse_low_bpm == b->pulse_low_bpm) &&
           (a->pulse_high_bpm == b->pulse_high_bpm);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    limits_auth_t auth;
    const alarm_limits_t before = alarm_default_limits();
    alarm_limits_t limits = before;

    limits_auth_init(&auth, &crypto);
    const bool accepted = limits_auth_apply(&auth, data, size, &limits) == LIMITS_AUTH_ACCEPTED;

    if (accepted ? !acceptable(&limits) : !same(&limits, &before)) {
        abort();
    }
    return 0;
}
