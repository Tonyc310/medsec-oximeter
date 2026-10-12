#ifndef LIMITS_KEY_H
#define LIMITS_KEY_H

#include <stdint.h>

#define LIMITS_KEY_SIZE 32u

/** The alarm-limits key, generated at build time by common/limits_key.cmake. */
extern const uint8_t limits_key[LIMITS_KEY_SIZE];

#endif
