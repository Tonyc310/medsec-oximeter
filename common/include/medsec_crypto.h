#ifndef MEDSEC_CRYPTO_H
#define MEDSEC_CRYPTO_H

#include "limits_auth.h"

#include <psa/crypto.h>

/* The MAC on alarm-limit changes: HMAC-SHA256 cut to the tag size. The oximeter checks it and the
 * hub makes it, so both take it from here. */
#define MEDSEC_LIMITS_MAC PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), LIMITS_AUTH_TAG_SIZE)

#endif
