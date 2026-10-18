/*
Prints the maths checksums (src/maths_check.c) of the machine it runs on: tools/wiiu/probe_tests.sh
builds it for the container's CPU (ARM64), for 32-bit x86 and for 32-bit big-endian PowerPC, with
the game's own flags for the maths (port/third_party/musl-math), and the three must print the same.
*/

#include "../src/maths_check.h"

#include <stdio.h>

int main(void)
{
	maths_check_result results[MATHS_CHECK_COUNT];
	int i;

	maths_check_run(results, NULL);
	for (i = 0; i < MATHS_CHECK_COUNT; i++)
		printf("%s %08X\n", results[i].name, results[i].checksum);
	return 0;
}
