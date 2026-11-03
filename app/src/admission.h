#ifndef ADMISSION_H
#define ADMISSION_H

/** Starts advertising, then advertises to whoever core/link_admission says may connect next, as
 * centrals connect, pair and leave, and drops a central that doesn't pair within 30 s. Call after
 * bt_enable, once the bond is loaded. */
void admission_start(void);

#endif
