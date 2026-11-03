#ifndef HCI_FUZZ_H
#define HCI_FUZZ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The controller the fuzzer plays: it answers the host's commands, and gives the host what the
 * harness says a central sent, as a controller gives it what the radio heard. */

/** A central connects, the host advertising. */
void hci_fuzz_connect(uint16_t handle);

/** The central leaves. */
void hci_fuzz_disconnect(uint16_t handle);

/** An ACL packet from the central: the start of an L2CAP frame, or the rest of one. */
void hci_fuzz_acl(uint16_t handle, bool start, const uint8_t *data, size_t length);

#endif
