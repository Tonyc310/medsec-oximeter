#include "log_verifier.h"

#include "medsec_crypto.h"

#include <psa/crypto.h>

static psa_key_id_t key_id = PSA_KEY_ID_NULL;

bool log_verifier_init(const uint8_t *key, size_t length)
{
    /* The backend only checks the log's tags; only the oximeter makes them. */
    return medsec_mac_key_load(&key_id, key, length, PSA_KEY_USAGE_VERIFY_MESSAGE);
}

/* psa_mac_verify compares in constant time. */
bool log_verifier_check(const uint8_t entry[SECURITY_LOG_ENTRY_SIZE])
{
    return psa_mac_verify(key_id, MEDSEC_MAC, entry, SECURITY_LOG_RECORD_SIZE,
                          &entry[SECURITY_LOG_RECORD_SIZE], SECURITY_LOG_TAG_SIZE) == PSA_SUCCESS;
}
