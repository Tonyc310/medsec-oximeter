#include "pairing_seal.h"

#include "medsec_crypto.h"

#include <psa/crypto.h>

static psa_key_id_t key_id = PSA_KEY_ID_NULL;

bool pairing_seal_init(const uint8_t *key, size_t length)
{
    return medsec_key_load(&key_id, key, length, PSA_KEY_TYPE_AES, PSA_ALG_CCM,
                           PSA_KEY_USAGE_ENCRYPT);
}

bool pairing_open_init(const uint8_t *key, size_t length)
{
    return medsec_key_load(&key_id, key, length, PSA_KEY_TYPE_AES, PSA_ALG_CCM,
                           PSA_KEY_USAGE_DECRYPT);
}

bool pairing_seal(const uint8_t device_id[DEVICE_ID_SIZE], const uint8_t data[PAIRING_DATA_SIZE],
                  uint8_t sealed[PAIRING_SEALED_SIZE])
{
    size_t length;

    if (psa_generate_random(sealed, PAIRING_SEAL_NONCE_SIZE) != PSA_SUCCESS) {
        return false;
    }
    return psa_aead_encrypt(key_id, PSA_ALG_CCM, sealed, PAIRING_SEAL_NONCE_SIZE, device_id,
                            DEVICE_ID_SIZE, data, PAIRING_DATA_SIZE,
                            &sealed[PAIRING_SEAL_NONCE_SIZE],
                            PAIRING_DATA_SIZE + PAIRING_SEAL_TAG_SIZE, &length) == PSA_SUCCESS;
}

bool pairing_open(const uint8_t device_id[DEVICE_ID_SIZE],
                  const uint8_t sealed[PAIRING_SEALED_SIZE], uint8_t data[PAIRING_DATA_SIZE])
{
    size_t length;

    return psa_aead_decrypt(key_id, PSA_ALG_CCM, sealed, PAIRING_SEAL_NONCE_SIZE, device_id,
                            DEVICE_ID_SIZE, &sealed[PAIRING_SEAL_NONCE_SIZE],
                            PAIRING_DATA_SIZE + PAIRING_SEAL_TAG_SIZE, data, PAIRING_DATA_SIZE,
                            &length) == PSA_SUCCESS;
}
