/*
The three cores, one at a time and then the same work: how fast each moves memory, swaps byte
order and runs a dependent chain of operations. Espresso's cores are not equal (core 1 has 2 MiB of
level 2 cache, cores 0 and 2 have 512 KiB), and the plan puts the game thread on one and the
audio, I/O and decompression on the others, so this decides which.

The workers touch nothing shared: each fills in its own result structure, which the main thread
reports after joining them (the report is not safe to write from two threads).
*/

#include "../bench.h"
#include "../stage.h"
#include "clock.h"

#include <coreinit/core.h>
#include <coreinit/thread.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CORES 3
#define STACK_SIZE (64 * 1024)
#define SMALL (256u * 1024u)
#define LARGE (8u * 1024u * 1024u)

typedef struct core_job
{
	uint8_t *source;
	uint8_t *destination;
	unsigned core_asked;
	unsigned core_ran;
	double copy_small, copy_large, fill_large, read_large, swap_small, swap_large, float_chain, integer_chain;
	uint32_t sink;
	uint32_t fpscr;
	double float_sink;
} core_job;

static OSThread threads[CORES];
static uint8_t stacks[CORES][STACK_SIZE] __attribute__((aligned(16)));
static core_job jobs[CORES];

static int worker(int argc, const char **argv)
{
	core_job *job = (core_job *)(uintptr_t)argv;

	(void)argc;
	job->core_ran = OSGetCoreId();
	/* (a new thread's FPSCR: whether the non-IEEE mode the main thread started with is every thread's) */
	job->fpscr = stage_read_fpscr();
	job->copy_small = bench_copy(clock_now_ns, job->destination, job->source, SMALL);
	job->copy_large = bench_copy(clock_now_ns, job->destination, job->source, LARGE);
	job->fill_large = bench_fill(clock_now_ns, job->destination, LARGE);
	job->read_large = bench_read(clock_now_ns, job->source, LARGE, &job->sink);
	job->swap_small = bench_swap32(clock_now_ns, job->destination, SMALL);
	job->swap_large = bench_swap32(clock_now_ns, job->destination, LARGE);
	job->float_chain = bench_float_chain(clock_now_ns, &job->float_sink);
	job->integer_chain = bench_integer_chain(clock_now_ns, &job->sink);
	return 0;
}

static double megabytes_per_second(double nanoseconds_per_byte)
{
	return nanoseconds_per_byte > 0.0 ? 1000.0 / nanoseconds_per_byte : 0.0;
}

void stage_cpu(stage_context *context)
{
	report *r = context->report;
	unsigned core;

	if (context->keep_going && !context->keep_going())
		return;
	report_section(r, "cpu");
	memset(jobs, 0, sizeof(jobs));
	for (core = 0; core < CORES; core++)
	{
		jobs[core].core_asked = core;
		jobs[core].source = memalign(32, LARGE);
		jobs[core].destination = memalign(32, LARGE);
		if (!jobs[core].source || !jobs[core].destination)
		{
			report_check(r, 0, "benchmark buffers", "2 x 8 MiB for core %u not available", core);
			free(jobs[core].source);
			free(jobs[core].destination);
			jobs[core].source = jobs[core].destination = NULL;
			continue;
		}
		memset(jobs[core].source, 0x5A, LARGE);
		memset(jobs[core].destination, 0xA5, LARGE);
	}

	/* one core at a time, so that they do not compete for the memory bus */
	for (core = 0; core < CORES; core++)
	{
		int result = 0;

		if (!jobs[core].source)
			continue;
		if (!OSCreateThread(&threads[core], worker, (int32_t)core, (char *)&jobs[core], stacks[core] + STACK_SIZE, STACK_SIZE, 16,
				(OSThreadAttributes)(OS_THREAD_ATTRIB_AFFINITY_CPU0 << core)))
		{
			report_check(r, 0, "worker thread", "OSCreateThread failed for core %u", core);
			continue;
		}
		OSSetThreadName(&threads[core], "probe worker");
		OSResumeThread(&threads[core]);
		OSJoinThread(&threads[core], &result);
		report_check(r, jobs[core].core_ran == core, "worker core", "asked for core %u, ran on %u", core, jobs[core].core_ran);
		report_value(r, "core", "%u: FPSCR at thread start %08X", core, (unsigned)jobs[core].fpscr);
		report_value(r, "core", "%u: copy %.0f MB/s in 256 KiB, %.0f MB/s in 8 MiB; fill %.0f; read %.0f", core,
			megabytes_per_second(jobs[core].copy_small), megabytes_per_second(jobs[core].copy_large),
			megabytes_per_second(jobs[core].fill_large), megabytes_per_second(jobs[core].read_large));
		report_value(r, "core", "%u: byte swap %.0f MB/s in 256 KiB, %.0f MB/s in 8 MiB; double mul+add %.2f ns/op; integer mul+add %.2f ns/op",
			core, megabytes_per_second(jobs[core].swap_small), megabytes_per_second(jobs[core].swap_large), jobs[core].float_chain,
			jobs[core].integer_chain);
		if (context->keep_going && !context->keep_going())
			break;
	}
	for (core = 0; core < CORES; core++)
	{
		free(jobs[core].source);
		free(jobs[core].destination);
	}
}
