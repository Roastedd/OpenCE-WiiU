/* What a draw needs besides its vertices - the GPU state, the shaders' keys, the stages' textures and samplers, the uniforms - derived from Direct3D's render
and texture stage states and a few facts of the draw (dk_draw_facts). The Direct3D layer's game thread used to derive them for every draw (d3d8_gx2.c's
draw_prepare, a tenth of the game thread in a fight); now it sends the state words that changed (DK_COMMAND_STATE_WRITES) and the facts when they change
(DK_COMMAND_DRAW_FACTS), and the GX2 thread keeps a copy of the states and derives here before a draw. The derivation is d3d8_gl.c's prepare_draw, as
d3d8_gx2.c had it. */
#ifndef DK_DERIVE_H
#define DK_DERIVE_H

#include <stdint.h>
#include "dk_commands.h"

#define DK_DERIVE_RENDER_STATES 144 /* D3DRS_MAX */
#define DK_DERIVE_TEXTURE_STATES 32 /* D3DTSS_MAX */
#define DK_DERIVE_STAGES 4

/* a stage's texture, as the game thread found it (the texture cache's image, or a render target's) */
struct dk_stage_facts
{
	uint32_t present; /* a texture is bound, it has data, and the stage's mode samples it */
	uint32_t id;
	uint32_t target;
	uint32_t composite;
	uint32_t kind; /* DK_TEXTURE_* */
	uint32_t mipmapped; /* more than one level */
	uint32_t hires;
	uint32_t hires_coverage;
	float scale[2]; /* a linear texture's 1 / width and height, else 1 */
};

struct dk_draw_facts
{
	uint32_t has_depth;
	float target_scale[2];
	uint32_t viewport_x, viewport_y, viewport_width, viewport_height;
	float viewport_min_z, viewport_max_z;
	float viewport_scale[4];
	float viewport_offset[4];
	int32_t ui_offset;
	float texture_clip[3];
	uint32_t warp_blend;
	uint32_t program_hash[2]; /* high, low */
	uint32_t packed_mask;
	struct dk_stage_facts stages[DK_DERIVE_STAGES];
};

/* index of a state written: a render state's, or DK_DERIVE_TEXTURE_STATE_INDEX(stage, type) */
#define DK_DERIVE_TEXTURE_STATE_INDEX(stage, type) (0x1000u + (stage) * DK_DERIVE_TEXTURE_STATES + (type))

struct dk_command_state_writes
{
	struct dk_command_header header;
	uint32_t count;
	uint32_t writes[]; /* index, value: count pairs */
};

struct dk_command_draw_facts
{
	struct dk_command_header header;
	struct dk_draw_facts facts;
};

/* what changed in the last dk_derive_run */
enum
{
	DK_DERIVED_STATE = 1u << 0,
	DK_DERIVED_KEYS = 1u << 1,
	DK_DERIVED_TEXTURES = 1u << 2,
	DK_DERIVED_VERTEX_PARAMETERS = 1u << 3,
	DK_DERIVED_PIXEL_PARAMETERS = 1u << 4,
};

struct dk_derive;

/* the one the GX2 thread keeps (zeroed: no state known yet) */
struct dk_derive *dk_derive_get(void);
void dk_derive_writes(struct dk_derive *derive, const struct dk_command_state_writes *command);
void dk_derive_facts(struct dk_derive *derive, const struct dk_draw_facts *facts);
/* whether a write or facts came since the last run */
int dk_derive_pending(const struct dk_derive *derive);
/* derives what the states and facts give; returns DK_DERIVED_* of what differs from the last run's (everything, the first time) */
unsigned dk_derive_run(struct dk_derive *derive);
const struct dk_draw_state *dk_derive_state(const struct dk_derive *derive);
const void *dk_derive_vertex_key(const struct dk_derive *derive);
const void *dk_derive_pixel_key(const struct dk_derive *derive);
const struct dk_command_textures *dk_derive_textures(const struct dk_derive *derive);
const struct dk_vertex_parameters *dk_derive_vertex_parameters(const struct dk_derive *derive);
const struct dk_pixel_parameters *dk_derive_pixel_parameters(const struct dk_derive *derive);
/* the whole of it, for the GPU test's replay (wiiu_gx2.c): saved before the kept frame, put back before each replay */
unsigned long dk_derive_size(void);

#endif
