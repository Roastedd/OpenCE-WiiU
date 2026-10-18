#include "stage.h"

#include "maths_check.h"

void stage_maths(stage_context *context)
{
	report *r = context->report;
	maths_check_result results[MATHS_CHECK_COUNT];
	maths_check_hooks hooks;
	int i;

	if (context->keep_going && !context->keep_going())
		return;
	report_section(r, "maths");
	hooks.now_ns = context->now_ns;
	hooks.keep_going = context->keep_going;
	maths_check_run(results, &hooks);
	for (i = 0; i < MATHS_CHECK_COUNT; i++)
	{
		const maths_check_expected *expected = &maths_check_expected_table[i];

		if (!results[i].ran)
		{
			report_note(r, "%s: not run (stopped)", expected->name);
			continue;
		}
		report_check(r, results[i].checksum == expected->checksum, expected->name, "%08X, expected %08X",
			(unsigned)results[i].checksum, (unsigned)expected->checksum);
		if (results[i].nanoseconds)
			report_value(r, expected->name, "%.0f ns per call", (double)results[i].nanoseconds / MATHS_CHECK_INPUTS);
	}
}
