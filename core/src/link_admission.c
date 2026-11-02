#include "link_admission.h"

link_admission_t link_admission(link_state_t links)
{
    if (links.connected == 0u) {
        return LINK_ADMIT_ANYONE;
    }
    /* A central below level 4 holds a connection: the other is the gateway's. Once the gateway is
     * linked nobody else needs one, so the oximeter stops advertising. */
    if ((links.connected < LINK_ADMISSION_CONNECTIONS) && !links.gateway_linked && links.bonded) {
        return LINK_ADMIT_GATEWAY;
    }
    return LINK_ADMIT_NOBODY;
}
