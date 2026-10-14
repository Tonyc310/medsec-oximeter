#include "medsec_crypto.h"

bool medsec_mac_key_load(psa_key_id_t *id, const uint8_t *key, size_t length, psa_key_usage_t usage)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    /* The old key goes first, so a failure below leaves no key loaded rather than the old one.
     * Destroying when there's no key is harmless. */
    (void)psa_destroy_key(*id);
    *id = PSA_KEY_ID_NULL;
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, MEDSEC_MAC);
    return psa_import_key(&attributes, key, length, id) == PSA_SUCCESS;
}
