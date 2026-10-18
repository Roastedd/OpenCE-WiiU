#include "gsh_check.h"

#include <string.h>

#define FILE_HEADER_SIZE 0x20u
#define BLOCK_HEADER_SIZE 0x20u

enum
{
	BLOCK_END = 1,
	BLOCK_PADDING = 2,
	BLOCK_VERTEX_HEADER = 3,
	BLOCK_VERTEX_PROGRAM = 5,
	BLOCK_PIXEL_HEADER = 6,
	BLOCK_PIXEL_PROGRAM = 7
};

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

const char *gsh_check(const uint8_t *data, size_t size, gsh_summary *summary)
{
	size_t at = FILE_HEADER_SIZE;
	int ended = 0;

	memset(summary, 0, sizeof(*summary));
	if (size < FILE_HEADER_SIZE + BLOCK_HEADER_SIZE)
		return "shorter than a header and a block";
	if (memcmp(data, "Gfx2", 4))
		return "no Gfx2 magic";
	if (be32(data + 4) != FILE_HEADER_SIZE)
		return "file header size is not 0x20";
	summary->major = be32(data + 8);
	summary->minor = be32(data + 12);
	while (at + BLOCK_HEADER_SIZE <= size)
	{
		uint32_t type, length;

		if (memcmp(data + at, "BLK{", 4))
			return "block without BLK{ magic";
		if (be32(data + at + 4) != BLOCK_HEADER_SIZE)
			return "block header size is not 0x20";
		type = be32(data + at + 16);
		length = be32(data + at + 20);
		if (length > size - at - BLOCK_HEADER_SIZE)
			return "block runs past the end of the file";
		summary->blocks++;
		switch (type)
		{
		case BLOCK_VERTEX_HEADER: summary->vertex_headers++; break;
		case BLOCK_VERTEX_PROGRAM: summary->vertex_programs++; break;
		case BLOCK_PIXEL_HEADER: summary->pixel_headers++; break;
		case BLOCK_PIXEL_PROGRAM: summary->pixel_programs++; break;
		case BLOCK_END: ended = 1; break;
		case BLOCK_PADDING: break;
		default: return "a block of a kind a shader pair does not have";
		}
		at += BLOCK_HEADER_SIZE + length;
		if (ended)
			break;
	}
	if (!ended)
		return "no end block";
	if (at != size)
		return "bytes after the end block";
	if (summary->vertex_headers != 1 || summary->vertex_programs != 1 || summary->pixel_headers != 1 || summary->pixel_programs != 1)
		return "not exactly one vertex and one pixel shader";
	return NULL;
}
