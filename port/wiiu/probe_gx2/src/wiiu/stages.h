/*
STAGES.H

The probe's stages. Each takes the session, reports into it, adds its page and records its answers; none of them stops
the run for what it finds (a finding is a line in the report), and each asks keep_going() now and then, so that HOME is
answered while one is under way. A stage that depends on an earlier one's result checks it and says so when it is not met.

The stages are in the order of what they risk: the ones that only draw come first, the compiler that runs on the console
(a library of 22 MB of someone else's code) last, after everything else is in the report.
*/

#ifndef HALO_WIIU_GX2_STAGES_H
#define HALO_WIIU_GX2_STAGES_H

#include "../answers.h"
#include "../bc.h"
#include "report.h"
#include "../scene.h"
#include "gx.h"
#include "pages.h"
#include "ui.h"

#define SESSION_PAGES 8

typedef struct gx_session
{
	report *report;
	answers *answers;
	uint64_t (*now_ns)(void);
	int (*keep_going)(void);
	void (*flush)(void);

	/* from the system stage */
	int ready;              /* the shaders, the pages and the target are there */
	gx_target scene;        /* the 64 x 64 target the tests draw into and the CPU reads */
	gx_pipeline attr;       /* a_pos, a_val: the value shown as 0.5 + 0.25 * value */
	gx_pipeline packed;     /* a_pos, a_packed */
	gx_pipeline generated;  /* the generator's vs00 and the diffuse pixel shader, compiled on a desktop */

	/* from the control stage */
	readback_order order;   /* the channel order of the target's bytes */
	int readback_ok;        /* the order was found and the whole target agreed */
	int texture_ok;         /* an RGBA8 texture drawn 1:1 came back as it was */
	int rows_flipped;       /* ... upside down */

	page pages[SESSION_PAGES];
	unsigned page_count;
} gx_session;

void stage_system(gx_session *s);
void stage_control(gx_session *s);
void stage_index(gx_session *s);
void stage_vertex(gx_session *s);
void stage_textures(gx_session *s);
void stage_glsl(gx_session *s);

/* ---------- shared by the stages (stage_common.c) */

/* a new, empty page (NULL if there is no room) */
page *session_new_page(gx_session *s, const char *title);
/* the pixel of the scene target in RGBA, whatever the order of its bytes */
void session_pixel(const gx_session *s, unsigned x, unsigned y, uint8_t rgba[4]);
/* "r,g,b,a" */
const char *pixel_text(const uint8_t rgba[4]);

/* One pass over the scene target: clears it to scene_clear_rgba, leaves it bound with the view set, ui_begin(64, 64) done. */
void scene_begin(gx_session *s);
/* flushes the ui, waits for the GPU and drops the CPU's stale copy of the target */
void scene_end(gx_session *s);
/* a tile's background: the scene's clear color over the whole viewport */
void tile_background(float width, float height);
/* a texture over the whole viewport (rows flipped if the control found the textures upside down) */
void draw_texture_fill(const gx_session *s, const GX2Texture *texture, float width, float height);

/* the pattern decoded on the CPU and uploaded as an RGBA8 texture (the control stage's); NULL if that failed */
const GX2Texture *control_reference(bc_format format);
const uint8_t *control_expected(bc_format format);

/* the first page: every answer, wrapped, with the compiler and build the report is from */
void session_overview(const gx_session *s, page *overview, const char *flavour, const char *build);

/* what a compiled pair is (sizes, inputs, uniforms, samplers), and whether its interpolants meet; either may be NULL */
void report_shader_pair(report *r, const char *name, const GX2VertexShader *vs, const GX2PixelShader *ps);

/* the session the callbacks of the pages draw for */
gx_session *session_current(void);
void session_set_current(gx_session *s);

#endif
