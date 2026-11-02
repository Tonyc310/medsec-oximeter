#ifndef LINK_ADMISSION_H
#define LINK_ADMISSION_H

#include <stdbool.h>
#include <stdint.h>

/* The connections the oximeter allows: one any central may take, and while a central that hasn't
 * paired holds it, one kept for the gateway it's bonded with (threat T-11). */
#define LINK_ADMISSION_CONNECTIONS 2u

typedef enum {
    LINK_ADMIT_NOBODY,
    LINK_ADMIT_GATEWAY, /* only the gateway the oximeter is bonded with */
    LINK_ADMIT_ANYONE,
} link_admission_t;

/* The oximeter's links as they stand. */
typedef struct {
    uint32_t connected;  /* centrals connected, 0 to LINK_ADMISSION_CONNECTIONS */
    bool gateway_linked; /* one of them at security level 4, which only the gateway reaches */
    bool bonded;         /* a gateway's bond is kept */
} link_state_t;

/** Which central the oximeter takes a connection from next. */
link_admission_t link_admission(link_state_t links);

#endif
