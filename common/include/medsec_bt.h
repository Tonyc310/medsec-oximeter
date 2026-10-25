#ifndef MEDSEC_BT_H
#define MEDSEC_BT_H

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

/* The alarm service isn't a Bluetooth SIG standard, so its UUIDs are 128-bit: one random base,
 * with the first field numbering the service and its characteristics. */
#define MEDSEC_UUID(n) BT_UUID_128_ENCODE(0x2f6a0000u + (n), 0x0f8a, 0x4e3c, 0x9d2b, 0x6c1e4a7b9d10)

#define MEDSEC_ALARM_SERVICE_VAL MEDSEC_UUID(1)
#define MEDSEC_ALARM_LIMITS_VAL MEDSEC_UUID(2)    /* alarm limits: read, and signed writes */
#define MEDSEC_ALARM_STATE_VAL MEDSEC_UUID(3)     /* ALARM_* bits: read and notify */
#define MEDSEC_ALARM_CHALLENGE_VAL MEDSEC_UUID(4) /* what a limits change is signed over: read */
#define MEDSEC_SECURITY_LOG_VAL MEDSEC_UUID(5)    /* security event records, oldest first: read */
#define MEDSEC_DEVICE_ID_VAL MEDSEC_UUID(6)       /* the ID its key is derived from: read */
/* How far the log goes: write a backend's challenge, then read the answer to it. */
#define MEDSEC_LOG_HEAD_VAL MEDSEC_UUID(9)

/* What the oximeter protects needs security level 4: authenticated (AUTHEN) LE Secure Connections
 * (LESC). Either flag alone accepts less: LESC alone takes an unauthenticated Just Works pairing.
 */
#define MEDSEC_PERM_READ_L4 (BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_READ_LESC)
#define MEDSEC_PERM_WRITE_L4 (BT_GATT_PERM_WRITE_AUTHEN | BT_GATT_PERM_WRITE_LESC)

#define MEDSEC_PAIRING_SERVICE_VAL MEDSEC_UUID(7)
#define MEDSEC_SEALED_PAIRING_DATA_VAL MEDSEC_UUID(8) /* pairing_seal's, fresh each read: read */

#endif
