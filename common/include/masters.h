#ifndef MASTERS_H
#define MASTERS_H

#include <stdint.h>

#define MASTER_SIZE 32u

/** The backend's master for each oximeter's limits key and log key, made at build time by
 * common/master.cmake. */
extern const uint8_t signing_master[MASTER_SIZE];

#endif
