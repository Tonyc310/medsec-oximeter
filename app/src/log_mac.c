#include "log_mac.h"

#include "medsec_crypto.h"

#include <psa/crypto.h>

static psa_key_id_t key_id = PSA_KEY_ID_NULL;

bool log_mac_init(const uint8_t *key, size_t length)
{
    /* The oximeter only makes these tags; the backend checks them. */
    return medsec_mac_key_load(&key_id, key, length, PSA_KEY_USAGE_SIGN_MESSAGE);
}

bool log_mac_tag(const uint8_t *data, size_t length, uint8_t tag[SECURITY_LOG_TAG_SIZE])
{
    size_t tag_length;

    return psa_mac_compute(key_id, MEDSEC_MAC, data, length, tag, SECURITY_LOG_TAG_SIZE,
                           &tag_length) == PSA_SUCCESS;
}
