#ifndef ALARM_SERVICE_H
#define ALARM_SERVICE_H

#include "oximetry.h"

/** Sets the default limits and the alarm LED; call before readings arrive. Returns 0 or -errno. */
int alarm_service_init(void);

/** Checks a reading against the limits; lights the LED and notifies centrals when alarms change. */
void alarm_service_update(const oximetry_reading_t *reading);

#endif
