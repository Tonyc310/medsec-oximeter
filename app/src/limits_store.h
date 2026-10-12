#ifndef LIMITS_STORE_H
#define LIMITS_STORE_H

#include "alarm.h"

/* Where the limits are kept, as alarm_limits_encode sends them. */
#define LIMITS_STORE_KEY "medsec/limits"

/** Loads the stored limits, or the defaults when none are stored or the stored ones aren't limits
 * a monitor would accept. Returns 0 or -errno from the storage. */
int limits_store_load(alarm_limits_t *limits);

/** Stores `limits` so they survive a reset. Returns 0 or -errno. */
int limits_store_save(const alarm_limits_t *limits);

#endif
