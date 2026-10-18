/*
The stages that are the same on the console, run on this machine and printed as the report they
would write: the host's counterpart of what the Wii U gives, for reading side by side
(tools/wiiu/probe_tests.sh prints the differences between the machines it builds for).
*/

#include "../src/stage.h"

#include <stdio.h>
#include <time.h>

#ifndef EXPECT_BIG_ENDIAN
#define EXPECT_BIG_ENDIAN 0
#define EXPECT_POINTER ((unsigned)sizeof(void *))
#define EXPECT_LONG ((unsigned)sizeof(long))
#endif

static uint64_t clock_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static int always(void)
{
	return 1;
}

int main(void)
{
	static char storage[16384];
	report r;
	stage_context context;
	stage_abi_expectation expected = {EXPECT_BIG_ENDIAN, EXPECT_POINTER, EXPECT_LONG, 2, 8, 1};

	report_init(&r, storage, sizeof(storage), NULL, NULL);
	context.report = &r;
	context.now_ns = clock_ns;
	context.keep_going = always;
	context.flush = NULL;
	stage_abi(&context, &expected);
	stage_numeric(&context);
	stage_maths(&context);
	report_summary(&r);
	fputs(r.text, stdout);
	return r.failures != 0;
}
