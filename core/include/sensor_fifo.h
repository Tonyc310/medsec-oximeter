#ifndef SENSOR_FIFO_H
#define SENSOR_FIFO_H

#include <stdint.h>

#define SENSOR_FIFO_DEPTH 32u

/* The MAX30101's FIFO registers 0x04 to 0x06, in the order a burst read returns them. Zephyr's
 * driver reads one sample a call and never these, so the firmware reads them to know how many
 * samples wait. */
typedef struct {
    uint8_t write_pointer;
    uint8_t overflow; /* samples lost to a full FIFO since one was last read, up to 31 */
    uint8_t read_pointer;
} sensor_fifo_t;

/** Samples waiting to be read: 0 to SENSOR_FIFO_DEPTH. */
uint32_t sensor_fifo_waiting(sensor_fifo_t fifo);

#endif
