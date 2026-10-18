/*
SHADER_DATA.H

The probe's shaders, as the program holds them (shader_glsl.c and shader_gsh.c, written by tools/embed_shaders.py):

 - GSH files, what CafeGLSL's command-line compiler writes for a vertex and pixel shader pair. The program loads these
   with the system's loader, so the drawing tests do not depend on the compiler running on the console;
 - GLSL text for the stage that does depend on it: the shaders OpenCE's generator writes (generated/, from
   tools/wiiu/gx2_probe_shaders.sh) and the probe's own, given to the compiler on the console.
*/

#ifndef HALO_WIIU_GX2_SHADER_DATA_H
#define HALO_WIIU_GX2_SHADER_DATA_H

typedef enum glsl_kind
{
	GLSL_VERTEX,
	GLSL_PIXEL
} glsl_kind;

typedef struct glsl_source
{
	const char *name;
	glsl_kind kind;
	const char *text;
} glsl_source;

/* the order of tools/embed_shaders.py SOURCES */
enum
{
	GLSL_ATTR_VS,
	GLSL_ATTR_PS,
	GLSL_GEN_VS00,
	GLSL_GEN_VS00_PACKED,
	GLSL_GEN_VS09,
	GLSL_GEN_VS09_LIT,
	GLSL_GEN_VS31,
	GLSL_GEN_VS31_LIT,
	GLSL_GEN_PS_DIFFUSE,
	GLSL_GEN_PS_TEXT,
	GLSL_GEN_PS_TEXT_FOG_ALPHATEST,
	GLSL_GEN_PS_TEXT_LIT_3D,
	GLSL_SOURCE_COUNT
};

extern const glsl_source glsl_sources[GLSL_SOURCE_COUNT];

typedef struct gsh_entry
{
	const char *name;
	const unsigned char *data;
	unsigned long size;
} gsh_entry;

/* the order of tools/embed_shaders.py PAIRS */
enum
{
	GSH_UI,        /* a textured, tinted quad: a_pos (0), a_uv (1), a_col (2); sampler 0 */
	GSH_ATTR,      /* a_pos (0), a_val (1): the value shown as 0.5 + 0.25 * value */
	GSH_PACKED,    /* a_pos (0), a_packed (1): a NORMPACKED3 integer, unpacked as the generator does */
	GSH_GENERATED, /* the generator's vs00 and its diffuse pass-through pixel shader: v0 (0) and v9 (9) */
	GSH_COUNT
};

extern const gsh_entry gsh_files[GSH_COUNT];

#endif
