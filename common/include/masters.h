#ifndef MASTERS_H
#define MASTERS_H

#include <stdint.h>

#define MASTER_SIZE 32u

/* The backend's masters, made at build time by common/master.cmake. */

/** For each oximeter's limits key and log key. */
extern const uint8_t signing_master[MASTER_SIZE];

/** For each oximeter's pairing key. */
extern const uint8_t pairing_master[MASTER_SIZE];

#endif
