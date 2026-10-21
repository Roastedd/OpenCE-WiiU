/*
WIIU_ALIGN_FIXUP.H

The Wii U's core raises an alignment exception for a floating-point load or store at an address that is not a multiple of 4
(lfs, lfd, stfs, stfd and their indexed and update forms; integer accesses and lfd at a word-aligned address work in hardware;
measured by the probe on a console). The game reads floats at odd offsets in animation data, as x86 allows. Cafe OS delivers the
exception to the application's callback, and a callback that moves the program counter past the instruction and returns TRUE
lets the thread go on. This makes the access in the callback's place, byte by byte: it decodes the instruction, moves the four or
eight bytes between the memory at the address the exception gave (DAR) and the register, updates the base register for the update
forms, and says whether it could. A fix-up costs a microsecond or so (the skipped exception alone measured 0.85-1.05 us).

Plain C on the registers of the saved context, so that it is tested on the desktop (and against the real instructions where there is
a PowerPC to run them on).
*/

#ifndef HALO_WIIU_ALIGN_FIXUP_H
#define HALO_WIIU_ALIGN_FIXUP_H

#include <stdint.h>

/* Makes the access of one instruction at address. gpr and fpr are the thread's registers at the exception (the base register of an
update form and the floating-point register of a load are changed). Returns 1 if it did, 0 if the instruction is not one of those
(paired singles, string or multiple loads, ...) or the register fields are not valid for it: the caller then treats the exception
as the fault it is. */
int wiiu_align_fixup(uint32_t instruction, uintptr_t address, uint32_t *gpr, double *fpr);

#endif
