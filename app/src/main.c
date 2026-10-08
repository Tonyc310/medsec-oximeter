#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(oximeter, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("medsec-oximeter started");
    return 0;
}
