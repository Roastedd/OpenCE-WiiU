#ifndef WIIU_MISSING_KEYS_H
#define WIIU_MISSING_KEYS_H

#include <stdint.h>

#define WIIU_MISSING_SLOTS 64u
#define WIIU_MISSING_WORDS 544u
#define WIIU_MISSING_WIRE 256u
#define WIIU_MISSING_TEXT 16384u
#define WIIU_MISSING_BATCH 8u

typedef struct wiiu_missing_keys_config
{
    const char *directory;
    uint32_t run_high, run_low;
    uint32_t (*thread_id)(void *context);
    int (*io_allowed)(void *context);
    void (*report)(void *context, const char *text);
    void *context;
} wiiu_missing_keys_config;

typedef struct wiiu_missing_keys_counts
{
    uint32_t accepted, written, full, unavailable, lost_io, io_errors, close_errors;
    uint32_t rejected_producer, rejected_consumer, queue_pending, batch_records;
    uint32_t phase, gate, io_gate, gameplay, files, failed, drained, complete;
    int last_error;
} wiiu_missing_keys_counts;

/* Initialize once before the producer and main-log worker start. No I/O. */
int wiiu_missing_keys_start(const wiiu_missing_keys_config *config);
/* One audited producer only; rejects other threads before their seen-table access. */
int wiiu_missing_keys_producer(void);
/* Cover the entire seen-table/program scan too, so close cannot certify a pending observation. */
int wiiu_missing_keys_observe(void);
void wiiu_missing_keys_observed(void);
/* Copied primitive data only. Return 1 if queued; 0 is explicit capture loss. */
int wiiu_missing_keys_pixel(uint32_t hash_high, uint32_t hash_low, const unsigned char *wire, uint32_t bytes);
int wiiu_missing_keys_vertex(uint32_t hash_high, uint32_t hash_low, uint32_t program_high,
    uint32_t program_low, uint32_t packed_mask, uint32_t instructions, const uint32_t *words);
void wiiu_missing_keys_unavailable(void);
/* Main-log worker only: bounded CPU work and at most one filesystem operation. */
int wiiu_missing_keys_service(void *unused);
/* Never waits, closes a descriptor, frees, resets or assumes the producer parked. */
void wiiu_missing_keys_stop(void);
/* Suspend/resume new worker turns for HOME; quiesce is permanent and nonblocking at final close. */
void wiiu_missing_keys_suspend(void);
void wiiu_missing_keys_resume(void);
void wiiu_missing_keys_quiesce(void);
/* Game-thread authoritative clock/menu transitions only. Active gameplay admits no new file step. */
void wiiu_missing_keys_gameplay(int active);
/* Development key sweeps only (halo/shader_capture.txt "always"): gameplay no longer holds file steps back, so a run that never pauses
   keeps its keys and the 64 slots do not fill. Called once at start, before the game thread runs. */
void wiiu_missing_keys_always(int on);
void wiiu_missing_keys_counts_get(wiiu_missing_keys_counts *counts);
void wiiu_missing_keys_report(const char *reason);

#endif
