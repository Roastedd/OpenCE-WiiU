/*
WIIU_BW_KERNELS.C

The Broadway kernels' switch (port/linux/include/halo_bw_kernels.h, port/wiiu/src/bw): the card file that turns them on, the boot-time
self-test that decides which, the timing of each against the C it replaces, and the per-tick count of what the game asked of them.

halo/broadway.txt on the card (read once, as the game's thread starts; missing is 0):
  0  off: nothing here runs, no paired-single instruction is executed, the game is what it was (the default)
  1  on: the self-test runs each kernel against the C it replaces on a few hundred thousand random and edge inputs, and a kernel that
     gives every bit the C gives is switched on; the others stay C
  2  the self-test and the timing only: the same log, every kernel stays off
  3  as 1, and if this thread's GQR5 is not the signed 16-bit load the quaternion kernel needs, it is set to it first (mtspr 901, the
     user alias of GQR5; whether a user program may write it is not known, and if the CPU refuses, the run ends there: use 1)

The self-test runs on the thread that calls this (the game's thread, which runs the ticks), after wiiu_fpu_setup, so with the FPSCR the
game computes under; the log says the core, the FPSCR, the eight quantization registers (GQR0 must be the float load and store the kernels'
psq_l and psq_st assume, and GQR5 the signed 16-bit load quaternion_decompress_8byte's kernel needs; both are per thread) and, for each
kernel and each class of input (normal, ties, wide, denormal, inf, nan: wiiu_bw_testcases.h), whether every bit matched. A class that fails
leaves its kernel off, and the first failure is said with the words that differed. After the self-test the log has the time of a call, C
against kernel, which is what a console run needs to know what the kernel gains.

While the game runs, every 150 ticks (wiiu_tick_parts.c calls wiiu_bw_tick_report) the log says how many calls of each replaced function a
tick made, how many the kernel answered, and for the BSP walks how many nodes they went through; with the tick parts line ("objects" is the
part the kernels' functions are mostly in) that is what a run with 0 in the file is compared with a run with 1.
*/

#define HALO_BW_KERNELS 1

#include "wiiu_shell.h"

#include "halo_bw_kernels.h"
#include "wiiu_bw_testcases.h"

#include <coreinit/context.h>
#include <coreinit/core.h>
#include <coreinit/exception.h>
#include <coreinit/time.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* the game's own functions (source/), called with the kernels still off to see that they are what the reference is */
void matrix4x3_multiply(const void *a, const void *b, void *result);
unsigned char fast_vector_intersects_sphere(const void *point, const void *vector, const void *center, float radius);
void quaternion_decompress_8byte(const void *compressed, void *quaternion);

static const char *const kernel_names[HALO_BW_K_COUNT] = { "matrix", "ray-sphere", "bsp-ray", "bsp-sphere", "reference", "quaternion" };

static int configured;
static unsigned self_test_core = 99;
static unsigned long reported_calls[HALO_BW_K_COUNT], reported_used[HALO_BW_K_COUNT], reported_walked[HALO_BW_K_COUNT];

/* the kernels as the test cases take them (the assembly is the same functions; a few are given a wrapper for the shape the cases pass) */
static int test_ray_sphere(const float *point, const float *vector, const float *center, float radius)
{
	return halo_bw_asm_ray_sphere(point, vector, center, radius);
}

static void test_game_matrix(const float *a, const float *b, float *out)
{
	matrix4x3_multiply(a, b, out);
}

static int test_game_sphere(const float *point, const float *vector, const float *center, float radius)
{
	return fast_vector_intersects_sphere(point, vector, center, radius);
}

static int test_game_qload(const void *compressed, float *quaternion)
{
	quaternion_decompress_8byte(compressed, quaternion);
	return 1;
}

/* ---------- the exception guard
The start-up runs the kernels once under a guard: a program, alignment, data-access or floating-point exception whose address is in the code of a
kernel (the kernels are leaves, with no stack frame, and use only volatile registers) is not the run's end but a failure of that kernel: the handler
returns to the kernel's caller, the caller finds the fault counted, and the log says the kind and the address. What a core does with a paired-single
instruction it was not set up for, or with a misaligned quantized load, is then in the log, where it would have been a crash. Exceptions
elsewhere go to the handlers that were there. The handlers are taken out before the start-up returns: the game runs with its own. */
extern const char halo_bw_range_matrix_begin[], halo_bw_range_matrix_end[], halo_bw_range_sphere_begin[], halo_bw_range_sphere_end[],
	halo_bw_range_bsp_begin[], halo_bw_range_bsp_end[], halo_bw_range_reference_begin[], halo_bw_range_reference_end[],
	halo_bw_range_quaternion_begin[], halo_bw_range_quaternion_end[], halo_bw_range_probe_begin[], halo_bw_range_probe_end[];

static const struct { const char *begin, *end; } kernel_ranges[] = {
	{ halo_bw_range_matrix_begin, halo_bw_range_matrix_end },
	{ halo_bw_range_sphere_begin, halo_bw_range_sphere_end },
	{ halo_bw_range_bsp_begin, halo_bw_range_bsp_end },
	{ halo_bw_range_reference_begin, halo_bw_range_reference_end },
	{ halo_bw_range_quaternion_begin, halo_bw_range_quaternion_end },
	{ halo_bw_range_probe_begin, halo_bw_range_probe_end },
};

static const OSExceptionType guard_types[4] = { OS_EXCEPTION_TYPE_PROGRAM, OS_EXCEPTION_TYPE_ALIGNMENT, OS_EXCEPTION_TYPE_DSI,
	OS_EXCEPTION_TYPE_FLOATING_POINT };
static const char *const guard_type_names[4] = { "program (an instruction this core does not allow)", "alignment", "data access", "floating point" };
static OSExceptionCallbackFn guard_previous[4];
static volatile unsigned guard_active, guard_faults, guard_slot, guard_pc, guard_address;

static BOOL guard_recover(unsigned slot, OSContext *context)
{
	const uintptr_t pc = context->srr0;
	unsigned range;

	if (guard_active)
		for (range = 0; range < sizeof(kernel_ranges) / sizeof(kernel_ranges[0]); range++)
			if (pc >= (uintptr_t)kernel_ranges[range].begin && pc < (uintptr_t)kernel_ranges[range].end)
			{
				if (!guard_faults++)
				{
					guard_slot = slot;
					guard_pc = (unsigned)pc;
					guard_address = (unsigned)context->dar;
				}
				context->srr0 = context->lr;	/* (back to the caller: the kernels are leaves) */
				return TRUE;
			}
	return guard_previous[slot] ? guard_previous[slot](context) : FALSE;
}

static BOOL guard_program(OSContext *context) { return guard_recover(0, context); }
static BOOL guard_alignment(OSContext *context) { return guard_recover(1, context); }
static BOOL guard_dsi(OSContext *context) { return guard_recover(2, context); }
static BOOL guard_float(OSContext *context) { return guard_recover(3, context); }

static void guard_begin(void)
{
	OSExceptionCallbackFn handlers[4] = { guard_program, guard_alignment, guard_dsi, guard_float };
	unsigned slot;

	guard_faults = 0;
	guard_active = 1;
	for (slot = 0; slot < 4; slot++)
		guard_previous[slot] = OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, guard_types[slot], handlers[slot]);
}

static void guard_end(void)
{
	unsigned slot;

	guard_active = 0;
	for (slot = 0; slot < 4; slot++)
		OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, guard_types[slot], guard_previous[slot]);
}

/* the faults counted since the last call: said, and cleared; returns how many */
static unsigned guard_take(const char *what)
{
	const unsigned faults = guard_faults;

	if (faults)
	{
		wiiu_log("Broadway kernels: %s: %u exceptions caught in kernel code, the first %s at %08x (address %08x)", what, faults,
			guard_type_names[guard_slot & 3], guard_pc, guard_address);
		guard_faults = 0;
	}
	return faults;
}

static unsigned read_fpscr(void)
{
	union { double real; uint64_t bits; } value;

	__asm__ volatile("mffs %0" : "=f"(value.real));
	return (unsigned)value.bits;
}

/* ---------- the test of a kernel, a class at a time */

enum
{
	CASES_MATRIX = 30000,	/* a class's cases (the matrix and the in-place forms are four runs of the kernel each) */
	CASES_SPHERE = 100000,
	CASES_WALK = 40000,
	CASES_FIND = 250000,
	CASES_QLOAD = 150000	/* and all 4 x 65536 values of a sample */
};

/* the classes' results of a kernel in one line, and the first failure on a line of its own; returns whether every class passed */
static int say_classes(const char *name, const struct bw_stats stats[BW_CLASSES])
{
	char line[200];
	size_t used = 0;
	unsigned long cases = 0;
	int cls, passed = 1;

	for (cls = 0; cls < BW_CLASSES; cls++)
	{
		if (!stats[cls].cases)
			continue;
		cases += stats[cls].cases;
		used += (size_t)snprintf(line + used, sizeof(line) - used, "%s%s %s", used ? ", " : "", bw_class_names[cls],
			stats[cls].failures ? "FAILED" : "ok");
		if (stats[cls].failures)
			passed = 0;
	}
	wiiu_log("Broadway kernel %s: %s (%lu cases: %s)", name, passed ? "every bit equals the C" : "DIFFERENT, left off", cases, line);
	for (cls = 0; cls < BW_CLASSES; cls++)
		if (stats[cls].failures)
			wiiu_log("Broadway kernel %s: %s: %lu of %lu cases differ; the first: %s", name, bw_class_names[cls], stats[cls].failures,
				stats[cls].cases, stats[cls].first);
	return passed;
}

/* the game's own C functions against the reference, with the kernels off: what the reference stands for is what the game runs */
static int game_equals_reference(void)
{
	struct bw_stats matrix[BW_CLASSES], sphere[BW_CLASSES], qload_stats;
	int cls, passed = 1, unavailable;

	memset(matrix, 0, sizeof(matrix));
	memset(sphere, 0, sizeof(sphere));
	memset(&qload_stats, 0, sizeof(qload_stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
	{
		/* (the reference against itself is not the question: the game's function against the reference is) */
		struct bw_stats *m = &matrix[cls], *s = &sphere[cls];

		bw_test_matrix(test_game_matrix, cls, 5000, 91u + (unsigned)cls, m);
		bw_test_sphere(test_game_sphere, cls, 20000, 97u + (unsigned)cls, s);
		passed = passed && !m->failures && !s->failures;
	}
	bw_test_qload(test_game_qload, 20000, 99u, &qload_stats, &unavailable);
	passed = passed && !qload_stats.failures;
	wiiu_log("Broadway reference: the game's own matrix4x3_multiply, fast_vector_intersects_sphere and quaternion_decompress_8byte %s the reference",
		passed ? "equal" : "DIFFER FROM");
	if (!passed)
	{
		for (cls = 0; cls < BW_CLASSES; cls++)
		{
			if (matrix[cls].failures)
				wiiu_log("  matrix: %s", matrix[cls].first);
			if (sphere[cls].failures)
				wiiu_log("  sphere: %s", sphere[cls].first);
		}
		if (qload_stats.failures)
			wiiu_log("  quaternion: %s", qload_stats.first);
	}
	return passed;
}

/* ---------- timing: C against kernel */

static volatile float sink_float;
static volatile int sink_int;

enum { BENCH_CALLS = 20000, BENCH_VARIANTS = 64 };

static double microseconds(OSTime ticks)
{
	return (double)OSTicksToNanoseconds(ticks) / 1000.0;
}

/* nanoseconds a call, C and kernel, over a fixed set of ordinary inputs (the games' own sizes), the loop and its sink the same for both */
static void bench(unsigned kernels_ready)
{
	static float a[BENCH_VARIANTS][13] __attribute__((aligned(16))), b[BENCH_VARIANTS][13] __attribute__((aligned(16)));
	static float out[13] __attribute__((aligned(16)));
	static float point[BENCH_VARIANTS][3] __attribute__((aligned(16))), vector[BENCH_VARIANTS][3] __attribute__((aligned(16)));
	static float center[BENCH_VARIANTS][3] __attribute__((aligned(16)));
	struct bw_rng rng = { 0x1234567u };
	OSTime started, c_time, k_time;
	int variant, call;
	int total = 0;

	for (variant = 0; variant < BENCH_VARIANTS; variant++)
	{
		int k;

		for (k = 0; k < 13; k++)
		{
			a[variant][k] = bw_float(&rng, BW_NORMAL, 0);
			b[variant][k] = bw_float(&rng, BW_NORMAL, 0);
		}
		for (k = 0; k < 3; k++)
		{
			point[variant][k] = bw_float(&rng, BW_NORMAL, 0);
			vector[variant][k] = bw_float(&rng, BW_NORMAL, 0);
			center[variant][k] = point[variant][k] + vector[variant][k] * 0.5f;
		}
	}
	if (kernels_ready & HALO_BW_BIT(HALO_BW_K_MATRIX))
	{
		/* (the game's function as the game calls it, with the kernel off and on, and the kernel alone) */
		OSTime w_off, w_on, direct;

		halo_bw_flags = 0;
		started = OSGetSystemTime();
		for (call = 0; call < BENCH_CALLS; call++)
			matrix4x3_multiply(a[call % BENCH_VARIANTS], b[(call * 7) % BENCH_VARIANTS], out);
		w_off = OSGetSystemTime() - started;
		halo_bw_flags = HALO_BW_BIT(HALO_BW_K_MATRIX);
		started = OSGetSystemTime();
		for (call = 0; call < BENCH_CALLS; call++)
			matrix4x3_multiply(a[call % BENCH_VARIANTS], b[(call * 7) % BENCH_VARIANTS], out);
		w_on = OSGetSystemTime() - started;
		halo_bw_flags = 0;
		started = OSGetSystemTime();
		for (call = 0; call < BENCH_CALLS; call++)
			halo_bw_asm_matrix4x3_multiply(a[call % BENCH_VARIANTS], b[(call * 7) % BENCH_VARIANTS], out);
		direct = OSGetSystemTime() - started;
		sink_float = out[12];
		wiiu_log("Broadway timing matrix4x3_multiply: the game's call %.0f ns with the C, %.0f ns with the kernel (the kernel alone %.0f ns)",
			microseconds(w_off) * 1000.0 / BENCH_CALLS, microseconds(w_on) * 1000.0 / BENCH_CALLS, microseconds(direct) * 1000.0 / BENCH_CALLS);
	}
	if (kernels_ready & HALO_BW_BIT(HALO_BW_K_SPHERE))
	{
		OSTime w_off, w_on, direct;

		halo_bw_flags = 0;
		started = OSGetSystemTime();
		for (call = 0; call < BENCH_CALLS; call++)
			total += fast_vector_intersects_sphere(point[call % BENCH_VARIANTS], vector[call % BENCH_VARIANTS], center[(call * 5) % BENCH_VARIANTS], 0.5f);
		w_off = OSGetSystemTime() - started;
		halo_bw_flags = HALO_BW_BIT(HALO_BW_K_SPHERE);
		started = OSGetSystemTime();
		for (call = 0; call < BENCH_CALLS; call++)
			total += fast_vector_intersects_sphere(point[call % BENCH_VARIANTS], vector[call % BENCH_VARIANTS], center[(call * 5) % BENCH_VARIANTS], 0.5f);
		w_on = OSGetSystemTime() - started;
		halo_bw_flags = 0;
		started = OSGetSystemTime();
		for (call = 0; call < BENCH_CALLS; call++)
			total += halo_bw_asm_ray_sphere(point[call % BENCH_VARIANTS], vector[call % BENCH_VARIANTS], center[(call * 5) % BENCH_VARIANTS], 0.5f);
		direct = OSGetSystemTime() - started;
		sink_int = total;
		wiiu_log("Broadway timing fast_vector_intersects_sphere: the game's call %.0f ns with the C, %.0f ns with the kernel (the kernel alone %.0f ns)",
			microseconds(w_off) * 1000.0 / BENCH_CALLS, microseconds(w_on) * 1000.0 / BENCH_CALLS, microseconds(direct) * 1000.0 / BENCH_CALLS);
	}
	if (kernels_ready & (HALO_BW_BIT(HALO_BW_K_BSP_RAY) | HALO_BW_BIT(HALO_BW_K_BSP_SPHERE)))
	{
		/* a chain of nodes the ray and the sphere pass to one side, as long as asked: the C is the reference walk (the game's loop is that
		loop with the recursion and the block lookups around it), the kernel the assembly */
		static int nodes[34 * 3] __attribute__((aligned(16)));
		static float planes[16] __attribute__((aligned(16)));
		static const int lengths[3] = { 1, 4, 16 };
		static float walk_point[3] __attribute__((aligned(16))) = { 0.25f, 0.5f, 0.75f }, walk_vector[3] __attribute__((aligned(16))) = { 0.0f, 0.0f, -1.0f };
		int length_index, node;

		/* plane k: normal +x, d = -1000 + k: every node is well in front of the point; the children lead to the next node, the end is a leaf */
		for (node = 0; node < 4; node++)
		{
			planes[4 * node] = 1.0f;
			planes[4 * node + 1] = planes[4 * node + 2] = 0.0f;
			planes[4 * node + 3] = -1000.0f + (float)node;
		}
		for (length_index = 0; length_index < 3; length_index++)
		{
			const int length = lengths[length_index];
			struct halo_bw_bsp_walk reference_walk, kernel_walk;
			unsigned long sum = 0;

			for (node = 0; node < 34; node++)
			{
				nodes[3 * node] = node & 3;
				nodes[3 * node + 1] = nodes[3 * node + 2] = node + 1 < length ? node + 1 : (int)(0x80000000u | 3u);
			}
			reference_walk.nodes = nodes;
			reference_walk.planes = planes;
			reference_walk.point = walk_point;
			reference_walk.vector = walk_vector;
			reference_walk.node_count = 34;
			reference_walk.plane_count = 4;
			if (kernels_ready & HALO_BW_BIT(HALO_BW_K_BSP_RAY))
			{
				started = OSGetSystemTime();
				for (call = 0; call < BENCH_CALLS; call++)
				{
					reference_walk.nodes_left = 100000;
					reference_walk.depth = 0;
					sum += (unsigned long)halo_bw_ref_bsp_ray_walk(&reference_walk, 0, 0.0f, 1.0f);
				}
				c_time = OSGetSystemTime() - started;
				kernel_walk = reference_walk;
				started = OSGetSystemTime();
				for (call = 0; call < BENCH_CALLS; call++)
				{
					kernel_walk.nodes_left = 100000;
					kernel_walk.depth = 0;
					sum += (unsigned long)halo_bw_asm_bsp_ray_walk(&kernel_walk, 0, 0.0f, 1.0f);
				}
				k_time = OSGetSystemTime() - started;
				wiiu_log("Broadway timing bsp ray walk of %d nodes: C %.0f ns a walk, kernel %.0f ns", length,
					microseconds(c_time) * 1000.0 / BENCH_CALLS, microseconds(k_time) * 1000.0 / BENCH_CALLS);
			}
			if (kernels_ready & HALO_BW_BIT(HALO_BW_K_BSP_SPHERE))
			{
				reference_walk.vector = 0;
				started = OSGetSystemTime();
				for (call = 0; call < BENCH_CALLS; call++)
				{
					reference_walk.nodes_left = 100000;
					reference_walk.depth = 0;
					sum += (unsigned long)halo_bw_ref_bsp_sphere_walk(&reference_walk, 0, 0.5f);
				}
				c_time = OSGetSystemTime() - started;
				kernel_walk = reference_walk;
				started = OSGetSystemTime();
				for (call = 0; call < BENCH_CALLS; call++)
				{
					kernel_walk.nodes_left = 100000;
					kernel_walk.depth = 0;
					sum += (unsigned long)halo_bw_asm_bsp_sphere_walk(&kernel_walk, 0, 0.5f);
				}
				k_time = OSGetSystemTime() - started;
				wiiu_log("Broadway timing bsp sphere walk of %d nodes: C %.0f ns a walk, kernel %.0f ns", length,
					microseconds(c_time) * 1000.0 / BENCH_CALLS, microseconds(k_time) * 1000.0 / BENCH_CALLS);
				reference_walk.vector = walk_vector;
			}
			sink_int = (int)sum;
		}
	}
	if (kernels_ready & HALO_BW_BIT(HALO_BW_K_REFERENCE))
	{
		/* a list of 8 references, the one wanted the last: the search the engine makes of a cluster's list */
		static struct halo_bw_ref_reference references[16] __attribute__((aligned(16)));
		int link, first = 0, i;
		unsigned long sum = 0;

		memset(references, 0, sizeof(references));
		for (i = 0; i < 8; i++)
		{
			references[i].identifier = 3;
			references[i].datum_index = 100 + i;
			references[i].next_reference_index = i < 7 ? (int)((3u << 16) | (unsigned)(i + 1)) : -1;
		}
		first = (int)(3u << 16);
		started = OSGetSystemTime();
		for (call = 0; call < BENCH_CALLS; call++)
		{
			link = first;
			sum += (unsigned long)(size_t)halo_bw_ref_reference_find(references, 16, 1, &link, 107);
		}
		c_time = OSGetSystemTime() - started;
		started = OSGetSystemTime();
		for (call = 0; call < BENCH_CALLS; call++)
		{
			link = first;
			sum += (unsigned long)(size_t)halo_bw_asm_reference_find(references, 16, 1, &link, 107);
		}
		k_time = OSGetSystemTime() - started;
		sink_int = (int)sum;
		wiiu_log("Broadway timing reference search of 8: C %.0f ns a search, kernel %.0f ns (the C here is the reference loop without datum_get's call)",
			microseconds(c_time) * 1000.0 / BENCH_CALLS, microseconds(k_time) * 1000.0 / BENCH_CALLS);
	}
	if (kernels_ready & HALO_BW_BIT(HALO_BW_K_QUATERNION))
	{
		static short samples[BENCH_VARIANTS][4] __attribute__((aligned(8)));
		static float quaternion[4] __attribute__((aligned(16)));

		for (variant = 0; variant < BENCH_VARIANTS; variant++)
		{
			int k;

			for (k = 0; k < 4; k++)
				samples[variant][k] = (short)bw_next(&rng);
		}
		{
			OSTime w_off, w_on, direct;

			halo_bw_flags = 0;
			started = OSGetSystemTime();
			for (call = 0; call < BENCH_CALLS; call++)
				quaternion_decompress_8byte(samples[call % BENCH_VARIANTS], quaternion);
			w_off = OSGetSystemTime() - started;
			halo_bw_flags = HALO_BW_BIT(HALO_BW_K_QUATERNION);
			started = OSGetSystemTime();
			for (call = 0; call < BENCH_CALLS; call++)
				quaternion_decompress_8byte(samples[call % BENCH_VARIANTS], quaternion);
			w_on = OSGetSystemTime() - started;
			halo_bw_flags = 0;
			started = OSGetSystemTime();
			for (call = 0; call < BENCH_CALLS; call++)
				halo_bw_asm_quaternion_load(samples[call % BENCH_VARIANTS], quaternion);
			direct = OSGetSystemTime() - started;
			sink_float = quaternion[3];
			wiiu_log("Broadway timing quaternion_decompress_8byte: the game's call %.0f ns with the C, %.0f ns with the kernel (the kernel alone %.0f ns)",
				microseconds(w_off) * 1000.0 / BENCH_CALLS, microseconds(w_on) * 1000.0 / BENCH_CALLS, microseconds(direct) * 1000.0 / BENCH_CALLS);
		}
	}
}

/* ---------- the start */

void wiiu_bw_start(void);

/* one kernel's tests are over: whether it passed (its classes all matched and it did not fault), and the bit it is */
static unsigned judge(unsigned bit, int passed, const char *name)
{
	return passed && !guard_take(name) ? bit : 0;
}

void wiiu_bw_start(void)
{
	FILE *file = fopen(WIIU_DATA_DIRECTORY "/broadway.txt", "r");
	unsigned gqr[8];
	unsigned fpscr, ready = 0;
	struct bw_stats stats[BW_CLASSES];
	float probe_in[2] __attribute__((aligned(8))) = { 1.5f, -2.25f }, probe_out[2] __attribute__((aligned(8))) = { 0.0f, 0.0f };
	int cls, gqr0_float, gqr5_signed16, unavailable;

	if (file)
	{
		if (fscanf(file, "%d", &configured) != 1)
			configured = 0;
		fclose(file);
	}
	if (configured < 1 || configured > 3)
	{
		configured = 0;
		wiiu_log("Broadway kernels: off (halo/broadway.txt is missing or 0; 1 turns on the kernels that pass the self-test, 2 runs the self-test only, 3 is 1 and sets GQR5)");
		return;
	}
	self_test_core = OSGetCoreId();
	fpscr = read_fpscr();
	halo_bw_flags = 0;
	wiiu_log("Broadway kernels: halo/broadway.txt %d; self-test on core %u, FPSCR %08x (the start-up runs under an exception guard: a core that refuses an instruction is said in the log, not a crash)",
		configured, self_test_core, fpscr);
	guard_begin();

	/* the first paired-single instruction and the first read of the quantization registers' aliases */
	halo_bw_asm_probe_ps(probe_in, probe_out);
	if (guard_take("a paired-single add") || probe_out[0] != 3.0f || probe_out[1] != -4.5f)
	{
		wiiu_log("Broadway kernels: this core does not run paired-single instructions (or gave %f and %f for 1.5 + 1.5 and -2.25 + -2.25): every kernel stays off",
			(double)probe_out[0], (double)probe_out[1]);
		guard_end();
		return;
	}
	memset(gqr, 0xff, sizeof(gqr));
	halo_bw_asm_read_gqr(gqr);
	if (guard_take("the read of GQR0 to GQR7 (SPR 896 to 903)"))
	{
		wiiu_log("Broadway kernels: the quantization registers cannot be read here: every kernel stays off");
		guard_end();
		return;
	}
	gqr0_float = (gqr[0] & (0x00070007u | 0x3f003f00u)) == 0;	/* (load and store type float, scales 0) */
	gqr5_signed16 = (gqr[5] >> 16) == 7;
	wiiu_log("Broadway kernels: GQR0-7 %08x %08x %08x %08x %08x %08x %08x %08x; GQR0 is %s; GQR5 is %s", gqr[0], gqr[1], gqr[2], gqr[3], gqr[4],
		gqr[5], gqr[6], gqr[7], gqr0_float ? "the float load and store the kernels assume" : "NOT the float load and store: every kernel stays off",
		gqr5_signed16 ? "the signed 16-bit load the quaternion kernel needs" : "not the signed 16-bit load (type 7, scale 0): the quaternion kernel stays off");
	if (configured == 3 && gqr0_float && !gqr5_signed16)
	{
		wiiu_log("Broadway kernels: halo/broadway.txt 3: setting GQR5 to 00070007 (mtspr 901, a user write of a quantization register)");
		halo_bw_asm_set_gqr5(0x00070007u);
		if (guard_take("the write of GQR5 (SPR 901)"))
			wiiu_log("Broadway kernels: the CPU refused the write: GQR5 stays as it was");
		halo_bw_asm_read_gqr(gqr);
		guard_take("the second read of the quantization registers");
		gqr5_signed16 = (gqr[5] >> 16) == 7;
		wiiu_log("Broadway kernels: GQR5 is now %08x: %s", gqr[5], gqr5_signed16 ? "the signed 16-bit load" : "still not the signed 16-bit load");
	}
	if (!gqr0_float)
	{
		guard_end();
		return;
	}
	if ((fpscr & 3u) != 0 || (fpscr & 0xf8u) != 0)
	{
		wiiu_log("Broadway kernels: FPSCR %08x has a rounding mode other than nearest or an exception enabled: every kernel stays off", fpscr);
		guard_end();
		return;
	}
	if (!game_equals_reference())
	{
		guard_end();
		return;
	}

	memset(stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_matrix(halo_bw_asm_matrix4x3_multiply, cls, CASES_MATRIX, 1u + (unsigned)cls, &stats[cls]);
	ready |= judge(HALO_BW_BIT(HALO_BW_K_MATRIX), say_classes("matrix4x3_multiply", stats), "matrix4x3_multiply");

	memset(stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_sphere(test_ray_sphere, cls, CASES_SPHERE, 11u + (unsigned)cls, &stats[cls]);
	ready |= judge(HALO_BW_BIT(HALO_BW_K_SPHERE), say_classes("fast_vector_intersects_sphere", stats), "fast_vector_intersects_sphere");

	memset(stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_ray_walk(halo_bw_asm_bsp_ray_walk, cls, CASES_WALK, 21u + (unsigned)cls, &stats[cls]);
	ready |= judge(HALO_BW_BIT(HALO_BW_K_BSP_RAY), say_classes("collision bsp ray walk", stats), "collision bsp ray walk");

	memset(stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_sphere_walk(halo_bw_asm_bsp_sphere_walk, cls, CASES_WALK, 31u + (unsigned)cls, &stats[cls]);
	ready |= judge(HALO_BW_BIT(HALO_BW_K_BSP_SPHERE), say_classes("collision bsp sphere walk", stats), "collision bsp sphere walk");

	memset(stats, 0, sizeof(stats));
	bw_test_find(halo_bw_asm_reference_find, CASES_FIND, 41u, &stats[BW_NORMAL]);
	ready |= judge(HALO_BW_BIT(HALO_BW_K_REFERENCE), say_classes("reference_list_remove search", stats), "reference_list_remove search");

	memset(stats, 0, sizeof(stats));
	if (gqr5_signed16)
	{
		bw_test_qload(halo_bw_asm_quaternion_load, CASES_QLOAD, 51u, &stats[BW_NORMAL], &unavailable);
		if (unavailable)
			wiiu_log("Broadway kernel quaternion_decompress_8byte: the kernel found GQR5 not to be the signed 16-bit load: left off");
		else
			ready |= judge(HALO_BW_BIT(HALO_BW_K_QUATERNION), say_classes("quaternion_decompress_8byte", stats), "quaternion_decompress_8byte");
	}
	else
		wiiu_log("Broadway kernel quaternion_decompress_8byte: not tested (GQR5 is %08x)", gqr[5]);

	bench(ready);
	guard_take("the timing");
	guard_end();
	if (configured != 2)
		halo_bw_flags = ready;
	wiiu_log("Broadway kernels: %s", configured != 2 ? "in use" : "self-test only (broadway.txt 2): none in use");
	if (configured != 2)
	{
		char list[160];
		size_t used = 0;
		int k;

		for (k = 0; k < HALO_BW_K_COUNT; k++)
			used += (size_t)snprintf(list + used, sizeof(list) - used, "%s%s %s", k ? ", " : "", kernel_names[k], ready & HALO_BW_BIT(k) ? "on" : "off");
		wiiu_log("Broadway kernels: %s", list);
	}
}

/* ---------- per tick */

/* wiiu_tick_parts.c, every 150 ticks: what the game asked of the kernels' functions a tick, and what the kernels answered */
void wiiu_bw_tick_report(unsigned long ticks);

void wiiu_bw_tick_report(unsigned long ticks)
{
	char line[230];
	size_t used = 0;
	int k;

	if (!ticks)
		return;
	if (OSGetCoreId() != self_test_core && self_test_core != 99)
		wiiu_log("Broadway kernels: the ticks now run on core %u, the self-test ran on core %u", OSGetCoreId(), self_test_core);
	for (k = 0; k < HALO_BW_K_COUNT; k++)
	{
		const unsigned long calls = halo_bw_calls[k] - reported_calls[k], used_now = halo_bw_used[k] - reported_used[k],
			walked = halo_bw_walked[k] - reported_walked[k];

		used += (size_t)snprintf(line + used, sizeof(line) - used, "%s%s %.1f/%.1f", k ? ", " : "", kernel_names[k], (double)calls / (double)ticks,
			(double)used_now / (double)ticks);
		if (k == HALO_BW_K_BSP_RAY || k == HALO_BW_K_BSP_SPHERE)
			used += (size_t)snprintf(line + used, sizeof(line) - used, " (%.1f nodes)", (double)walked / (double)ticks);
		else if (k == HALO_BW_K_REFERENCE)
			used += (size_t)snprintf(line + used, sizeof(line) - used, " (%.1f steps of the cluster code's own list walk)", (double)walked / (double)ticks);
		reported_calls[k] = halo_bw_calls[k];
		reported_used[k] = halo_bw_used[k];
		reported_walked[k] = halo_bw_walked[k];
	}
	wiiu_log("bw calls a tick, asked/answered by a kernel (broadway.txt %d, %u in use): %s", configured, (unsigned)halo_bw_flags, line);
}
