#ifndef UPDATE_H
#define UPDATE_H

#include <stdint.h>
#include <zephyr/bluetooth/conn.h>

/** Sets up the SMP client that sends the oximeter its updates. Returns 0 or -errno. */
int update_init(void);

/** Notes the oximeter's SMP characteristic at `handle`, found by discovery on `conn`. */
void update_found(struct bt_conn *conn, uint16_t handle);

/** Subscribes to the oximeter's SMP responses, which only a link at level 4 may. */
void update_link_secured(struct bt_conn *conn);

#endif
