/*
CARD.H

The probe's files on the SD card: its report and, if the console fault handler runs, a crash note.
Plain open/write/fsync/close on the card's path, which is how the other Wii U apps here reach it.
*/

#ifndef HALO_WIIU_PROBE_CARD_H
#define HALO_WIIU_PROBE_CARD_H

#include <stddef.h>

#define PROBE_DIRECTORY "/vol/external01/wiiu/apps/opence-probe"

/* which compiler built this one (the Makefile says); the files carry it, so that the two builds' reports sit side by side */
#ifndef PROBE_FLAVOUR
#define PROBE_FLAVOUR "unknown"
#endif
#define PROBE_REPORT_FILE "report-" PROBE_FLAVOUR ".txt"
#define PROBE_CRASH_FILE "crash-" PROBE_FLAVOUR ".txt"

/* creates the folders (nothing if they exist) */
void card_prepare(void);

/* Writes (or appends) size bytes to a file of the probe's folder. Returns 0, or the errno of what failed.
Safe in a fault handler: no allocation and no locks of its own. */
int card_write(const char *name, const char *data, size_t size, int append);

#endif
