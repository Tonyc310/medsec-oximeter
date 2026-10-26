/* Any bytes, as a security log record reaching the hub: a record it takes encodes back to the same
 * bytes, so the hub never shows a record other than the one the oximeter wrote. */
#include "security_log.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    security_record_t record = {0};
    uint8_t encoded[SECURITY_LOG_RECORD_SIZE];

    if (security_log_decode(data, size, &record)) {
        security_log_encode(&record, encoded);
        if (memcmp(encoded, data, sizeof encoded) != 0) {
            abort();
        }
    }
    return 0;
}
