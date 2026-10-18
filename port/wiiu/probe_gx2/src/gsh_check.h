/*
GSH_CHECK.H

A GSH (GFD) file is what CafeGLSL's command-line compiler writes and what the program's drawing loads with the system's
loader. This walks one without the loader: the file header, the chain of blocks, the shaders in it. A file that is cut
short, or older than the loader's version, shows here on the desktop (tests/test_gx2_logic.c checks the embedded files)
instead of as a loader error on the console. All fields are big-endian in the file.
*/

#ifndef HALO_WIIU_GX2_GSH_CHECK_H
#define HALO_WIIU_GX2_GSH_CHECK_H

#include <stddef.h>
#include <stdint.h>

typedef struct gsh_summary
{
	unsigned major, minor;
	unsigned vertex_headers, vertex_programs, pixel_headers, pixel_programs;
	unsigned blocks;
} gsh_summary;

/* NULL if the file is well formed, else what is wrong. */
const char *gsh_check(const uint8_t *data, size_t size, gsh_summary *summary);

#endif
