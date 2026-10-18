/*
The Wii U probe: what the console and Cafe OS allow, measured instead of assumed (plan phase 0).

It runs its stages once, one after the other, and writes what it finds as text:

 - on screen as it goes, the checks and notes (the values are in the file);
 - to /vol/external01/wiiu/apps/opence-probe/report.txt on the SD card, after every stage and
   before the ones that might not return, so that the report ends where the console stopped;
 - to crash.txt, if the system tells us about a fault.

Nothing here keeps the console busy: the stages ask the system whether the user has pressed HOME
between steps, and a stage that has finished leaves the screen up until the user leaves.
*/

#include "../stage.h"
#include "card.h"
#include "clock.h"
#include "crash.h"

#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <stdio.h>
#include <string.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/proc.h>

#define REPORT_CAPACITY (96 * 1024)
#define FAILURES_SHOWN 12
#define FAILURE_WIDTH 100

/* what the port's code assumes of the machine (plan section 6.3) */
static const stage_abi_expectation expected_abi = {1, 4, 4, 2, 8, 1};

static char report_storage[REPORT_CAPACITY];
static report probe_report;
static int card_error;
static int card_started;
static char failed_lines[FAILURES_SHOWN][FAILURE_WIDTH];
static unsigned failed_count;

static void sink(void *user, report_kind kind, const char *line)
{
	(void)user;
	if (kind == REPORT_FAIL)
	{
		if (failed_count < FAILURES_SHOWN)
			snprintf(failed_lines[failed_count], FAILURE_WIDTH, "%s", line);
		failed_count++;
	}
	if (kind != REPORT_VALUE)
		WHBLogPrint(line);
}

/* Writes what the report holds that the card does not. The first write makes the file, the rest add to it. */
static void flush_report(void)
{
	size_t size;
	const char *pending = report_pending(&probe_report, &size);
	int error;

	if (!size)
		return;
	error = card_write("report.txt", pending, size, card_started);
	if (error)
	{
		if (!card_error)
			WHBLogPrintf("cannot write the report to the card: error %d", error);
		card_error = error;
		return; /* what could not be written is tried again with the next stage */
	}
	card_started = 1;
	report_mark_flushed(&probe_report, size);
}

/* Asks the system whether the user has asked to leave (HOME, then closing the app). WHBProcIsRunning does not
come back while the app is in the background, and nothing is asked of it after it said no. */
static int leaving;

static int keep_going(void)
{
	if (!leaving && !WHBProcIsRunning())
		leaving = 1;
	return !leaving;
}

static void stage_abi_as_expected(stage_context *context)
{
	stage_abi(context, &expected_abi);
}

static void run_stage(stage_context *context, const char *name, void (*stage)(stage_context *))
{
	if (!keep_going())
		return;
	crash_set_stage(name);
	WHBLogPrintf("running %s ...", name);
	WHBLogConsoleDraw();
	report_value(context->report, "stage", "%s started", name);
	flush_report();
	stage(context);
	flush_report();
	WHBLogConsoleDraw();
}

int main(void)
{
	stage_context context;
	unsigned i;

	WHBProcInit();
	WHBLogConsoleInit();
	crash_install();
	card_prepare();

	report_init(&probe_report, report_storage, sizeof(report_storage), sink, NULL);
	context.report = &probe_report;
	context.now_ns = clock_now_ns;
	context.keep_going = keep_going;
	context.flush = flush_report;

	WHBLogPrint("OpenCE Wii U probe");
	WHBLogConsoleDraw();
	run_stage(&context, "system", stage_system);
	run_stage(&context, "abi", stage_abi_as_expected);
	run_stage(&context, "numeric", stage_numeric);
	run_stage(&context, "maths", stage_maths);
	run_stage(&context, "cpu", stage_cpu);
	run_stage(&context, "memory", stage_memory);

	crash_set_stage("finished");
	if (leaving)
		report_note(&probe_report, "stopped early: the user left");
	report_summary(&probe_report);
	flush_report();

	WHBLogPrint("");
	WHBLogPrint("== finished ==");
	WHBLogPrintf("%u checks, %u failed", probe_report.checks, probe_report.failures);
	for (i = 0; i < failed_count && i < FAILURES_SHOWN; i++)
		WHBLogPrint(failed_lines[i]);
	if (failed_count > FAILURES_SHOWN)
		WHBLogPrintf("(%u more failures in the report)", failed_count - FAILURES_SHOWN);
	if (card_error)
		WHBLogPrintf("the report could not be written to the card (error %d)", card_error);
	else
		WHBLogPrint("report: " PROBE_DIRECTORY "/report.txt");
	WHBLogPrint("Press HOME to leave.");

	while (keep_going())
	{
		WHBLogConsoleDraw();
		OSSleepTicks(OSMillisecondsToTicks(50));
	}

	WHBLogConsoleFree();
	WHBProcShutdown();
	return 0;
}
