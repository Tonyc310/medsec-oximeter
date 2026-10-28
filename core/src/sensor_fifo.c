#include "sensor_fifo.h"

uint32_t sensor_fifo_waiting(sensor_fifo_t fifo)
{
    /* Without rollover, a full FIFO keeps what it holds and drops new samples, so a lost sample
     * means its pointers met because it's full, not empty. */
    if (fifo.overflow != 0u) {
        return SENSOR_FIFO_DEPTH;
    }
    /* The pointers are 5 bits, counting round the 32 slots. */
    return ((uint32_t)fifo.write_pointer - (uint32_t)fifo.read_pointer) & (SENSOR_FIFO_DEPTH - 1u);
}
