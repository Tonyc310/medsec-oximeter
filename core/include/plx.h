#ifndef PLX_H
#define PLX_H

#include "oximetry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The Bluetooth SIG Pulse Oximeter Service's PLX Continuous Measurement characteristic. Values are
 * IEEE 11073 SFLOATs (a 12-bit mantissa and a 4-bit power of ten), little-endian. */

/* Flags, SpO2 and pulse rate: the fields every Continuous Measurement carries. */
#define PLX_CONTINUOUS_SIZE 5u

/* With every optional field present. */
#define PLX_CONTINUOUS_MAX_SIZE 20u

/** Encodes a reading with no optional fields; no reading is sent as SFLOAT "not a number". */
void plx_encode_continuous(const oximetry_reading_t *reading, uint8_t out[PLX_CONTINUOUS_SIZE]);

/**
 * Decodes a Continuous Measurement from another device. Returns false, and leaves `reading`
 * untouched, for anything malformed: a wrong length, reserved flags, or values out of range.
 */
bool plx_decode_continuous(const uint8_t *data, size_t length, oximetry_reading_t *reading);

#endif
