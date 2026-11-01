/* Mip levels made on the CPU. */
#ifndef WIIU_MIP_H
#define WIIU_MIP_H

#include <stddef.h>
#include <stdint.h>

/* The next level of a 4-byte-texel image: each texel is the average of the
2 x 2 of the source it covers, with half ties rounded up per channel. A size
of 1 repeats that row/column; an odd size above 1 discards its unpaired edge.
Rows are `pitch` texels apart. The destination is max(sw / 2, 1) x max(sh / 2, 1). */
void wiiu_mip_next(const uint8_t *source, uint32_t source_width, uint32_t source_height, uint32_t source_pitch, uint8_t *destination, uint32_t destination_pitch);

#endif
