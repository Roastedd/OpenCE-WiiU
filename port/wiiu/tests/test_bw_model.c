/* The scalar models of the Broadway kernels (port/linux/include/halo_bw_model.h) against the C they stand for
(port/wiiu/src/wiiu_bw_reference.h), on millions of random and edge inputs of every class (wiiu_bw_testcases.h): every output bit equal.
The models do what the assembly does, operation for operation (QEMU cannot run paired singles); the console's self-test runs the
assembly through the same cases.

Build with the game's floating point (-ffp-contract=off, SSE on x86) and run on all three machines the port agrees across:
  cc -O2 -ffp-contract=off -DHALO_BW_KERNELS -DHALO_BW_MODEL -iquote port/linux/include -iquote port/wiiu/src port/wiiu/tests/test_bw_model.c -o test_bw_model
  ./test_bw_model [multiplier]          (port/wiiu/tests/bw_tests.sh does it in the container for the three machines) */

#include "halo_bw_kernels.h"
#include "wiiu_bw_testcases.h"

#include <stdlib.h>

unsigned halo_bw_flags;
unsigned long halo_bw_calls[HALO_BW_K_COUNT], halo_bw_used[HALO_BW_K_COUNT], halo_bw_walked[HALO_BW_K_COUNT];

/* the constant the assembly and the model multiply by is the C's 1.f / 32767 */
static int check_scale(void)
{
	const float scale = 1.f / COMPRESSED_QUATERNION_COMPONENT_MAXIMUM;

	if (bw_bits(scale) != 0x38000100u)
	{
		printf("1.f / 32767 is %08x, not 38000100\n", bw_bits(scale));
		return 1;
	}
	return 0;
}

static int report(const char *name, const struct bw_stats *stats)
{
	printf("%-14s %9lu cases (%lu with two NaN payloads), %lu different%s%s\n", name, stats->cases, stats->lenient, stats->failures,
		stats->failures ? ": " : "", stats->failures ? stats->first : "");
	return stats->failures != 0;
}

int main(int argc, char **argv)
{
	const unsigned long scale = argc > 1 ? strtoul(argv[1], 0, 10) : 1;
	struct bw_stats stats;
	int failed = check_scale(), cls, unavailable;

	printf("pointer %zu bytes, long %zu, float %zu\n", sizeof(void *), sizeof(long), sizeof(float));
	memset(&stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_matrix(halo_bw_model_matrix4x3_multiply, cls, 200000ul * scale, 1u + (unsigned)cls, &stats);
	failed |= report("matrix", &stats);
	memset(&stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_sphere(halo_bw_model_ray_sphere, cls, 1000000ul * scale, 11u + (unsigned)cls, &stats);
	failed |= report("ray-sphere", &stats);
	memset(&stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_ray_walk(halo_bw_model_bsp_ray_walk, cls, 300000ul * scale, 21u + (unsigned)cls, &stats);
	failed |= report("bsp ray walk", &stats);
	memset(&stats, 0, sizeof(stats));
	for (cls = 0; cls < BW_CLASSES; cls++)
		bw_test_sphere_walk(halo_bw_model_bsp_sphere_walk, cls, 300000ul * scale, 31u + (unsigned)cls, &stats);
	failed |= report("bsp sphere walk", &stats);
	memset(&stats, 0, sizeof(stats));
	bw_test_find(halo_bw_model_reference_find, 1000000ul * scale, 41u, &stats);
	failed |= report("reference find", &stats);
	memset(&stats, 0, sizeof(stats));
	bw_test_qload(halo_bw_model_quaternion_load, 1000000ul * scale, 51u, &stats, &unavailable);
	failed |= report("quaternion load", &stats);
	printf(failed ? "bw model: DIFFERENT\n" : "bw model: every case equals the C\n");
	return failed;
}
