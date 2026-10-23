/* The range of a draw's 16-bit indices. */
#ifndef WIIU_INDICES_H
#define WIIU_INDICES_H

#include <stddef.h>
#include <stdint.h>

/* the least and greatest of count 16-bit indices (count 0: minimum 0xffff, maximum 0), two at a time with word loads */
void wiiu_index_range(const uint16_t *indices, size_t count, uint32_t *minimum, uint32_t *maximum);

#endif
