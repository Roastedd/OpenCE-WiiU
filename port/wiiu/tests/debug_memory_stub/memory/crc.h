/* the checksum of memory/crc.h, as a sum: the test's pointers' headers only need one that changes with them */
#ifndef __DEBUG_MEMORY_STUB_CRC_H
#define __DEBUG_MEMORY_STUB_CRC_H
static inline void crc_new(unsigned long *checksum) { *checksum = 0xFFFFFFFFUL; }
static inline void crc_checksum_buffer(unsigned long *checksum, void const *buffer, unsigned long size)
{
	unsigned char const *bytes = (unsigned char const *)buffer;

	while (size--)
		*checksum = (*checksum * 31UL) ^ *bytes++;
}
#endif
