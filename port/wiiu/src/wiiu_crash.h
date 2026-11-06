/*
WIIU_CRASH.H

A bad memory access, a jump to nowhere or an illegal instruction ends a Wii U app with the last picture on the screen and
no word of why. The system calls a function of ours first, on the thread that faulted; this one writes where it was (the
program counter, the return address, the stack, the registers, the thread's name) and what the shell last knew (the phase,
the last heartbeat) into memory, by hand: no printf, no allocation, no lock, and no input or output; a thread of its own on another
core writes it to the file while the faulting thread waits, a few seconds at most. Then the system carries on as it would have.
*/

#ifndef HALO_WIIU_CRASH_H
#define HALO_WIIU_CRASH_H

#include <stdint.h>

/* path: the file to write. phase: the shell's current phase (a string literal, or NULL). beat: the last heartbeat line, in a
buffer that stays where it is (or NULL). Both are read from the fault handler. */
void wiiu_crash_install(const char *path, const char *const *phase, const char *beat);

/* A float or double at an address that is not a multiple of 4 raises an alignment exception on this core; the handler makes the
access itself (wiiu_align_fixup.c) and the game goes on. How many times, and the program counters it happened at (look them up
in the link map), for finding the places in the game to fix: place index 0, 1, ... gives the pc (0 past the last) and its count. */
uint32_t wiiu_crash_fixups(void);
/* the ones taken on the calling thread's core (the tick snapshot's timers: the captures run on core 0, the blends on core 1) */
uint32_t wiiu_crash_fixups_here(void);
uint32_t wiiu_crash_fixup_place(uint32_t index, uint32_t *count);

#endif
