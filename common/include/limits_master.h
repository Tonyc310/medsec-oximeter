#ifndef LIMITS_MASTER_H
#define LIMITS_MASTER_H

#include <stdint.h>

#define LIMITS_MASTER_SIZE 32u

/** The hub's master secret, generated at build time by common/limits_master.cmake. */
extern const uint8_t limits_master[LIMITS_MASTER_SIZE];

#endif
