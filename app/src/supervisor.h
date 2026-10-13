#ifndef SUPERVISOR_H
#define SUPERVISOR_H

/** Called once a second, on the supervisor's thread, while the sensor sends no samples. */
typedef void (*supervisor_stall_handler_t)(void);

/** Starts the watchdog and the twice-a-second check that samples are arriving. Returns 0 or
 * -errno. */
int supervisor_start(supervisor_stall_handler_t on_stall);

/** Notes a sample read from the sensor; call for each one. */
void supervisor_sample(void);

#endif
