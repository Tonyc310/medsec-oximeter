#ifndef PLX_SERVICE_H
#define PLX_SERVICE_H

#include "oximetry.h"

/** Starts Bluetooth and advertises the Pulse Oximeter Service; returns 0 or a negative errno. */
int plx_service_start(void);

/** Sends a reading to every central subscribed to Continuous Measurement notifications. */
void plx_service_send(const oximetry_reading_t *reading);

#endif
