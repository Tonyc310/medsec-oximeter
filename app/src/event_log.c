#include "event_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

/* Each record is kept under its slot's key, medsec/log/0 to medsec/log/63, so a new record
 * overwrites the oldest in flash as it does in the log. */
LOG_MODULE_REGISTER(event_log, LOG_LEVEL_INF);

#define LOG_KEY "medsec/log"
#define BOOT_KEY "medsec/boot"

/* Events come from start-up, the Bluetooth thread and the supervisor. */
static K_MUTEX_DEFINE(lock);
static security_log_t security;
static uint16_t boot;
/* What flash holds, by slot, so reading the log needs no flash access. */
static security_record_t records[SECURITY_LOG_CAPACITY];

/* Only a complete read is decoded (CERT EXP33-C); the decoder refuses anything malformed. */
static int on_record(const char *key, size_t length, settings_read_cb read, void *read_arg,
                     void *param)
{
    uint8_t stored[SECURITY_LOG_RECORD_SIZE];
    security_record_t record;

    ARG_UNUSED(key);
    ARG_UNUSED(param);
    if ((read(read_arg, stored, sizeof stored) == (ssize_t)length) &&
        security_log_decode(stored, length, &record)) {
        security_log_restore(&security, stored, length);
        records[(record.sequence - 1u) % SECURITY_LOG_CAPACITY] = record;
    }
    return 0;
}

static int on_boot(const char *key, size_t length, settings_read_cb read, void *read_arg,
                   void *param)
{
    uint16_t stored;

    ARG_UNUSED(key);
    ARG_UNUSED(param);
    if (read(read_arg, &stored, sizeof stored) == (ssize_t)length) {
        boot = stored;
    }
    return 0;
}

static int load(void)
{
    security_log_init(&security);
    (void)memset(records, 0, sizeof records);
    boot = 0u;

    int err = settings_load_subtree_direct(LOG_KEY, on_record, NULL);

    if (err == 0) {
        err = settings_load_subtree_direct(BOOT_KEY, on_boot, NULL);
    }
    if (err == 0) {
        boot++;
        err = settings_save_one(BOOT_KEY, &boot, sizeof boot);
    }
    return err;
}

int event_log_start(void)
{
    uint32_t cause = 0u;
    int err = settings_subsys_init();

    if (err != 0) {
        return err;
    }
    (void)k_mutex_lock(&lock, K_FOREVER);
    err = load();
    k_mutex_unlock(&lock);
    if (err != 0) {
        return err;
    }

    /* The cause stays set until cleared: clearing it makes the next start-up report its own. */
    if (hwinfo_get_reset_cause(&cause) == 0) {
        (void)hwinfo_clear_reset_cause();
    }
    const uint8_t detail[] = {(uint8_t)cause, (uint8_t)(cause >> 8), (uint8_t)(cause >> 16),
                              (uint8_t)(cause >> 24)};

    return event_log_record(SECURITY_EVENT_START, detail, sizeof detail);
}

int event_log_record(security_event_t event, const void *detail, size_t length)
{
    security_record_t record = {.uptime_ms = k_uptime_get_32(), .event = (uint8_t)event};
    uint8_t encoded[SECURITY_LOG_RECORD_SIZE];
    char key[sizeof(LOG_KEY "/63")];

    if (length > SECURITY_LOG_DETAIL_SIZE) {
        return -EINVAL;
    }
    if (length > 0u) {
        (void)memcpy(record.detail, detail, length);
    }

    (void)k_mutex_lock(&lock, K_FOREVER);
    const uint32_t slot = security_log_next(&security, &record);

    record.boot = boot;
    records[slot] = record;
    security_log_encode(&record, encoded);
    (void)snprintf(key, sizeof key, LOG_KEY "/%u", (unsigned int)slot);

    const int err = settings_save_one(key, encoded, sizeof encoded);

    k_mutex_unlock(&lock);
    /* Callers carry on: the event still reaches the console, and the gap in numbering shows. */
    if (err != 0) {
        LOG_WRN("security event %u not saved (%d)", (unsigned int)event, err);
    }
    return err;
}

size_t event_log_read(uint8_t *out, size_t size)
{
    size_t copied = 0u;

    (void)k_mutex_lock(&lock, K_FOREVER);
    const uint32_t count = security_log_count(&security);

    for (uint32_t i = 0u; (i < count) && ((copied + SECURITY_LOG_RECORD_SIZE) <= size); i++) {
        security_log_encode(&records[security_log_slot(&security, i)], &out[copied]);
        copied += SECURITY_LOG_RECORD_SIZE;
    }
    k_mutex_unlock(&lock);
    return copied;
}
