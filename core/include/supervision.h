#ifndef SUPERVISION_H
#define SUPERVISION_H

#include <stdbool.h>
#include <stdint.h>

/* The sensor counts as stopped after a second without a sample: 100 samples missed. Times are a
 * millisecond counter that may wrap, such as Zephyr's 32-bit uptime. */
#define SUPERVISION_STALL_MS 1000u

/* When the last sample arrived. The members are private. */
typedef struct {
    uint32_t last_sample_ms;
} supervision_t;

/** Starts supervising at `now_ms`. */
void supervision_init(supervision_t *supervision, uint32_t now_ms);

/** Notes a sample arriving at `now_ms`. */
void supervision_sample(supervision_t *supervision, uint32_t now_ms);

/** True when no sample has arrived for SUPERVISION_STALL_MS or more. */
bool supervision_stalled(const supervision_t *supervision, uint32_t now_ms);

#endif
