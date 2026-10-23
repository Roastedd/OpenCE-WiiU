/* The range of a draw's 16-bit indices. */
#ifndef WIIU_INDICES_H
#define WIIU_INDICES_H

#include <stddef.h>
#include <stdint.h>

/* the least and greatest of count 16-bit indices (count 0: minimum 0xffff, maximum 0), two at a time with word loads */
void wiiu_index_range(const uint16_t *indices, size_t count, uint32_t *minimum, uint32_t *maximum);

/* the same, copying them to destination on the way (destination does not overlap; count of 16-bit indices): the list is read once. Copies and finds the range in words when source and
destination are aligned alike, an index at a time if not */
void wiiu_index_copy_range(uint16_t *destination, const uint16_t *source, size_t count, uint32_t *minimum, uint32_t *maximum);

#endif
