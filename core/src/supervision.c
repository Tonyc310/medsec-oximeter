#include "supervision.h"

/* Until the first sample, the clock runs from the start. */
void supervision_init(supervision_t *supervision, uint32_t now_ms)
{
    supervision->last_sample_ms = now_ms;
}

void supervision_sample(supervision_t *supervision, uint32_t now_ms)
{
    supervision->last_sample_ms = now_ms;
}

/* Unsigned subtraction gives the time since the last sample even when the counter has wrapped. */
bool supervision_stalled(const supervision_t *supervision, uint32_t now_ms)
{
    return (uint32_t)(now_ms - supervision->last_sample_ms) >= SUPERVISION_STALL_MS;
}
