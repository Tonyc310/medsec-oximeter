#ifndef PAIRING_H
#define PAIRING_H

#include "device_key.h"

#include <stdint.h>
#include <zephyr/bluetooth/conn.h>

/** Registers the hub's side of pairing. Returns 0 or -errno. */
int pairing_init(void);

/** Secures the link to the oximeter on `conn` at security level 4: with the bond, if the hub has
 * one; otherwise by pairing, reading its sealed out-of-band data at `sealed_handle` and opening it
 * with `key`, the pairing key derived for the oximeter with `device_id`. The connection's
 * security_changed callback has the result. */
void pairing_start(struct bt_conn *conn, uint16_t sealed_handle,
                   const uint8_t device_id[DEVICE_ID_SIZE], const uint8_t key[DEVICE_KEY_SIZE]);

#endif
