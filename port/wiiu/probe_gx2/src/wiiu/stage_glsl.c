/*
The CafeGLSL stage: the compiler that runs on the console. CafeGLSL (Exzap/CafeGLSL, a fork of Mesa) is a library linked
into the program; this calls it the way its README says (InitGLSLCompiler, CompileShaderPair, FreeShaders,
DestroyGLSLCompiler) on the shaders OpenCE's generator writes for the ES path, times each compile, reports the compiler's
log, whether the vertex outputs and the pixel inputs meet, and draws a triangle with the generator's vs00 and a diffuse
pass-through pixel shader -- the pair the Mac's compiler made too, so that the two can be compared byte for byte.

It is the last stage: it runs 22 MB of someone else's code, on a thread with a large stack (Mesa recurses), and the
report is written before every compile.
*/

#include "../iface_check.h"
#include "crash.h"
#include "stages.h"

#include <cafeglsl/CafeGLSLCompiler.h>
#include <coreinit/cache.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <coreinit/thread.h>
#include <stdio.h>
#include <string.h>

#define STACK_BYTES (4u << 20)
#define LOG_BYTES 4096

typedef struct pair_spec
{
	const char *label;
	const char *brief;
	unsigned vs, ps;
} pair_spec;

static const pair_spec pairs[] =
{
	{ "attr (the probe's own, GLSL 330)", "attr", GLSL_ATTR_VS, GLSL_ATTR_PS },
	{ "vs00 + ps_diffuse (the draw test)", "vs00+diffuse", GLSL_GEN_VS00, GLSL_GEN_PS_DIFFUSE },
	{ "vs00 + ps_text", "vs00+text", GLSL_GEN_VS00, GLSL_GEN_PS_TEXT },
	{ "vs00_packed + ps_diffuse", "vs00p+diffuse", GLSL_GEN_VS00_PACKED, GLSL_GEN_PS_DIFFUSE },
	{ "vs09 + ps_text_fog_alphatest", "vs09+fog", GLSL_GEN_VS09, GLSL_GEN_PS_TEXT_FOG_ALPHATEST },
	{ "vs09_lit + ps_text_lit_3d", "vs09lit+lit3d", GLSL_GEN_VS09_LIT, GLSL_GEN_PS_TEXT_LIT_3D },
	{ "vs31 + ps_text (the largest vertex program)", "vs31+text", GLSL_GEN_VS31, GLSL_GEN_PS_TEXT },
	{ "vs31_lit + ps_text_lit_3d", "vs31lit+lit3d", GLSL_GEN_VS31_LIT, GLSL_GEN_PS_TEXT_LIT_3D },
};
#define PAIR_COUNT ((unsigned)(sizeof(pairs) / sizeof(pairs[0])))
#define DRAW_PAIR 1

typedef struct pair_result
{
	int done;
	int compiled;
	GX2ShaderMode mode;
	uint32_t milliseconds;
	GX2VertexShader *vs;
	GX2PixelShader *ps;
	char log[200];
} pair_result;

static pair_result results[PAIR_COUNT];
static gx_session *job_session;
static int initialized;
static const char *version_text = "?";
static uint32_t heap_before, heap_after_init, heap_after_destroy;

static uint32_t mem2_free(void)
{
	MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2);

	return heap && heap->tag == MEM_EXPANDED_HEAP_TAG ? MEMGetTotalFreeSizeForExpHeap(heap) : 0;
}

/* ---------- running on a thread with a big stack */

static void (*big_function)(void);

static int big_entry(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	big_function();
	return 0;
}

static int run_big(void (*function)(void), char *error, size_t error_size)
{
	OSThread *thread = gx_alloc(sizeof(OSThread), 16);
	void *stack = gx_alloc(STACK_BYTES, 16);
	int result = 0;

	if (!thread || !stack)
	{
		gx_free(thread);
		gx_free(stack);
		snprintf(error, error_size, "no memory for a thread with a %u KiB stack", STACK_BYTES >> 10);
		return 0;
	}
	big_function = function;
	if (!OSCreateThread(thread, big_entry, 0, NULL, (uint8_t *)stack + STACK_BYTES, STACK_BYTES, OSGetThreadPriority(OSGetCurrentThread()), OS_THREAD_ATTRIB_AFFINITY_ANY))
	{
		gx_free(thread);
		gx_free(stack);
		snprintf(error, error_size, "OSCreateThread refused");
		return 0;
	}
	OSSetThreadName(thread, "cafeglsl");
	OSResumeThread(thread);
	OSJoinThread(thread, &result);
	gx_free(thread);
	gx_free(stack);
	return 1;
}

/* ---------- the compiling */

static void compile_job(void)
{
	gx_session *s = job_session;
	report *r = s->report;
	static char log[LOG_BYTES];
	unsigned i;

	/* an alignment exception (the 750 does not do a misaligned float access) is answered by making the access, and counted: the
	compiler is someone else's code, and if it has any this says so instead of ending the run */
	crash_recover_alignment(2);
	crash_set_stage("glsl: InitGLSLCompiler");
	heap_before = mem2_free();
	InitGLSLCompiler();
	initialized = 1;
	heap_after_init = mem2_free();
	version_text = GetGLSLCompilerVersion();
	report_value(r, "compiler version", "%s", version_text ? version_text : "(none)");
	report_value(r, "MEM2 free before, after init", "%u, %u bytes", heap_before, heap_after_init);
	s->flush();

	for (i = 0; i < PAIR_COUNT && s->keep_going(); i++)
	{
		pair_result *res = &results[i];
		uint64_t start, end;
		size_t l;

		crash_set_stage(pairs[i].label);
		report_note(r, "compiling %s: %u + %u bytes of GLSL", pairs[i].label, (unsigned)strlen(glsl_sources[pairs[i].vs].text),
			(unsigned)strlen(glsl_sources[pairs[i].ps].text));
		s->flush();
		log[0] = 0;
		start = s->now_ns();
		res->mode = CompileShaderPair(glsl_sources[pairs[i].vs].text, glsl_sources[pairs[i].ps].text, GLSL_COMPILE_AUTO, &res->vs, &res->ps, log, LOG_BYTES,
			GLSL_COMPILER_FLAG_NONE);
		end = s->now_ns();
		res->milliseconds = (uint32_t)((end - start) / 1000000u);
		res->done = 1;
		res->compiled = res->mode != GLSL_SHADER_MODE_ERROR && res->vs && res->ps;
		for (l = 0; l < sizeof(res->log) - 1 && log[l]; l++)
			res->log[l] = (log[l] == '\n' || log[l] == '\t') ? ' ' : log[l];
		res->log[l] = 0;
		report_check(r, res->compiled, "CafeGLSL compile", "%s: %s in %u ms, uniform mode %d%s%s", pairs[i].label, res->compiled ? "compiled" : "FAILED",
			(unsigned)res->milliseconds, res->compiled ? (int)res->mode : -1, res->log[0] ? ", log: " : "", res->log);
		if (res->compiled)
			report_shader_pair(r, pairs[i].label, res->vs, res->ps);
		s->flush();
	}
	crash_set_stage("glsl: compiled");
	{
		uint32_t refused;
		uint32_t made = crash_alignment_emulated(&refused);

		report_value(r, "alignment exceptions in the compiler", "%u taken, %u accesses made, %u refused", (unsigned)crash_alignment_traps(), (unsigned)made,
			(unsigned)refused);
	}
	crash_recover_alignment(0);
}

static void destroy_job(void)
{
	crash_set_stage("glsl: DestroyGLSLCompiler");
	DestroyGLSLCompiler();
	heap_after_destroy = mem2_free();
}

/* ---------- the draw test */

typedef struct generated_case
{
	const char *label;
	const GX2VertexShader *vs;
	const GX2PixelShader *ps;
	GX2ShaderMode mode;
	gx_fetch fetch;
	void *vertices;
	gx_texture shot; /* what the GPU drew, kept as a picture for the page */
	int drawn;
	int right;
} generated_case;

static generated_case gen_cases[2];

/* uploads a uniform the compiled vertex shader kept (it leaves out what the program does not use): as many elements of
floats_per_element as it holds, from values */
static void set_uniform(const GX2VertexShader *vs, const char *name, const float *values, unsigned floats_per_element)
{
	GX2UniformVar *var = GX2GetVertexUniformVar(vs, name);

	if (var)
		GX2SetVertexUniformReg(var->offset, var->count * floats_per_element, values);
}

static void draw_generated(const generated_case *g)
{
	/* the game's vertex program computes the clip position from c[0..3], then undoes Direct3D's screen-space conversion with
	c[58] and c[59] (c[-38] and c[-37]) and the viewport uniforms: these make it the identity */
	static float c[192][4];
	static const float scale[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	static const float offset[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	static const float zero = 0.0f;

	memset(c, 0, sizeof(c));
	c[0][0] = 1.0f;
	c[1][1] = 1.0f;
	c[2][2] = 1.0f;
	c[3][3] = 1.0f;
	c[58][0] = c[58][1] = c[58][2] = 1.0f;
	c[59][0] = c[59][1] = -0.5f;
	gx_state(0);
	gx_bind(g->vs, g->ps, &g->fetch.shader, g->mode);
	set_uniform(g->vs, "c", &c[0][0], 4);
	set_uniform(g->vs, "viewport_scale", scale, 4);
	set_uniform(g->vs, "viewport_offset", offset, 4);
	set_uniform(g->vs, "point_size", &zero, 1);
	set_uniform(g->vs, "screen_offset", &zero, 1);
	GX2SetAttribBuffer(0, 3 * 32, 32, g->vertices);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 3, 0, 1);
}

static void draw_shot(unsigned index, float width, float height)
{
	if (gen_cases[index].shot.ok)
		draw_texture_fill(session_current(), &gen_cases[index].shot.texture, width, height);
	ui_flush();
}

static int make_vertices(generated_case *g)
{
	float positions[3][4], colors[3][4], data[3][8];
	unsigned i;

	scene_generated(positions, colors);
	for (i = 0; i < 3; i++)
	{
		memcpy(data[i], positions[i], 16);
		memcpy(data[i] + 4, colors[i], 16);
	}
	g->vertices = gx_buffer(data, sizeof(data), GX2_VERTEX_BUFFER_ALIGNMENT);
	return g->vertices != NULL;
}

static void test_generated(gx_session *s, generated_case *g, const char *name)
{
	static const gx_stream streams[2] =
	{
		{ "v0_in", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, GX_MASK_XYZW, GX2_ENDIAN_SWAP_DEFAULT },
		{ "v9_in", 0, 16, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, GX_MASK_XYZW, GX2_ENDIAN_SWAP_DEFAULT },
	};
	report *r = s->report;
	char error[200];
	unsigned ix, iy, ox, oy, x, y;
	uint8_t inside[4], outside[4], color[4];
	static uint8_t shot[SCENE_SIZE * SCENE_SIZE * 4];

	g->label = name;
	if (!make_vertices(g) || !gx_fetch_create(&g->fetch, g->vs, streams, 2, error, sizeof(error)))
	{
		report_check(r, 0, "generated draw", "%s: %s", name, g->vertices ? error : "no memory");
		return;
	}
	if ((int)g->mode != (int)GX2_SHADER_MODE_UNIFORM_REGISTER)
		report_note(r, "%s: uniform mode %d, not 0: the draw below sets uniform registers all the same", name, (int)g->mode);
	/* the programs are in memory the CPU wrote (the compiler, or the loader): the GPU must not run what an old cache line says */
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, g->vs->program, g->vs->size);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, g->ps->program, g->ps->size);
	crash_set_stage(name);
	s->flush();
	scene_begin(s);
	draw_generated(g);
	scene_end(s);
	g->drawn = 1;
	scene_generated_sample(&ix, &iy, &ox, &oy);
	session_pixel(s, ix, iy, inside);
	session_pixel(s, ox, oy, outside);
	scene_generated_color(color);
	g->right = readback_distance(inside, color) <= 6 && readback_distance(outside, scene_clear_rgba) <= 3;
	report_check(r, g->right, "generated draw", "%s: inside the triangle %s (expected %s), outside %s (expected %s)", name, pixel_text(inside),
		pixel_text(color), pixel_text(outside), pixel_text(scene_clear_rgba));
	for (y = 0; y < SCENE_SIZE; y++)
		for (x = 0; x < SCENE_SIZE; x++)
			session_pixel(s, x, y, shot + ((size_t)y * SCENE_SIZE + x) * 4);
	if (gx_texture_create(&g->shot, SCENE_SIZE, SCENE_SIZE, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, 4, 0, error, sizeof(error)))
		gx_texture_upload(&g->shot, shot, (size_t)SCENE_SIZE * 4);
}

static const char *same_text(const void *a, const void *b, uint32_t size_a, uint32_t size_b, char *buffer, size_t buffer_size)
{
	if (size_a != size_b)
	{
		snprintf(buffer, buffer_size, "sizes differ (%u, %u)", (unsigned)size_a, (unsigned)size_b);
		return buffer;
	}
	if (!memcmp(a, b, size_a))
		return "byte for byte the same";
	{
		const uint8_t *p = a, *q = b;
		uint32_t i = 0;

		while (i < size_a && p[i] == q[i])
			i++;
		snprintf(buffer, buffer_size, "%u bytes, first difference at %u", (unsigned)size_a, (unsigned)i);
	}
	return buffer;
}

void stage_glsl(gx_session *s)
{
	report *r = s->report;
	char error[200], text[200];
	unsigned i, compiled = 0, done = 0;
	uint32_t slowest = 0, total = 0;
	page *p;
	const pair_result *draw;

	report_section(r, "glsl");
	if (!s->ready)
	{
		report_note(r, "skipped: the system stage did not finish");
		return;
	}
	{
		OSThread *self = OSGetCurrentThread();

		report_value(r, "main thread stack", "%p to %p (%u KiB)", self->stackEnd, self->stackStart, (unsigned)(((uintptr_t)self->stackStart - (uintptr_t)self->stackEnd) >> 10));
	}
	report_note(r, "the compiler runs 22 MB of Mesa on a thread with a %u KiB stack; the report is written before each compile", STACK_BYTES >> 10);
	job_session = s;
	s->flush();
	if (!run_big(compile_job, error, sizeof(error)))
	{
		report_check(r, 0, "compiler thread", "%s", error);
		return;
	}
	for (i = 0; i < PAIR_COUNT; i++)
	{
		done += results[i].done;
		compiled += results[i].compiled;
		if (results[i].done)
		{
			total += results[i].milliseconds;
			if (results[i].milliseconds > slowest)
				slowest = results[i].milliseconds;
		}
	}
	answers_set(s->answers, "cafeglsl.library", "%s runs on the console (InitGLSLCompiler returned)", version_text ? version_text : "?");
	answers_set(s->answers, "cafeglsl.compiles", "%u of %u shader pairs compiled (%u tried); the generator's ES dialect (#version 300 es) %s", compiled, PAIR_COUNT, done,
		results[1].done ? (results[1].compiled ? "is accepted" : "is REFUSED") : "was not tried");
	snprintf(text, sizeof(text), "%u ms in all, slowest %u ms:", (unsigned)total, (unsigned)slowest);
	for (i = 0; i < PAIR_COUNT; i++)
		if (results[i].done)
			snprintf(text + strlen(text), sizeof(text) - strlen(text), " %s %u,", pairs[i].brief, (unsigned)results[i].milliseconds);
	answers_set(s->answers, "cafeglsl.compile_ms", "%s", text);

	/* the draw: runtime-compiled, then the one the Mac's compiler made */
	draw = &results[DRAW_PAIR];
	if (draw->compiled)
	{
		char vs_buffer[96], ps_buffer[96];
		const char *vs_same, *ps_same;

		gen_cases[0].vs = draw->vs;
		gen_cases[0].ps = draw->ps;
		gen_cases[0].mode = draw->mode;
		test_generated(s, &gen_cases[0], "runtime-compiled vs00 + diffuse");
		vs_same = same_text(draw->vs->program, s->generated.group.vertexShader->program, draw->vs->size, s->generated.group.vertexShader->size, vs_buffer,
			sizeof(vs_buffer));
		ps_same = same_text(draw->ps->program, s->generated.group.pixelShader->program, draw->ps->size, s->generated.group.pixelShader->size, ps_buffer,
			sizeof(ps_buffer));
		report_value(r, "program bytes vs", "%s", vs_same);
		report_value(r, "program bytes ps", "%s", ps_same);
		answers_set(s->answers, "cafeglsl.draw_runtime", "%s", gen_cases[0].drawn ? (gen_cases[0].right ? "the generator's vs00 + diffuse pixel shader, compiled on the console, draws the triangle right" :
			"the runtime-compiled generated pair drew WRONG (see the report)") : "the runtime-compiled pair could not be drawn");
		answers_set(s->answers, "cafeglsl.same_as_mac", "vertex program %s; pixel program %s", vs_same, ps_same);
	}
	else
	{
		answers_set(s->answers, "cafeglsl.draw_runtime", "no draw: the pair did not compile");
	}
	gen_cases[1].vs = s->generated.group.vertexShader;
	gen_cases[1].ps = s->generated.group.pixelShader;
	gen_cases[1].mode = GX2_SHADER_MODE_UNIFORM_REGISTER;
	test_generated(s, &gen_cases[1], "vs00 + diffuse compiled on the Mac (GSH)");
	answers_set(s->answers, "cafeglsl.draw_offline", "%s", gen_cases[1].drawn ? (gen_cases[1].right ? "the same pair compiled on the Mac and loaded from a GSH file draws right" :
		"the offline-compiled pair drew WRONG (see the report)") : "could not be drawn");

	/* give everything back, and see what the library leaves */
	for (i = 0; i < PAIR_COUNT; i++)
	{
		if (results[i].compiled)
			FreeShaders(results[i].vs, results[i].ps);
		results[i].vs = NULL;
		results[i].ps = NULL;
	}
	if (initialized)
	{
		s->flush();
		if (run_big(destroy_job, error, sizeof(error)))
		{
			report_value(r, "MEM2 free after destroy", "%u bytes (before init: %u; the library's footprint left behind: %d)", heap_after_destroy, heap_before,
				(int)heap_before - (int)heap_after_destroy);
			answers_set(s->answers, "cafeglsl.memory", "MEM2 free: %u before init, %u after init, %u after destroy", heap_before, heap_after_init, heap_after_destroy);
		}
	}
	crash_set_stage("glsl: done");

	p = session_new_page(s, "CafeGLSL on the console");
	if (!p)
		return;
	page_text(p, 1, 2, STYLE_NORMAL, "version %s: %u of %u pairs compiled; %u ms in all", version_text ? version_text : "?", compiled, PAIR_COUNT, (unsigned)total);
	for (i = 0; i < PAIR_COUNT; i++)
	{
		page_text(p, 1, 4 + i, results[i].compiled ? STYLE_GOOD : results[i].done ? STYLE_BAD : STYLE_DIM, "%s %s", results[i].compiled ? "ok  " : results[i].done ? "FAIL" : "----",
			pairs[i].label);
		if (results[i].done)
			page_text(p, 64, 4 + i, STYLE_NORMAL, "%u ms", (unsigned)results[i].milliseconds);
	}
	page_wrap(p, 1, 13, 100, STYLE_NORMAL, "A triangle drawn with the generator's vs00 and a diffuse pass-through pixel shader. Expected: a triangle of color 64,128,191 filling the top left half of the square (clear color 51,102,153).");
	page_tile_add(p, 4, 17, 20, 14, draw_shot, 0, gen_cases[0].drawn ? (gen_cases[0].right ? STYLE_GOOD : STYLE_BAD) : STYLE_DIM);
	page_text(p, 4, 32, STYLE_NORMAL, "compiled on the console");
	page_text(p, 4, 33, gen_cases[0].right ? STYLE_GOOD : STYLE_DIM, "%s", gen_cases[0].drawn ? (gen_cases[0].right ? "draws right" : "WRONG") : "not drawn");
	page_tile_add(p, 40, 17, 20, 14, draw_shot, 1, gen_cases[1].drawn ? (gen_cases[1].right ? STYLE_GOOD : STYLE_BAD) : STYLE_DIM);
	page_text(p, 40, 32, STYLE_NORMAL, "compiled on the Mac (GSH)");
	page_text(p, 40, 33, gen_cases[1].right ? STYLE_GOOD : STYLE_DIM, "%s", gen_cases[1].drawn ? (gen_cases[1].right ? "draws right" : "WRONG") : "not drawn");
}
