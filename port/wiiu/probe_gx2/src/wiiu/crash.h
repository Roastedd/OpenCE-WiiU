/*
CRASH.H

A bad memory access, a jump to nowhere or an illegal instruction ends a Wii U app with the last
picture still on the screen and no word of why. The system calls a function of ours first, on the
thread that faulted. This one does not touch the card: the first report froze the console on a misaligned
float load, with no note written, and writing a file from inside an exception (the card is reached
through messages to another processor that the faulting core may not be able to wait for) is the thing
under suspicion. It copies down where it was, with the stage that was running and the registers, into
memory, by hand (no printf, no allocation), and a thread of its own on another core writes the note to
crash-<compiler>.txt while the faulting one waits for it, a few seconds at most. Then the system carries
on as it would have.

For the unaligned stage it can do more: an alignment exception that arrives while the stage has asked
for it is counted, the instruction that caused it skipped, and the thread resumed (if the system lets a
handler do that, which this finds out).
*/

#ifndef HALO_WIIU_GX2_CRASH_H
#define HALO_WIIU_GX2_CRASH_H

#include <stdint.h>

void crash_install(void);

/* a string literal: what the main thread is doing, for the note */
void crash_set_stage(const char *stage);

/* While on, an alignment exception is counted and the thread resumed instead of reporting a crash: 1 skips the instruction, 2
makes the access (src/wiiu_align_fixup.c) and then skips it. 0 is off. */
void crash_recover_alignment(int mode);
/* how many accesses the handler made, and how many instructions it could not (mode 2) */
uint32_t crash_alignment_emulated(uint32_t *refused);
/* how many alignment exceptions the handler has taken, and where the last one was (its pc, and the address) */
uint32_t crash_alignment_traps(void);
void crash_alignment_last(uint32_t *pc, uint32_t *address);
/* and the instruction there and the DSISR the exception gave */
void crash_alignment_detail(uint32_t *instruction, uint32_t *dsisr);
/* the bits the context held for the instruction's floating-point register when the exception came */
void crash_alignment_register(uint64_t *bits);

#endif
