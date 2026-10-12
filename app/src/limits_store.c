#include "limits_store.h"

#include <zephyr/settings/settings.h>

/* Stored limits pass the same checks as limits sent over the air: flash that holds anything else
 * leaves the defaults in force. */
static int on_stored(const char *key, size_t length, settings_read_cb read, void *read_arg,
                     void *param)
{
    uint8_t value[ALARM_LIMITS_SIZE];

    ARG_UNUSED(key);
    /* The decoder refuses a value of the wrong size; this makes sure it never sees bytes a short
     * read left unset (CERT EXP33-C). */
    if (read(read_arg, value, sizeof value) == (ssize_t)length) {
        (void)alarm_limits_decode(value, length, param);
    }
    return 0;
}

int limits_store_load(alarm_limits_t *limits)
{
    const int err = settings_subsys_init();

    *limits = alarm_default_limits();
    if (err != 0) {
        return err;
    }
    return settings_load_subtree_direct(LIMITS_STORE_KEY, on_stored, limits);
}

int limits_store_save(const alarm_limits_t *limits)
{
    uint8_t value[ALARM_LIMITS_SIZE];

    alarm_limits_encode(limits, value);
    return settings_save_one(LIMITS_STORE_KEY, value, sizeof value);
}
