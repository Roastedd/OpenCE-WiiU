/*
WIIU_CRASH.H

A bad memory access, a jump to nowhere or an illegal instruction ends a Wii U app with the last picture on the screen and
no word of why. The system calls a function of ours first, on the thread that faulted; this one writes where it was (the
program counter, the return address, the stack, the registers, the thread's name) and what the shell last knew (the phase,
the last heartbeat) to a file on the card, by hand: no printf, no allocation, no lock. Then the system carries on as it would
have.
*/

#ifndef HALO_WIIU_CRASH_H
#define HALO_WIIU_CRASH_H

/* path: the file to write. phase: the shell's current phase (a string literal, or NULL). beat: the last heartbeat line, in a
buffer that stays where it is (or NULL). Both are read from the fault handler. */
void wiiu_crash_install(const char *path, const char *const *phase, const char *beat);

#endif
