/*
BENCH.H

Timing loops over memory and arithmetic, for the numbers the port's design depends on: how fast
a core copies, fills, reads and byte-swaps memory (the offline map converter exists so that the
console never swaps at load; this says what swapping at load would cost), and the cost of a
dependent floating-point operation. The same code on the desktop and on the console.

Every loop returns its best of several runs, in nanoseconds per unit, because the rest of a
system only ever makes a run slower. Buffers are the caller's; sizes are in bytes and multiples
of 128.
*/

#ifndef HALO_WIIU_BENCH_H
#define HALO_WIIU_BENCH_H

#include <stddef.h>
#include <stdint.h>

typedef uint64_t (*bench_clock)(void);

/* nanoseconds per byte */
double bench_copy(bench_clock now, void *destination, const void *source, size_t size);
double bench_fill(bench_clock now, void *destination, size_t size);
/* reads every word; *sink receives their sum so that the loop is not removed */
double bench_read(bench_clock now, const void *source, size_t size, uint32_t *sink);
/* swaps the byte order of every 32-bit word in place, as loading a little-endian file would */
double bench_swap32(bench_clock now, void *words, size_t size);
/* nanoseconds per dependent double-precision multiply and add; *sink receives the result */
double bench_float_chain(bench_clock now, double *sink);
/* nanoseconds per dependent integer multiply and add */
double bench_integer_chain(bench_clock now, uint32_t *sink);

#endif
