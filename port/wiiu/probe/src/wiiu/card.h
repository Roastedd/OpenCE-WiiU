/*
CARD.H

The probe's files on the SD card: its report, a crash note if the fault handler runs, and the unaligned stage's memory of which
cases froze the console. Plain open/write/fsync/close on the card's path, which is how the other Wii U apps here reach it.
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
/* the unaligned stage's memory across restarts: the case it is in (so that a case that froze the console is known to have), and the
cases known to */
#define PROBE_UNALIGNED_PROGRESS_FILE "unaligned-progress-" PROBE_FLAVOUR ".txt"
#define PROBE_UNALIGNED_FATAL_FILE "unaligned-fatal-" PROBE_FLAVOUR ".txt"

/* creates the folders (nothing if they exist) */
void card_prepare(void);

/* Writes (or appends) size bytes to a file of the probe's folder. Returns 0, or the errno of what failed.
No allocation and no locks of its own. */
int card_write(const char *name, const char *data, size_t size, int append);

/* Reads a file of the probe's folder into buffer (terminated); returns its length, or -1 if there is none. */
int card_read(const char *name, char *buffer, size_t capacity);

/* Removes a file of the probe's folder. */
void card_remove(const char *name);

#endif
