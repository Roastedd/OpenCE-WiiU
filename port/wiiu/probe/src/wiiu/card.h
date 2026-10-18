/*
CARD.H

The probe's files on the SD card: its report and, if the console fault handler runs, a crash note.
Plain open/write/fsync/close on the card's path, which is how the other Wii U apps here reach it.
*/

#ifndef HALO_WIIU_PROBE_CARD_H
#define HALO_WIIU_PROBE_CARD_H

#include <stddef.h>

#define PROBE_DIRECTORY "/vol/external01/wiiu/apps/opence-probe"

/* creates the folders (nothing if they exist) */
void card_prepare(void);

/* Writes (or appends) size bytes to a file of the probe's folder. Returns 0, or the errno of what failed.
Safe in a fault handler: no allocation and no locks of its own. */
int card_write(const char *name, const char *data, size_t size, int append);

#endif
