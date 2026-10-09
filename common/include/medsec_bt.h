#ifndef MEDSEC_BT_H
#define MEDSEC_BT_H

#include <zephyr/bluetooth/uuid.h>

/* The alarm service isn't a Bluetooth SIG standard, so its UUIDs are 128-bit: one random base,
 * with the first field numbering the service and its characteristics. */
#define MEDSEC_UUID(n) BT_UUID_128_ENCODE(0x2f6a0000u + (n), 0x0f8a, 0x4e3c, 0x9d2b, 0x6c1e4a7b9d10)

#define MEDSEC_ALARM_SERVICE_VAL MEDSEC_UUID(1)
#define MEDSEC_ALARM_LIMITS_VAL MEDSEC_UUID(2) /* alarm limits: read and write */
#define MEDSEC_ALARM_STATE_VAL MEDSEC_UUID(3)  /* ALARM_* bits: read and notify */

#endif
