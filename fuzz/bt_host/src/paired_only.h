#ifndef PAIRED_ONLY_H
#define PAIRED_ONLY_H

/** Stops the fuzzer when a central below security level 4 reaches anything in the oximeter's
 * services but what it needs to find them and pair (C-04). */
void paired_only_watch(void);

#endif
