#ifndef PLXS_H
#define PLXS_H

#include "oximetry.h"

/** Starts Bluetooth and advertises the Pulse Oximeter Service; returns 0 or a negative errno. */
int plxs_start(void);

/** Sends a reading to every central subscribed to Continuous Measurement notifications. */
void plxs_send(const oximetry_reading_t *reading);

#endif
