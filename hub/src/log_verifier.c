#include "log_verifier.h"

#include "medsec_crypto.h"

#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>

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

/* The tag covers the hub's own challenge, so an answer to an earlier one, replayed, fails; and the
 * number, so neither can a changed one. */
bool log_verifier_check_head(const uint8_t challenge[SECURITY_LOG_CHALLENGE_SIZE],
                             const uint8_t answer[SECURITY_LOG_HEAD_ANSWER_SIZE], uint32_t *newest)
{
    const uint32_t sequence = sys_get_le32(answer);
    uint8_t message[SECURITY_LOG_HEAD_MESSAGE_SIZE];

    security_log_head_message(challenge, sequence, message);
    if (psa_mac_verify(key_id, MEDSEC_MAC, message, sizeof message, &answer[sizeof sequence],
                       SECURITY_LOG_TAG_SIZE) != PSA_SUCCESS) {
        return false;
    }
    *newest = sequence;
    return true;
}
