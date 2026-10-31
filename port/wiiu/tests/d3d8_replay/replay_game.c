/*
REPLAY_GAME.C

The made-up game of the Direct3D layer's replay: frames drawn as Halo draws them, through the same calls - the BSP from the tag cache with the dynamic
triangles' renewed index buffer, models from the tag cache with the map's index lists in the window, dynamic vertices locked and written between draws,
lists the game makes in the heap and reuses at once (and one too long to go in a command), two-stream and stride-0 declarations, render to texture and a
texture rendered a level at a time, immediate mode, the decals' batches, a visibility test, the text's atlas, a HUD placeholder, a HUD override and the
menus' art, textures and a palette written while the draws that read them may not have been run yet, every way a render or texture stage state is set,
the texture clip, the zoom's blend, the menus' offset, two viewports, and some two thousand light frames so that the texture cache drops what it has
not used. The game's own choices come from a generator of their own, so that the calls are the same whatever the GX2 thread's schedule.
*/

#include "replay.h"

#include <stdlib.h>
#include <string.h>

static uint32_t game_seed = 0x2545f491u;

static unsigned choose(unsigned count)
{
	return count ? replay_random(&game_seed) % count : 0;
}

static float coordinate(void)
{
	return (float)(int)(replay_random(&game_seed) % 20001) / 1000.0f - 10.0f;
}

static void bytes_fill(void *at, unsigned long size)
{
	unsigned char *byte = at;

	while (size--)
		*byte++ = (unsigned char)replay_random(&game_seed);
}

/* ---------- vertex shaders and their declarations (the library's tokens: the type in the top three bits) */

#define TOKEN_STREAM(n) ((1UL << 29) | (DWORD)(n))
#define TOKEN_REGISTER(reg, type) ((2UL << 29) | ((DWORD)(type) << 16) | (DWORD)(reg))
#define TOKEN_SKIP(dwords) ((2UL << 29) | (1UL << 28) | ((DWORD)(dwords) << 16))
#define TOKEN_END 0xFFFFFFFFUL

static DWORD shader_make(const DWORD *declaration, unsigned instructions, uint32_t program_seed)
{
	DWORD function[1 + 4 * 64];
	DWORD handle = 0;
	unsigned index;

	function[0] = ((DWORD)instructions << 16) | 0x2078;
	for (index = 0; index < instructions * 4; index++)
		function[1 + index] = replay_random(&program_seed);
	D3DDevice_CreateVertexShader(declaration, function, &handle, 0);
	return handle;
}

static DWORD vs_bsp, vs_model, vs_dynamic, vs_two, vs_skip, vs_immediate;

static void shaders_make(void)
{
	static const DWORD bsp[] = { TOKEN_STREAM(0), TOKEN_REGISTER(0, D3DVSDT_FLOAT3), TOKEN_REGISTER(3, D3DVSDT_NORMPACKED3),
		TOKEN_REGISTER(9, D3DVSDT_FLOAT2), TOKEN_REGISTER(10, D3DVSDT_FLOAT2), TOKEN_END };
	static const DWORD model[] = { TOKEN_STREAM(0), TOKEN_REGISTER(0, D3DVSDT_FLOAT3), TOKEN_REGISTER(1, D3DVSDT_NORMPACKED3),
		TOKEN_REGISTER(9, D3DVSDT_NORMSHORT2), TOKEN_REGISTER(2, D3DVSDT_PBYTE4), TOKEN_REGISTER(4, D3DVSDT_FLOAT2), TOKEN_END };
	static const DWORD dynamic[] = { TOKEN_STREAM(0), TOKEN_REGISTER(0, D3DVSDT_FLOAT3), TOKEN_REGISTER(3, D3DVSDT_D3DCOLOR),
		TOKEN_REGISTER(9, D3DVSDT_FLOAT2), TOKEN_END };
	static const DWORD two[] = { TOKEN_STREAM(0), TOKEN_REGISTER(0, D3DVSDT_FLOAT3), TOKEN_STREAM(1), TOKEN_REGISTER(9, D3DVSDT_FLOAT2),
		TOKEN_REGISTER(10, D3DVSDT_SHORT2), TOKEN_END };
	static const DWORD skip[] = { TOKEN_STREAM(0), TOKEN_REGISTER(0, D3DVSDT_FLOAT4), TOKEN_SKIP(2), TOKEN_REGISTER(9, D3DVSDT_FLOAT1),
		TOKEN_STREAM(2), TOKEN_REGISTER(3, D3DVSDT_D3DCOLOR), TOKEN_END };
	static const DWORD immediate[] = { TOKEN_END };

	vs_bsp = shader_make(bsp, 12, 1);
	vs_model = shader_make(model, 40, 2);
	vs_dynamic = shader_make(dynamic, 9, 3);
	vs_two = shader_make(two, 15, 4);
	vs_skip = shader_make(skip, 7, 5);
	vs_immediate = shader_make(immediate, 5, 6);
}

/* ---------- vertex and index data */

#define BSP_VERTICES 8192
#define BSP_STRIDE 32
#define MODELS 4
#define MODEL_VERTICES 1024
#define MODEL_STRIDE 32
#define TWO_STRIDE 12
#define DYNAMIC_BYTES (192 * 1024)
#define DYNAMIC_STRIDE 24
#define TRIANGLE_BYTES (6 * 24000)
#define FIXED_INDICES 3000
#define HEAP_INDICES 900
#define LONG_INDICES 140000

/* the map's buffers' headers, which the game keeps in its tag data, over vertices in the tag cache */
static D3DVertexBuffer vb_bsp, vb_models[MODELS], vb_two_static;
static D3DVertexBuffer *vb_dynamic, *vb_two_dynamic, *vb_color;
static D3DIndexBuffer ib_fixed[MODELS];
static D3DIndexBuffer *ib_triangles;
static WORD *heap_list, *long_list;
static unsigned long dynamic_used, triangles_used;

static unsigned char *tag_cache_take(unsigned long *used, unsigned long size)
{
	unsigned char *at = (unsigned char *)physical_memory_get_tag_cache_base_address() + *used;

	*used += (size + 4095) & ~4095UL;
	/* (as the map's file read into the tag cache announces it) */
	memory_watch_prepare_write(at, size);
	return at;
}

static void float_put(unsigned char *at, float value, int little)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	if (little)
		bits = (bits >> 24) | ((bits >> 8) & 0xff00u) | ((bits << 8) & 0xff0000u) | (bits << 24);
	memcpy(at, &bits, sizeof(bits));
}

static void vertices_fill(unsigned char *at, unsigned long count, unsigned long stride, int little)
{
	unsigned long vertex;

	bytes_fill(at, count * stride);
	for (vertex = 0; vertex < count; vertex++)
	{
		float_put(at + vertex * stride, coordinate(), little);
		float_put(at + vertex * stride + 4, coordinate(), little);
		float_put(at + vertex * stride + 8, coordinate(), little);
	}
}

static void map_buffer_make(D3DVertexBuffer *buffer, unsigned char *data)
{
	memset(buffer, 0, sizeof(*buffer));
	buffer->Common = D3DCOMMON_TYPE_VERTEXBUFFER | 1;
	buffer->Data = PLATFORM_VIRTUAL_TO_PHYSICAL(data);
}

static void buffers_make(void)
{
	unsigned long used = 0, model;
	unsigned char *at;

	at = tag_cache_take(&used, BSP_VERTICES * BSP_STRIDE);
	vertices_fill(at, BSP_VERTICES, BSP_STRIDE, 0);
	map_buffer_make(&vb_bsp, at);
	for (model = 0; model < MODELS; model++)
	{
		unsigned long index;
		WORD *list;

		/* (the models' vertices as the Xbox had them, little-endian, but one: the converter swaps the BSP's) */
		at = tag_cache_take(&used, MODEL_VERTICES * MODEL_STRIDE);
		vertices_fill(at, MODEL_VERTICES, MODEL_STRIDE, model != 3);
		map_buffer_make(&vb_models[model], at);
		/* the model's strips, where the map has them in the window */
		list = platform_contiguous_alloc(FIXED_INDICES * sizeof(WORD), 32, PLATFORM_ANY_PHYSICAL_ADDRESS, PAGE_READWRITE);
		memory_watch_prepare_write(list, FIXED_INDICES * sizeof(WORD));
		for (index = 0; index < FIXED_INDICES; index++)
			list[index] = (WORD)(index < 60 ? choose(64) : choose(MODEL_VERTICES));
		memset(&ib_fixed[model], 0, sizeof(ib_fixed[model]));
		ib_fixed[model].Common = D3DCOMMON_TYPE_INDEXBUFFER | 1;
		ib_fixed[model].Data = (DWORD)(uintptr_t)list;
	}
	at = tag_cache_take(&used, MODEL_VERTICES * TWO_STRIDE);
	vertices_fill(at, MODEL_VERTICES, TWO_STRIDE, 0);
	map_buffer_make(&vb_two_static, at);
	D3DDevice_CreateVertexBuffer(DYNAMIC_BYTES, 0, 0, 0, &vb_dynamic);
	D3DDevice_CreateVertexBuffer(MODEL_VERTICES * TWO_STRIDE, 0, 0, 0, &vb_two_dynamic);
	D3DDevice_CreateVertexBuffer(64, 0, 0, 0, &vb_color);
	D3DDevice_CreateIndexBuffer(TRIANGLE_BYTES, 0, D3DFMT_INDEX16, 0, &ib_triangles);
	heap_list = malloc(HEAP_INDICES * sizeof(WORD));
	long_list = malloc(LONG_INDICES * sizeof(WORD));
}

/* the dynamic vertex buffer's next vertices, written as the game writes its dynamic vertices (a lock of the range, then the vertices); their first's number */
static unsigned long dynamic_write(unsigned long count)
{
	BYTE *data;
	unsigned long first;

	if ((dynamic_used + count) * DYNAMIC_STRIDE > DYNAMIC_BYTES)
		dynamic_used = 0;
	first = dynamic_used;
	D3DVertexBuffer_Lock(vb_dynamic, (UINT)(first * DYNAMIC_STRIDE), (UINT)(count * DYNAMIC_STRIDE), &data, 0);
	vertices_fill(data, count, DYNAMIC_STRIDE, 0);
	dynamic_used += count;
	return first;
}

/* a list of the dynamic triangles (rasterizer_xbox_draw_primitives.c): written into the index buffer's store of now; where it is */
static WORD *triangles_write(unsigned long count, unsigned long vertices)
{
	BYTE *data;
	WORD *list;
	unsigned long index;

	if ((triangles_used + count) * sizeof(WORD) > TRIANGLE_BYTES)
		triangles_used = 0;
	D3DIndexBuffer_Lock(ib_triangles, (UINT)(triangles_used * sizeof(WORD)), (UINT)(count * sizeof(WORD)), &data, 0);
	list = (WORD *)(void *)data;
	for (index = 0; index < count; index++)
		list[index] = (WORD)choose((unsigned)vertices);
	triangles_used += count;
	return list;
}

/* ---------- textures */

enum
{
	T_DXT1, T_DXT5, T_SWIZZLED, T_LINEAR, T_PALETTED, T_CUBE, T_VOLUME, T_REWRITTEN, T_REWRITTEN_DXT, T_ATLAS, T_PLACEHOLDER, T_OVERRIDE, T_MENU,
	T_TARGET, T_RIPPLE, T_DEPTH16, T_COUNT
};

static D3DBaseTexture *textures[T_COUNT];
static D3DBaseTexture map_texture;
static D3DPalette *palette;
static D3DSurface *back_buffer, *depth_buffer, *target_surface, *ripple_surfaces[4], *depth16_surface;

static void level_fill(D3DTexture *texture, UINT level)
{
	D3DLOCKED_RECT locked;
	D3DSURFACE_DESC description;

	D3DTexture_GetLevelDesc(texture, level, &description);
	D3DTexture_LockRect(texture, level, &locked, NULL, 0);
	if (locked.pBits)
		bytes_fill(locked.pBits, description.Size);
}

static D3DBaseTexture *texture_make(UINT width, UINT height, UINT levels, D3DFORMAT format)
{
	D3DTexture *texture = NULL;
	UINT level;

	D3DDevice_CreateTexture(width, height, levels, 0, format, 0, &texture);
	for (level = 0; level < levels; level++)
		level_fill(texture, level);
	return (D3DBaseTexture *)texture;
}

static void textures_make(void)
{
	D3DCubeTexture *cube = NULL;
	D3DVolumeTexture *volume = NULL;
	D3DCOLOR *colors;
	unsigned face, level;
	unsigned char *texels;

	textures[T_DXT1] = texture_make(256, 256, 9, D3DFMT_DXT1);
	textures[T_DXT5] = texture_make(64, 64, 7, D3DFMT_DXT5);
	textures[T_SWIZZLED] = texture_make(64, 64, 1, D3DFMT_A8R8G8B8);
	textures[T_LINEAR] = texture_make(100, 60, 1, D3DFMT_LIN_A8R8G8B8);
	textures[T_PALETTED] = texture_make(32, 32, 1, D3DFMT_P8);
	textures[T_REWRITTEN] = texture_make(32, 32, 1, D3DFMT_A8R8G8B8);
	textures[T_REWRITTEN_DXT] = texture_make(64, 64, 1, D3DFMT_DXT1);
	textures[T_ATLAS] = texture_make(REPLAY_ATLAS_SIZE, REPLAY_ATLAS_SIZE, 1, D3DFMT_A8);
	textures[T_PLACEHOLDER] = texture_make(32, 32, 1, D3DFMT_A8R8G8B8);
	textures[T_OVERRIDE] = texture_make(32, 32, 1, D3DFMT_A8R8G8B8);
	textures[T_MENU] = texture_make(64, 32, 1, D3DFMT_A8R8G8B8);
	textures[T_TARGET] = texture_make(128, 128, 1, D3DFMT_A8R8G8B8);
	textures[T_RIPPLE] = texture_make(64, 64, 4, D3DFMT_A8R8G8B8);
	textures[T_DEPTH16] = texture_make(128, 128, 1, D3DFMT_LIN_D16);
	D3DDevice_CreateCubeTexture(32, 6, 0, D3DFMT_DXT1, 0, &cube);
	for (face = 0; face < 6; face++)
		for (level = 0; level < 6; level++)
		{
			D3DLOCKED_RECT locked;

			D3DCubeTexture_LockRect(cube, (D3DCUBEMAP_FACES)face, level, &locked, NULL, 0);
			bytes_fill(locked.pBits, (unsigned long)locked.Pitch * (((32u >> level) + 3) / 4 ? ((32u >> level) + 3) / 4 : 1));
		}
	textures[T_CUBE] = (D3DBaseTexture *)cube;
	D3DDevice_CreateVolumeTexture(16, 16, 4, 1, 0, D3DFMT_A8R8G8B8, 0, &volume);
	{
		D3DLOCKED_BOX locked;

		D3DVolumeTexture_LockBox(volume, 0, &locked, NULL, 0);
		bytes_fill(locked.pBits, (unsigned long)locked.SlicePitch * 4);
	}
	textures[T_VOLUME] = (D3DBaseTexture *)volume;
	D3DDevice_CreatePalette(D3DPALETTE_256, &palette);
	D3DPalette_Lock(palette, &colors, 0);
	bytes_fill(colors, 256 * sizeof(D3DCOLOR));
	/* a bitmap of the map's: its header in the tag data, its texels in the texture cache's memory (read from the file) */
	texels = platform_contiguous_alloc(128 * 128 / 2 * 2, 128, PLATFORM_ANY_PHYSICAL_ADDRESS, PAGE_READWRITE);
	memory_watch_prepare_write(texels, 128 * 128);
	bytes_fill(texels, 128 * 128);
	memset(&map_texture, 0, sizeof(map_texture));
	map_texture.Common = D3DCOMMON_TYPE_TEXTURE | 1;
	map_texture.Data = PLATFORM_VIRTUAL_TO_PHYSICAL(texels);
	map_texture.Format = (7UL << D3DFORMAT_USIZE_SHIFT) | (7UL << D3DFORMAT_VSIZE_SHIFT) | ((DWORD)D3DFMT_DXT1 << D3DFORMAT_FORMAT_SHIFT) |
		(8UL << D3DFORMAT_MIPMAP_SHIFT) | (2UL << D3DFORMAT_DIMENSION_SHIFT) | D3DFORMAT_DMACHANNEL_A;
	replay_atlas_data = textures[T_ATLAS]->Data;
	replay_hud_placeholder_data = textures[T_PLACEHOLDER]->Data;
	replay_hud_override_address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(textures[T_OVERRIDE]->Data);
	replay_menu_data = textures[T_MENU]->Data;
	D3DDevice_GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back_buffer);
	D3DDevice_GetDepthStencilSurface(&depth_buffer);
	D3DTexture_GetSurfaceLevel((D3DTexture *)textures[T_TARGET], 0, &target_surface);
	for (level = 0; level < 4; level++)
		D3DTexture_GetSurfaceLevel((D3DTexture *)textures[T_RIPPLE], level, &ripple_surfaces[level]);
	D3DTexture_GetSurfaceLevel((D3DTexture *)textures[T_DEPTH16], 0, &depth16_surface);
}

/* a texture rewritten as the game writes one (a lock of the level): the draws before must see what was there */
static void texture_rewrite(D3DBaseTexture *texture)
{
	level_fill((D3DTexture *)texture, 0);
}

/* ---------- state, set every way the game sets it */

static void pixel_shader_set(unsigned which, DWORD modes)
{
	D3DPIXELSHADERDEF definition;
	uint32_t seed = 0x1000u + which;
	unsigned index;

	memset(&definition, 0, sizeof(definition));
	for (index = 0; index < 8; index++)
	{
		definition.PSAlphaInputs[index] = replay_random(&seed);
		definition.PSAlphaOutputs[index] = replay_random(&seed) & 0x3ffff;
		definition.PSRGBInputs[index] = replay_random(&seed);
		definition.PSRGBOutputs[index] = replay_random(&seed) & 0x3ffff;
		definition.PSConstant0[index] = replay_random(&seed);
		definition.PSConstant1[index] = replay_random(&seed);
	}
	definition.PSFinalCombinerInputsABCD = replay_random(&seed);
	definition.PSFinalCombinerInputsEFG = replay_random(&seed);
	definition.PSCompareMode = replay_random(&seed) & 0xffff;
	definition.PSFinalCombinerConstant0 = replay_random(&seed);
	definition.PSFinalCombinerConstant1 = replay_random(&seed);
	definition.PSCombinerCount = PS_COMBINERCOUNT(1 + which % 8, 0);
	definition.PSTextureModes = modes;
	definition.PSDotMapping = 0;
	definition.PSInputTexture = 0;
	D3DDevice_SetPixelShaderProgram(&definition);
}

static void render_states_vary(unsigned count)
{
	static const D3DRENDERSTATETYPE simple[] = { D3DRS_ZFUNC, D3DRS_ALPHAFUNC, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF,
		D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_ZWRITEENABLE, D3DRS_COLORWRITEENABLE, D3DRS_STENCILZFAIL, D3DRS_STENCILPASS, D3DRS_STENCILFUNC,
		D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK, D3DRS_BLENDOP, D3DRS_BLENDCOLOR, D3DRS_PSCONSTANT0_3, D3DRS_PSFINALCOMBINERCONSTANT1 };
	static const D3DRENDERSTATETYPE deferred[] = { D3DRS_FOGENABLE, D3DRS_FOGTABLEMODE, D3DRS_FOGSTART, D3DRS_FOGEND, D3DRS_FOGDENSITY, D3DRS_POINTSIZE };
	static const D3DRENDERSTATETYPE complex[] = { D3DRS_FOGCOLOR, D3DRS_CULLMODE, D3DRS_FRONTFACE, D3DRS_ZENABLE, D3DRS_STENCILENABLE, D3DRS_FILLMODE,
		D3DRS_STENCILFAIL, D3DRS_TEXTUREFACTOR };
	static const DWORD blends[] = { D3DBLEND_ZERO, D3DBLEND_ONE, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLEND_CONSTANTCOLOR };
	static const DWORD compares[] = { D3DCMP_LESSEQUAL, D3DCMP_ALWAYS, D3DCMP_EQUAL, D3DCMP_GREATER };

	while (count--)
	{
		const unsigned kind = choose(10);

		if (kind < 5)
		{
			const D3DRENDERSTATETYPE state = simple[choose(sizeof(simple) / sizeof(simple[0]))];
			DWORD value = replay_random(&game_seed) & 0xff;

			if (state == D3DRS_SRCBLEND || state == D3DRS_DESTBLEND)
				value = blends[choose(5)];
			else if (state == D3DRS_ZFUNC || state == D3DRS_ALPHAFUNC || state == D3DRS_STENCILFUNC)
				value = compares[choose(4)];
			else if (state == D3DRS_COLORWRITEENABLE)
				value = choose(16);
			else if (state == D3DRS_ALPHABLENDENABLE || state == D3DRS_ZWRITEENABLE || state == D3DRS_ALPHATESTENABLE)
				value = choose(2);
			D3DDevice_SetRenderState(state, value);
		}
		else if (kind < 7)
			D3DDevice_SetRenderState(deferred[choose(sizeof(deferred) / sizeof(deferred[0]))], replay_random(&game_seed));
		else if (kind < 9)
		{
			const D3DRENDERSTATETYPE state = complex[choose(sizeof(complex) / sizeof(complex[0]))];
			DWORD value = replay_random(&game_seed);

			if (state == D3DRS_CULLMODE)
				value = choose(3) == 0 ? D3DCULL_NONE : choose(2) ? D3DCULL_CW : D3DCULL_CCW;
			else if (state == D3DRS_FRONTFACE)
				value = choose(2) ? D3DFRONT_CW : D3DFRONT_CCW;
			else if (state == D3DRS_ZENABLE || state == D3DRS_STENCILENABLE)
				value = choose(2);
			else if (state == D3DRS_FILLMODE)
				value = choose(4) ? D3DFILL_SOLID : D3DFILL_WIREFRAME;
			D3DDevice_SetRenderState(state, value);
		}
		else
		{
			/* (the ways round the inline function: the renderer's own NotInline calls, the deferred function itself, the z bias) */
			const unsigned way = choose(3);

			if (way == 0)
				D3DDevice_SetRenderStateNotInline(choose(2) ? D3DRS_ALPHAREF : D3DRS_ZBIAS, choose(256));
			else if (way == 1)
				D3DDevice_SetRenderState_Deferred(D3DRS_FOGEND, replay_random(&game_seed));
			else
				D3DDevice_SetRenderState(D3DRS_ZBIAS, choose(3) ? 0 : 8);
		}
	}
}

static void texture_states_vary(unsigned count)
{
	static const DWORD filters[] = { D3DTEXF_POINT, D3DTEXF_LINEAR, D3DTEXF_ANISOTROPIC };
	static const DWORD addresses[] = { D3DTADDRESS_WRAP, D3DTADDRESS_MIRROR, D3DTADDRESS_CLAMP, D3DTADDRESS_BORDER };

	while (count--)
	{
		const DWORD stage = choose(4);
		const unsigned kind = choose(9);

		if (kind == 0)
			D3DDevice_SetTextureStageState(stage, D3DTSS_MINFILTER, filters[choose(3)]);
		else if (kind == 1)
			D3DDevice_SetTextureStageState(stage, D3DTSS_MAGFILTER, filters[choose(2)]);
		else if (kind == 2)
			D3DDevice_SetTextureStageState(stage, D3DTSS_MIPFILTER, choose(3));
		else if (kind == 3)
			D3DDevice_SetTextureStageState(stage, D3DTSS_ADDRESSU, addresses[choose(4)]);
		else if (kind == 4)
			D3DDevice_SetTextureStageState(stage, D3DTSS_BORDERCOLOR, replay_random(&game_seed));
		else if (kind == 5)
			D3DDevice_SetTextureStageState(stage, D3DTSS_ALPHAKILL, choose(2) ? D3DTALPHAKILL_ENABLE : 0);
		else if (kind == 6)
			D3DDevice_SetTextureStageState(stage, D3DTSS_BUMPENVMAT01, replay_random(&game_seed));
		else if (kind == 7)
			D3DDevice_SetTextureStageState(stage, choose(2) ? D3DTSS_TEXCOORDINDEX : D3DTSS_COLORKEYCOLOR, choose(4));
		else
			/* (as the environment's code calls it) */
			D3DDevice_SetTextureState_Deferred(stage, D3DTSS_ADDRESSV, addresses[choose(4)]);
	}
}

static void constants_vary(unsigned registers)
{
	float values[64][4];
	const INT first = (INT)choose(150) - 96;
	unsigned index;

	if (registers > 64)
		registers = 64;
	for (index = 0; index < registers; index++)
	{
		values[index][0] = coordinate();
		values[index][1] = coordinate();
		values[index][2] = choose(4) ? coordinate() : 0.0f;
		values[index][3] = 1.0f;
	}
	D3DDevice_SetVertexShaderConstant(first, values, registers);
}

static void viewport_set(DWORD x, DWORD y, DWORD width, DWORD height)
{
	D3DVIEWPORT8 viewport;

	viewport.X = x;
	viewport.Y = y;
	viewport.Width = width;
	viewport.Height = height;
	viewport.MinZ = 0.0f;
	viewport.MaxZ = 1.0f;
	D3DDevice_SetViewport(&viewport);
}

/* ---------- the passes of a frame */

static unsigned long frame_number;

static void bsp_pass(unsigned draws)
{
	unsigned draw;

	D3DDevice_SetVertexShader(vs_bsp);
	D3DDevice_SetStreamSource(0, &vb_bsp, BSP_STRIDE);
	D3DDevice_SetIndices(ib_triangles, (UINT)choose(64));
	pixel_shader_set(0, PS_TEXTUREMODES(1, 1, 0, 0));
	D3DDevice_SetTexture(0, textures[T_DXT1]);
	D3DDevice_SetTexture(1, textures[T_SWIZZLED]);
	D3DDevice_SetTexture(2, NULL);
	D3DDevice_SetTexture(3, NULL);
	for (draw = 0; draw < draws; draw++)
	{
		const unsigned long count = 3 * (1 + choose(300));
		const WORD *list = triangles_write(count, BSP_VERTICES - 64);

		if (choose(3) == 0)
			render_states_vary(1 + choose(3));
		if (choose(4) == 0)
			D3DDevice_SetTexture(1, choose(2) ? textures[T_SWIZZLED] : (D3DBaseTexture *)&map_texture);
		D3DDevice_DrawIndexedVertices(D3DPT_TRIANGLELIST, (UINT)count, list);
	}
}

static void model_pass(unsigned models)
{
	unsigned model;

	D3DDevice_SetVertexShader(vs_model);
	for (model = 0; model < models; model++)
	{
		const unsigned which = choose(MODELS);
		const unsigned long start = choose(FIXED_INDICES - 400), count = 3 + choose(390);
		static const unsigned stage0[] = { T_DXT5, T_CUBE, T_VOLUME, T_DXT1, T_REWRITTEN_DXT };

		D3DDevice_SetStreamSource(0, &vb_models[which], MODEL_STRIDE);
		D3DDevice_SetIndices(&ib_fixed[which], 0);
		D3DDevice_SetTexture(0, textures[stage0[choose(5)]]);
		D3DDevice_SetTexture(1, choose(2) ? textures[T_DXT1] : NULL);
		if (choose(2))
		{
			static const DWORD modes[] = { PS_TEXTUREMODES(1, 1, 0, 0), PS_TEXTUREMODES(3, 1, 0, 0), PS_TEXTUREMODES(2, 0, 0, 4), PS_TEXTUREMODES(1, 0, 0, 0) };
			const unsigned which_modes = choose(4);

			pixel_shader_set(1 + choose(5), modes[which_modes]);
		}
		constants_vary(4 + choose(30));
		if (choose(2))
			texture_states_vary(1 + choose(4));
		if (choose(2))
			render_states_vary(1 + choose(2));
		/* (as DrawIndexedPrimitive does: the list from the index buffer's data on) */
		D3DDevice_DrawIndexedVertices(choose(4) ? D3DPT_TRIANGLESTRIP : D3DPT_TRIANGLELIST, (UINT)(count - count % 3),
			(const WORD *)(uintptr_t)ib_fixed[which].Data + start);
	}
	/* (a program loaded at an address of its own and selected with another's declaration) */
	D3DDevice_LoadVertexShader(vs_model, 10);
	D3DDevice_SelectVertexShader(vs_bsp, 10);
	D3DDevice_SetStreamSource(0, &vb_bsp, BSP_STRIDE);
	D3DDevice_DrawVertices(D3DPT_TRIANGLESTRIP, (UINT)choose(4000), 30);
	D3DDevice_SelectVertexShader(vs_bsp, 0);
}

static void dynamic_pass(unsigned batches)
{
	unsigned batch;

	D3DDevice_SetVertexShader(vs_dynamic);
	D3DDevice_SetStreamSource(0, vb_dynamic, DYNAMIC_STRIDE);
	D3DDevice_SetTexture(0, textures[T_REWRITTEN]);
	D3DDevice_SetTexture(1, textures[T_PALETTED]);
	D3DDevice_SetPalette(1, palette);
	D3DDevice_SetTexture(2, textures[T_LINEAR]);
	pixel_shader_set(7, PS_TEXTUREMODES(1, 1, 1, 0));
	for (batch = 0; batch < batches; batch++)
	{
		const unsigned long count = 4 * (2 + choose(40));
		const unsigned long first = dynamic_write(count);
		const unsigned way = choose(4);

		render_states_vary(choose(3));
		if (way == 0)
			D3DDevice_DrawVertices(choose(2) ? D3DPT_TRIANGLELIST : D3DPT_QUADLIST, (UINT)first, (UINT)count);
		else if (way == 1)
		{
			/* (dynamic triangles over the dynamic vertices: rasterizer_draw_dynamic_triangles_dynamic_vertices) */
			const WORD *list = triangles_write(3 * (count / 2), count);

			D3DDevice_SetIndices(ib_triangles, (UINT)first);
			D3DDevice_DrawIndexedVertices(D3DPT_TRIANGLELIST, (UINT)(3 * (count / 2)), list);
		}
		else if (way == 2)
		{
			/* (a model's list over dynamic vertices: rasterizer_draw_static_triangles_dynamic_vertices) */
			D3DDevice_SetIndices(&ib_fixed[0], (UINT)first);
			D3DDevice_DrawIndexedVertices(D3DPT_TRIANGLESTRIP, 60, (const WORD *)(uintptr_t)ib_fixed[0].Data);
		}
		else
		{
			/* (a list the game makes in its own memory and makes again at once: copied into the command) */
			unsigned long index, length = 3 * (1 + choose(HEAP_INDICES / 3 - 1));

			for (index = 0; index < length; index++)
				heap_list[index] = (WORD)choose((unsigned)count);
			D3DDevice_SetIndices(&ib_fixed[1], (UINT)first);
			D3DDevice_DrawIndexedVertices(D3DPT_TRIANGLELIST, (UINT)length, heap_list);
			for (index = 0; index < length; index++)
				heap_list[index] = 0xffff;
		}
		replay_gx2_moment();
	}
	if (frame_number % 3 == 0)
	{
		D3DCOLOR *colors;

		D3DPalette_Lock(palette, &colors, 0);
		bytes_fill(colors, 256 * sizeof(D3DCOLOR));
	}
	texture_rewrite(textures[T_REWRITTEN]);
	D3DDevice_SetPalette(1, NULL);
}

static void streams_pass(void)
{
	BYTE *data;
	unsigned long count = 64 + choose(200);

	/* two streams, the second the game's */
	D3DVertexBuffer_Lock(vb_two_dynamic, 0, (UINT)(count * TWO_STRIDE), &data, 0);
	vertices_fill(data, count, TWO_STRIDE, 0);
	D3DDevice_SetVertexShader(vs_two);
	D3DDevice_SetStreamSource(0, &vb_two_static, TWO_STRIDE);
	D3DDevice_SetStreamSource(1, vb_two_dynamic, TWO_STRIDE);
	D3DDevice_SetIndices(&ib_fixed[2], 0);
	D3DDevice_DrawIndexedVertices(D3DPT_TRIANGLELIST, 57, (const WORD *)(uintptr_t)ib_fixed[2].Data);
	D3DDevice_DrawVertices(D3DPT_LINESTRIP, 0, (UINT)count);
	/* a skip and a stream of one value for every vertex */
	D3DVertexBuffer_Lock(vb_color, 0, 4, &data, 0);
	bytes_fill(data, 4);
	D3DDevice_SetVertexShader(vs_skip);
	D3DDevice_SetStreamSource(0, &vb_models[1], 28);
	D3DDevice_SetStreamSource(2, vb_color, 0);
	D3DDevice_DrawVertices(D3DPT_POINTLIST, (UINT)choose(100), 40);
	D3DDevice_SetStreamSource(1, NULL, 0);
	D3DDevice_SetStreamSource(2, NULL, 0);
	/* now and then a list too long to go in a command */
	if (frame_number % 7 == 3)
	{
		unsigned long index;

		for (index = 0; index < LONG_INDICES; index++)
			long_list[index] = (WORD)choose(BSP_VERTICES);
		D3DDevice_SetVertexShader(vs_bsp);
		D3DDevice_SetStreamSource(0, &vb_bsp, BSP_STRIDE);
		D3DDevice_SetIndices(ib_triangles, 0);
		D3DDevice_DrawIndexedVertices(D3DPT_TRIANGLELIST, LONG_INDICES - LONG_INDICES % 3, long_list);
		memset(long_list, 0, LONG_INDICES * sizeof(WORD));
	}
}

static void target_pass(void)
{
	unsigned level;

	/* into a texture, then drawn with it */
	D3DDevice_SetRenderTarget(target_surface, depth16_surface);
	D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff203040, 1.0f, 0);
	D3DDevice_SetVertexShader(vs_bsp);
	D3DDevice_SetStreamSource(0, &vb_bsp, BSP_STRIDE);
	D3DDevice_SetTexture(0, textures[T_DXT1]);
	D3DDevice_DrawVertices(D3DPT_TRIANGLELIST, (UINT)choose(4000), 99);
	/* the water's ripples: a level at a time, then the levels as one texture */
	for (level = 0; level < 4; level++)
	{
		D3DDevice_SetRenderTarget(ripple_surfaces[level], NULL);
		D3DDevice_SetTexture(0, level ? textures[T_TARGET] : textures[T_SWIZZLED]);
		D3DDevice_DrawVertices(D3DPT_QUADLIST, (UINT)choose(4000), 4);
	}
	D3DDevice_SetRenderTarget(back_buffer, depth_buffer);
	viewport_set(0, 0, (DWORD)halo_screen_width(), 480);
	D3DDevice_SetTexture(0, textures[T_TARGET]);
	D3DDevice_SetTexture(1, textures[T_RIPPLE]);
	pixel_shader_set(0, PS_TEXTUREMODES(1, 1, 0, 0));
	D3DDevice_DrawVertices(D3DPT_TRIANGLELIST, (UINT)choose(4000), 33);
	D3DDevice_SetTexture(1, NULL);
}

static void immediate_pass(void)
{
	static struct { float position[3]; uint32_t texcoord; } decals[64];
	unsigned vertex, batch;

	D3DDevice_SetVertexShader(vs_immediate);
	D3DDevice_SetTexture(0, textures[T_SWIZZLED]);
	D3DDevice_Begin(D3DPT_TRIANGLEFAN);
	for (vertex = 0; vertex < 6 + choose(10); vertex++)
	{
		D3DDevice_SetVertexDataColor(D3DVSDE_DIFFUSE, replay_random(&game_seed));
		D3DDevice_SetVertexData2f(D3DVSDE_TEXCOORD0, coordinate(), coordinate());
		if (vertex == 3)
			D3DDevice_SetVertexData4ub(D3DVSDE_SPECULAR, 1, 2, 3, 4);
		D3DDevice_SetVertexData4f(D3DVSDE_VERTEX, coordinate(), coordinate(), coordinate(), 1.0f);
	}
	D3DDevice_End();
	/* the decals: batches of quads, and one batch spoiled by a vertex made another way */
	for (batch = 0; batch < 2; batch++)
	{
		D3DDevice_Begin(D3DPT_QUADLIST);
		if (batch == 1 && frame_number % 2)
			D3DDevice_SetVertexData4f(D3DVSDE_VERTEX, coordinate(), coordinate(), coordinate(), 1.0f);
		for (vertex = 0; vertex < 1 + choose(5); vertex++)
		{
			unsigned char color[4];
			unsigned count = 4 * (1 + choose(4)), index;

			bytes_fill(color, 4);
			for (index = 0; index < count; index++)
			{
				decals[index].position[0] = coordinate();
				decals[index].position[1] = coordinate();
				decals[index].position[2] = coordinate();
				decals[index].texcoord = replay_random(&game_seed);
			}
			wiiu_decal_vertices(color, decals, (long)count);
		}
		D3DDevice_End();
	}
	/* a lens flare's test: its quad counted by nothing while the queries are off */
	D3DDevice_BeginVisibilityTest();
	D3DDevice_Begin(D3DPT_QUADLIST);
	for (vertex = 0; vertex < 4; vertex++)
		D3DDevice_SetVertexData2s(D3DVSDE_VERTEX, (SHORT)choose(600), (SHORT)choose(400));
	D3DDevice_End();
	D3DDevice_EndVisibilityTest(1 + choose(100));
	{
		UINT result;

		D3DDevice_GetVisibilityTestResult(5, &result, NULL);
	}
}

static void interface_pass(void)
{
	unsigned draw;

	halo_screen_ui_offset(TRUE);
	D3DDevice_SetVertexShader(vs_dynamic);
	D3DDevice_SetStreamSource(0, vb_dynamic, DYNAMIC_STRIDE);
	pixel_shader_set(2, PS_TEXTUREMODES(1, 0, 0, 0));
	for (draw = 0; draw < 6; draw++)
	{
		static const unsigned which[] = { T_ATLAS, T_PLACEHOLDER, T_OVERRIDE, T_MENU, T_ATLAS, T_REWRITTEN_DXT };
		const unsigned long first = dynamic_write(8);

		/* (the text draws its glyphs into the atlas as it goes) */
		if (which[draw] == T_ATLAS)
			replay_atlas_draw(&game_seed);
		D3DDevice_SetTexture(0, textures[which[draw]]);
		D3DDevice_DrawVertices(D3DPT_QUADLIST, (UINT)first, 8);
	}
	halo_screen_ui_offset(FALSE);
	/* the menus' art comes and goes */
	if (frame_number == 5 || frame_number == 20 || frame_number == 2462)
		replay_menu_serial++;
}

static void effects_pass(void)
{
	D3DDevice_SetVertexShader(vs_bsp);
	D3DDevice_SetStreamSource(0, &vb_bsp, BSP_STRIDE);
	D3DDevice_SetTexture(0, textures[T_DXT1]);
	halo_texture_clip(TRUE, 0.25f, 0.75f);
	D3DDevice_DrawVertices(D3DPT_TRIANGLELIST, (UINT)choose(4000), 30);
	halo_texture_clip(FALSE, 0.0f, 0.0f);
	halo_screen_warp_blend(1);
	D3DDevice_DrawVertices(D3DPT_TRIANGLELIST, (UINT)choose(4000), 30);
	halo_screen_warp_blend(0);
	if (frame_number % 5 == 1)
	{
		D3DDevice_SetShaderConstantMode(D3DSCM_NORESERVEDCONSTANTS);
		D3DDevice_DrawVertices(D3DPT_TRIANGLELIST, (UINT)choose(4000), 30);
		D3DDevice_SetShaderConstantMode(0);
	}
	if (frame_number % 4 == 2)
	{
		/* (compressed texels read where the game keeps them, rewritten after a draw read them) */
		D3DDevice_SetTexture(0, textures[T_REWRITTEN_DXT]);
		D3DDevice_DrawVertices(D3DPT_TRIANGLELIST, (UINT)choose(4000), 30);
		texture_rewrite(textures[T_REWRITTEN_DXT]);
	}
}

static void frame_begin(void)
{
	/* (rasterizer_dynamic_geometry_begin) */
	wiiu_index_buffer_renew(ib_triangles, TRIANGLE_BYTES);
	triangles_used = 0;
	dynamic_used = 0;
	D3DDevice_SetRenderTarget(back_buffer, depth_buffer);
	viewport_set(0, 0, (DWORD)halo_screen_width(), 480);
	D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xff000000u | frame_number, 1.0f, 0);
}

/* the submission each resource was last used in (the Lock field, resource_used in d3d8_gx2.c), into a digest of its own: nothing reads it, but it is
kept as the Xbox's runtime keeps it */
static void locks_note(void)
{
	DWORD locks[T_COUNT + 2 * MODELS + 14];
	unsigned count = 0, index;

	for (index = 0; index < T_COUNT; index++)
		locks[count++] = textures[index]->Lock;
	for (index = 0; index < MODELS; index++)
	{
		locks[count++] = vb_models[index].Lock;
		locks[count++] = ib_fixed[index].Lock;
	}
	locks[count++] = vb_bsp.Lock;
	locks[count++] = vb_two_static.Lock;
	locks[count++] = vb_dynamic->Lock;
	locks[count++] = vb_two_dynamic->Lock;
	locks[count++] = vb_color->Lock;
	locks[count++] = ib_triangles->Lock;
	locks[count++] = palette->Lock;
	locks[count++] = map_texture.Lock;
	locks[count++] = back_buffer->Lock;
	locks[count++] = depth_buffer->Lock;
	locks[count++] = target_surface->Lock;
	locks[count++] = ripple_surfaces[0]->Lock ^ ripple_surfaces[1]->Lock << 8 ^ ripple_surfaces[2]->Lock << 16 ^ ripple_surfaces[3]->Lock << 24;
	locks[count++] = depth16_surface->Lock;
	locks[count++] = (DWORD)frame_number;
	replay_locks = replay_hash(replay_locks, locks, count * sizeof(locks[0]));
}

static void frame_end(void)
{
	D3DDevice_Present(NULL, NULL, NULL, NULL);
	locks_note();
	halo_screen_commit();
	replay_gx2_moment();
}

static void frame_full(void)
{
	frame_begin();
	halo_render_marker("structure");
	bsp_pass(8 + choose(8));
	replay_gx2_moment();
	model_pass(6 + choose(6));
	replay_gx2_moment();
	dynamic_pass(4 + choose(6));
	streams_pass();
	replay_gx2_moment();
	if (frame_number % 2 == 0)
		target_pass();
	immediate_pass();
	effects_pass();
	/* the second window of split screen, with its own view */
	viewport_set(0, 240, (DWORD)halo_screen_width(), 240);
	{
		D3DRECT rectangle = { 10, 250, 200, 400 };

		D3DDevice_Clear(1, &rectangle, D3DCLEAR_TARGET_R | D3DCLEAR_TARGET_G | D3DCLEAR_TARGET_B, 0x00ff8040, 1.0f, 0);
	}
	bsp_pass(3);
	model_pass(3);
	interface_pass();
	if (frame_number % 6 == 5)
		D3DDevice_KickPushBuffer();
	if (frame_number % 9 == 4)
		(void)D3DDevice_IsBusy();
	if (frame_number % 11 == 7)
		halo_switch_renderer_idle();
	frame_end();
}

/* a frame of one draw and a clear, to go on to the texture cache's drops */
static void frame_light(void)
{
	frame_begin();
	D3DDevice_SetVertexShader(vs_bsp);
	D3DDevice_SetStreamSource(0, &vb_bsp, BSP_STRIDE);
	D3DDevice_SetIndices(ib_triangles, 0);
	D3DDevice_SetTexture(0, textures[T_DXT1]);
	D3DDevice_SetTexture(1, NULL);
	D3DDevice_SetTexture(2, NULL);
	D3DDevice_DrawIndexedVertices(D3DPT_TRIANGLELIST, 30, triangles_write(30, 100));
	frame_end();
}

void replay_game_run(unsigned long frames)
{
	D3DPRESENT_PARAMETERS presentation;
	D3DDevice *device;

	memset(&presentation, 0, sizeof(presentation));
	presentation.BackBufferWidth = (UINT)halo_screen_width();
	presentation.BackBufferHeight = 480;
	Direct3D_CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &presentation, &device);
	/* (the rasterizer presents once as it starts, before anything is drawn: rasterizer_initialize. The layer decides then whether the streams go to the queue; the
	reads of a stream noted before that would be noted where the queue's hazard check does not look) */
	D3DDevice_Present(NULL, NULL, NULL, NULL);
	shaders_make();
	buffers_make();
	textures_make();
	for (frame_number = 0; frame_number < frames; frame_number++)
	{
		if (frame_number == 30)
			halo_stream_endian_forget();
		if (frame_number == 33)
		{
			/* (a BSP switch: the tag cache written while draws that read it may not have been run) */
			unsigned char *at = (unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(vb_bsp.Data) + 64 * BSP_STRIDE;

			memory_watch_prepare_write(at, 512 * BSP_STRIDE);
			vertices_fill(at, 512, BSP_STRIDE, 0);
		}
		if (frame_number == 35)
			replay_screen_height = 1080;
		if (frame_number < 40 || frame_number >= 2450)
			frame_full();
		else
			frame_light();
	}
}
