/*
CRASH.H

A bad memory access, a jump to nowhere or an illegal instruction ends a Wii U app with the last
picture still on the screen and no word of why. The system calls a function of ours first, on the
thread that faulted; this one writes where it was, with the stage that was running and the
registers, to crash-<compiler>.txt on the card (by hand: no printf, no allocation), then lets the system carry
on as it would have.
*/

#ifndef HALO_WIIU_GX2_CRASH_H
#define HALO_WIIU_GX2_CRASH_H

void crash_install(void);

/* a string literal: what the main thread is doing, for the note */
void crash_set_stage(const char *stage);

#endif
