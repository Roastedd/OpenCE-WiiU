/* The Broadway kernels' real machine code (the objects tools/wiiu_build.py assembles from port/wiiu/src/bw, as C data by
tools/bw_object_image.py) run by the instruction simulator (gekko_sim.h) through the same cases as the scalar models (wiiu_bw_testcases.h),
against the same C (wiiu_bw_reference.h): every output bit equal. Needs a 32-bit big-endian host, which is where the data are in the
byte order the code reads: qemu-ppc. port/wiiu/tests/bw_asm_sim_tests.sh builds and runs it.

  cc ... -DHALO_BW_KERNELS -iquote port/linux/include -iquote port/wiiu/src -iquote port/wiiu/tests -I<folder of bw_image.h> test_bw_asm_sim.c */

#include "halo_bw_kernels.h"
#include "wiiu_bw_testcases.h"
#include "gekko_sim.h"
#include "bw_image.h"

#include <stdlib.h>

unsigned halo_bw_flags;
unsigned long halo_bw_calls[HALO_BW_K_COUNT], halo_bw_used[HALO_BW_K_COUNT], halo_bw_walked[HALO_BW_K_COUNT];

typedef char pointers_are_32_bits[sizeof(void *) == 4 ? 1 : -1];
typedef char the_walk_is_32_bytes[sizeof(struct halo_bw_bsp_walk) == 32 ? 1 : -1];

static struct gekko_image images[sizeof(bw_objects) / sizeof(bw_objects[0])];
static unsigned long instructions_run;
static unsigned gqr5_value = 0x00070007u;

static uint32_t sim_call(const char *function, uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, float f1, float f2)
{
	unsigned object, entry;
	struct gekko g;

	for (object = 0; object < sizeof(bw_objects) / sizeof(bw_objects[0]); object++)
		for (entry = 0; entry < bw_objects[object].function_count; entry++)
			if (!strcmp(bw_objects[object].functions[entry].name, function))
			{
				gekko_start(&g, 0, gqr5_value);
				g.r[3] = r3; g.r[4] = r4; g.r[5] = r5; g.r[6] = r6; g.r[7] = r7;
				g.ps0[1] = f1; g.ps0[2] = f2;
				instructions_run += gekko_run(&g, images[object].code, images[object].words, bw_objects[object].functions[entry].entry);
				gekko_check_contract(&g, function);
				return g.r[3];
			}
	printf("no function %s\n", function);
	exit(2);
}

#define POINTER(p) ((uint32_t)(uintptr_t)(p))

static void sim_matrix(const float *a, const float *b, float *out)
{
	sim_call("halo_bw_asm_matrix4x3_multiply", POINTER(a), POINTER(b), POINTER(out), 0, 0, 0, 0);
}

static int sim_sphere(const float *point, const float *vector, const float *center, float radius)
{
	return (int)sim_call("halo_bw_asm_ray_sphere", POINTER(point), POINTER(vector), POINTER(center), 0, 0, radius, 0);
}

static int sim_ray_walk(struct halo_bw_bsp_walk *walk, int node, float t0, float t1)
{
	return (int)sim_call("halo_bw_asm_bsp_ray_walk", POINTER(walk), (uint32_t)node, 0, 0, 0, t0, t1);
}

static int sim_sphere_walk(struct halo_bw_bsp_walk *walk, int node, float radius)
{
	return (int)sim_call("halo_bw_asm_bsp_sphere_walk", POINTER(walk), (uint32_t)node, 0, 0, 0, radius, 0);
}

static int *sim_find(const void *references, unsigned count, unsigned zero_invalid, int *first, int datum)
{
	return (int *)(uintptr_t)sim_call("halo_bw_asm_reference_find", POINTER(references), count, zero_invalid, POINTER(first), (uint32_t)datum, 0, 0);
}

static int sim_qload(const void *compressed, float *quaternion)
{
	return (int)sim_call("halo_bw_asm_quaternion_load", POINTER(compressed), POINTER(quaternion), 0, 0, 0, 0, 0);
}

static int report(const char *name, const struct bw_stats *stats)
{
	printf("%-16s %8lu cases (%lu with two NaN payloads), %lu different%s%s\n", name, stats->cases, stats->lenient, stats->failures,
		stats->failures ? ": " : "", stats->failures ? stats->first : "");
	return stats->failures != 0;
}

int main(int argc, char **argv)
{
	const unsigned long scale = argc > 1 ? strtoul(argv[1], 0, 10) : 1;
	struct bw_stats stats;
	int failed = 0, cls, unavailable;
	unsigned object;

	for (object = 0; object < sizeof(bw_objects) / sizeof(bw_objects[0]); object++)
		gekko_load(&images[object], &bw_objects[object]);
	memset(&stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_matrix(sim_matrix, cls, 20000ul * scale, 1u + (unsigned)cls, &stats);
	failed |= report("matrix", &stats);
	memset(&stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_sphere(sim_sphere, cls, 50000ul * scale, 11u + (unsigned)cls, &stats);
	failed |= report("ray-sphere", &stats);
	memset(&stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_ray_walk(sim_ray_walk, cls, 10000ul * scale, 21u + (unsigned)cls, &stats);
	failed |= report("bsp ray walk", &stats);
	memset(&stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_sphere_walk(sim_sphere_walk, cls, 10000ul * scale, 31u + (unsigned)cls, &stats);
	failed |= report("bsp sphere walk", &stats);
	memset(&stats, 0, sizeof(stats));
	bw_test_find(sim_find, 50000ul * scale, 41u, &stats);
	failed |= report("reference find", &stats);
	memset(&stats, 0, sizeof(stats));
	bw_test_qload(sim_qload, 20000ul * scale, 51u, &stats, &unavailable);
	failed |= report("quaternion load", &stats);
	if (unavailable)
	{
		printf("quaternion load answered 0 with GQR5 set\n");
		failed = 1;
	}
	/* a thread whose GQR5 is not the signed 16-bit load: the kernel answers 0 and writes nothing */
	{
		struct compressed_quaternion_8byte compressed = { 1, 2, 3, 4 };
		float out[4] = { 1.5f, 2.5f, 3.5f, 4.5f };

		gqr5_value = 0;
		if (sim_qload(&compressed, out) != 0 || out[0] != 1.5f || out[1] != 2.5f || out[2] != 3.5f || out[3] != 4.5f)
		{
			printf("quaternion load with GQR5 = 0 did not decline cleanly\n");
			failed = 1;
		}
		else
			printf("quaternion load with GQR5 = 0: declines, nothing written\n");
		/* and after the set-up writes GQR5 (the simulator keeps the register for the next call: one thread's, for the call after) */
		{
			struct gekko g;
			unsigned object, entry;

			for (object = 0; object < sizeof(bw_objects) / sizeof(bw_objects[0]); object++)
				for (entry = 0; entry < bw_objects[object].function_count; entry++)
					if (!strcmp(bw_objects[object].functions[entry].name, "halo_bw_asm_set_gqr5"))
					{
						gekko_start(&g, 0, 0);
						g.r[3] = 0x00070007u;
						gekko_run(&g, images[object].code, images[object].words, bw_objects[object].functions[entry].entry);
						gekko_check_contract(&g, "halo_bw_asm_set_gqr5");
						if (g.gqr[5] != 0x00070007u || g.gqr[0] != 0)
						{
							printf("halo_bw_asm_set_gqr5 did not set GQR5 alone\n");
							failed = 1;
						}
						else
							printf("halo_bw_asm_set_gqr5: sets GQR5 to 00070007 and nothing else\n");
					}
		}
		gqr5_value = 0x00070007u;
	}
	/* the start-up's leaves: the pair add, and the eight quantization registers read as the user aliases give them */
	{
		float in[2] = { 1.5f, -2.25f }, out[2] = { 0.0f, 0.0f };
		unsigned gqr[8];
		unsigned index;
		int leaves_ok = 1;

		sim_call("halo_bw_asm_probe_ps", POINTER(in), POINTER(out), 0, 0, 0, 0, 0);
		memset(gqr, 0xff, sizeof(gqr));
		sim_call("halo_bw_asm_read_gqr", POINTER(gqr), 0, 0, 0, 0, 0, 0);
		if (out[0] != 3.0f || out[1] != -4.5f || gqr[0] != 0 || gqr[5] != gqr5_value)
		{
			printf("the start-up's leaves gave %f %f and GQR0 %08x GQR5 %08x\n", (double)out[0], (double)out[1], gqr[0], gqr[5]);
			leaves_ok = 0;
		}
		for (index = 1; index < 8; index++)
			if (index != 5 && gqr[index] != 0)
				leaves_ok = 0;
		if (leaves_ok)
			printf("halo_bw_asm_probe_ps and halo_bw_asm_read_gqr: as they should be\n");
		else
			failed = 1;
	}
	printf("%lu instructions simulated\n", instructions_run);
	printf(failed ? "bw asm: DIFFERENT\n" : "bw asm: every case equals the C\n");
	return failed;
}
