#include "limits_store.h"

#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

static void assert_defaults(const alarm_limits_t *limits)
{
    const alarm_limits_t defaults = alarm_default_limits();

    zassert_equal(limits->spo2_low_percent, defaults.spo2_low_percent);
    zassert_equal(limits->pulse_low_bpm, defaults.pulse_low_bpm);
    zassert_equal(limits->pulse_high_bpm, defaults.pulse_high_bpm);
}

ZTEST(limits_store, test_starts_with_the_defaults_when_nothing_is_stored)
{
    alarm_limits_t limits = {0};

    zassert_ok(limits_store_load(&limits));
    assert_defaults(&limits);
}

/* A load reads flash afresh, as the oximeter does when it starts. */
ZTEST(limits_store, test_restores_the_last_saved_limits)
{
    const alarm_limits_t raised = {
        .spo2_low_percent = 95u, .pulse_low_bpm = 40u, .pulse_high_bpm = 130u};
    alarm_limits_t limits = {0};

    zassert_ok(limits_store_save(&raised));
    zassert_ok(limits_store_load(&limits));
    zassert_equal(limits.spo2_low_percent, 95u);
    zassert_equal(limits.pulse_low_bpm, 40u);
    zassert_equal(limits.pulse_high_bpm, 130u);
}

/* Flash holding anything but limits a monitor would accept, whatever put it there. */
ZTEST(limits_store, test_keeps_the_defaults_when_the_stored_value_is_not_valid)
{
    static const struct {
        const char *name;
        uint8_t value[ALARM_LIMITS_SIZE + 1u];
        size_t length;
    } cases[] = {
        {"too short", {95u, 40u, 0u, 130u}, 4u},
        {"too long", {95u, 40u, 0u, 130u, 0u, 0u}, 6u},
        {"SpO2 limit of 100%", {100u, 40u, 0u, 130u, 0u}, 5u},
    };

    for (size_t i = 0u; i < ARRAY_SIZE(cases); i++) {
        alarm_limits_t limits = {0};

        zassert_ok(settings_save_one(LIMITS_STORE_KEY, cases[i].value, cases[i].length));
        zassert_ok(limits_store_load(&limits), "%s", cases[i].name);
        assert_defaults(&limits);
    }
}

static void *start_settings(void)
{
    zassert_ok(settings_subsys_init());
    return NULL;
}

/* Every test starts with nothing stored. */
static void forget_limits(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)settings_delete(LIMITS_STORE_KEY);
}

ZTEST_SUITE(limits_store, NULL, start_settings, forget_limits, NULL, NULL);
