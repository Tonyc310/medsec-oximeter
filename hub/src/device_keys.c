#include "device_keys.h"

#include <psa/crypto.h>
#include <string.h>

/* Says what the derived key is for, so the same master can never yield it for another purpose. */
#define LABEL "medsec limits key"

bool device_keys_hkdf(const uint8_t *ikm, size_t ikm_length, const uint8_t *salt,
                      size_t salt_length, const uint8_t *info, size_t info_length, uint8_t *out,
                      size_t out_length)
{
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t status = psa_crypto_init();

    /* PSA takes the inputs in this order. An empty salt is RFC 5869's absent one. */
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_setup(&operation, PSA_ALG_HKDF(PSA_ALG_SHA_256));
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT, salt,
                                                salt_length);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET, ikm,
                                                ikm_length);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO, info,
                                                info_length);
    }
    if (status == PSA_SUCCESS) {
        status = psa_key_derivation_output_bytes(&operation, out, out_length);
    }
    (void)psa_key_derivation_abort(&operation);
    return status == PSA_SUCCESS;
}

bool device_keys_derive(const uint8_t *master, size_t master_length,
                        const uint8_t device_id[DEVICE_ID_SIZE], uint8_t key[DEVICE_KEY_SIZE])
{
    uint8_t info[sizeof LABEL - 1u + DEVICE_ID_SIZE];

    (void)memcpy(info, LABEL, sizeof LABEL - 1u);
    (void)memcpy(&info[sizeof LABEL - 1u], device_id, DEVICE_ID_SIZE);
    return device_keys_hkdf(master, master_length, NULL, 0u, info, sizeof info, key,
                            DEVICE_KEY_SIZE);
}
