/* Mip levels made on the CPU. */
#ifndef WIIU_MIP_H
#define WIIU_MIP_H

#include <stddef.h>
#include <stdint.h>

/* The next level of a 4-byte-texel image: each texel is the average of the 2 x 2 of the source it covers (a source of odd size repeats its last row and column, a size of 1 stays
1), every channel rounded to nearest. Rows are `pitch` texels apart in source and destination. The destination is max(sw / 2, 1) x max(sh / 2, 1). */
void wiiu_mip_next(const uint8_t *source, uint32_t source_width, uint32_t source_height, uint32_t source_pitch, uint8_t *destination, uint32_t destination_pitch);

#endif
