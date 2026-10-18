/*
The probe's report (src/report.c) and the stages that are the same on the console and on a desktop
(abi, numeric, maths), on known answers. Built for the desktop, for 32-bit x86 and for 32-bit
big-endian PowerPC with the game's own floating-point flags, and run with the sanitizers where there
are any: tools/wiiu/probe_tests.sh. What the machine is expected to be comes in as macros
(EXPECT_*), because only the script knows what it built for.
*/

#include "../src/bench.h"
#include "../src/maths_check.h"
#include "../src/stage.h"

#include <stdio.h>
#include <string.h>

#ifndef EXPECT_BIG_ENDIAN
#error "the expectations of the machine (EXPECT_*) come from tools/wiiu/probe_tests.sh"
#endif

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("check failed, line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

typedef struct sink_state
{
	unsigned lines[5];
} sink_state;

static void count_lines(void *user, report_kind kind, const char *line)
{
	sink_state *state = user;

	(void)line;
	state->lines[kind]++;
}

static uint64_t fake_clock(void)
{
	static uint64_t now;

	now += 1000000;
	return now;
}

static int always(void)
{
	return 1;
}

static int calls_left;

static int a_few_times(void)
{
	return calls_left-- > 0;
}

static int contains(const report *r, const char *text)
{
	return strstr(r->text, text) != NULL;
}

static void test_report(void)
{
	char storage[400];
	report r;
	sink_state sink = {{0}};
	char long_text[600];
	size_t size;
	const char *pending;
	unsigned i;

	report_init(&r, storage, sizeof(storage), count_lines, &sink);
	report_section(&r, "memory %d", 1);
	report_value(&r, "size", "%u MiB", 128u);
	report_check(&r, 1, "mapped", "at %08X", 0x80000000u);
	report_check(&r, 0, "written", "read back %d", 7);
	report_note(&r, "tab\there and a newline\nand \x01 control");
	CHECK(strcmp(r.text, "[memory 1]\nsize = 128 MiB\nPASS mapped: at 80000000\nFAIL written: read back 7\n"
		"# tab?here and a newline?and ? control\n") == 0);
	CHECK(r.checks == 2 && r.failures == 1 && !r.truncated);
	CHECK(sink.lines[REPORT_SECTION] == 1 && sink.lines[REPORT_VALUE] == 1 && sink.lines[REPORT_PASS] == 1 &&
		sink.lines[REPORT_FAIL] == 1 && sink.lines[REPORT_NOTE] == 1);

	/* handed out in pieces, each piece once */
	pending = report_pending(&r, &size);
	CHECK(pending == r.text && size == r.length);
	report_mark_flushed(&r, 11);
	pending = report_pending(&r, &size);
	CHECK(pending == r.text + 11 && size == r.length - 11);
	report_mark_flushed(&r, 100000);
	report_pending(&r, &size);
	CHECK(size == 0);

	/* a line that is too long ends in "...", and nothing is written past its end */
	memset(long_text, 'x', sizeof(long_text) - 1);
	long_text[sizeof(long_text) - 1] = 0;
	report_init(&r, storage, sizeof(storage), NULL, NULL);
	report_value(&r, "long", "%s", long_text);
	CHECK(strstr(r.text, "xxx...\n") != NULL);
	CHECK(r.length < 250 && r.text[r.length] == 0);

	/* full: lines are dropped, counted and the text stays terminated */
	report_init(&r, storage, sizeof(storage), count_lines, &sink);
	for (i = 0; i < 100; i++)
		report_check(&r, i != 50, "filler", "line %u of a report with no room", i);
	CHECK(r.truncated);
	CHECK(r.length < sizeof(storage) && storage[r.length] == 0);
	CHECK(r.checks == 100 && r.failures == 1);
	report_summary(&r);
	CHECK(r.checks == 100);

	/* nothing to write into */
	report_init(&r, storage, 0, NULL, NULL);
	report_note(&r, "dropped");
	CHECK(r.truncated && r.length == 0);

	report_init(&r, storage, sizeof(storage), NULL, NULL);
	report_check(&r, 1, "a", "b");
	report_summary(&r);
	CHECK(contains(&r, "[summary]\nchecks = 1\nfailed = 0\nreport truncated = no\n"));
}

static void stage_run(void (*stage)(stage_context *), report *r, char *storage, size_t size, int (*keep_going)(void))
{
	stage_context context;

	report_init(r, storage, size, NULL, NULL);
	context.report = r;
	context.now_ns = fake_clock;
	context.keep_going = keep_going;
	context.flush = NULL;
	stage(&context);
}

static void abi_expected(stage_abi_expectation *expected)
{
	expected->big_endian = EXPECT_BIG_ENDIAN;
	expected->pointer_size = EXPECT_POINTER;
	expected->long_size = EXPECT_LONG;
	expected->wchar_size = 2;
	expected->double_alignment = 8;
	expected->char_signed = 1;
}

static void test_abi(void)
{
	static char storage[8192];
	report r;
	stage_abi_expectation expected;
	stage_context context;

	abi_expected(&expected);
	report_init(&r, storage, sizeof(storage), NULL, NULL);
	context.report = &r;
	context.now_ns = fake_clock;
	context.keep_going = always;
	context.flush = NULL;
	stage_abi(&context, &expected);
	if (r.failures)
		printf("%s", r.text);
	CHECK(r.failures == 0 && r.checks >= 9);

	/* the check can fail: told the opposite byte order, it must say so twice (the order and the bit-fields) */
	expected.big_endian = !expected.big_endian;
	report_init(&r, storage, sizeof(storage), NULL, NULL);
	stage_abi(&context, &expected);
	CHECK(r.failures == 2);
	CHECK(contains(&r, "FAIL byte order") && contains(&r, "FAIL bit-field order"));

	/* and told a wrong size */
	abi_expected(&expected);
	expected.pointer_size = EXPECT_POINTER == 4 ? 8 : 4;
	report_init(&r, storage, sizeof(storage), NULL, NULL);
	stage_abi(&context, &expected);
	CHECK(r.failures == 1 && contains(&r, "FAIL pointer size"));
}

static void test_numeric(void)
{
	static char storage[8192];
	report r;

	stage_run(stage_numeric, &r, storage, sizeof(storage), always);
	if (r.failures)
		printf("%s", r.text);
	CHECK(r.failures == 0 && r.checks >= 7);
	CHECK(contains(&r, "int32 of NaN = ") && contains(&r, "0/0 bits = "));
}

/* a floating-point unit that starts as the console's does, with the non-IEEE bit set, and one that cannot clear it */
static uint32_t fake_fpscr;

static uint32_t fake_read(void)
{
	return fake_fpscr;
}

static void fake_clear(void)
{
	fake_fpscr &= ~4u;
}

static void fake_cannot_clear(void)
{
}

static void test_numeric_with_the_non_ieee_bit_set(void)
{
	static char storage[8192];
	report r;

	/* as the first console run found it (FPSCR 82028004): the stage says what the bit does, clears it, and says so */
	fake_fpscr = 0x82028004u;
	stage_numeric_use_fpu(fake_read, fake_clear);
	stage_run(stage_numeric, &r, storage, sizeof(storage), always);
	CHECK(contains(&r, "FPSCR at start = 82028004") && contains(&r, "# non-IEEE mode is on at start"));
	CHECK(contains(&r, "FPSCR after clearing NI = 82028000") && contains(&r, "PASS non-IEEE mode cleared"));
	CHECK(r.failures == 0);

	/* one that cannot be cleared is a failure the report names */
	fake_fpscr = 0x82028004u;
	stage_numeric_use_fpu(fake_read, fake_cannot_clear);
	stage_run(stage_numeric, &r, storage, sizeof(storage), always);
	CHECK(contains(&r, "FAIL non-IEEE mode cleared") && r.failures == 1);

	/* one that starts clean has nothing to clear */
	fake_fpscr = 0x00000000u;
	stage_numeric_use_fpu(fake_read, fake_clear);
	stage_run(stage_numeric, &r, storage, sizeof(storage), always);
	CHECK(contains(&r, "PASS non-IEEE mode: NI 0 at start") && !contains(&r, "cleared") && r.failures == 0);

	/* a rounding mode that is not to-nearest, and traps enabled, are failures too */
	fake_fpscr = 0x00000003u | 0x80u;
	stage_run(stage_numeric, &r, storage, sizeof(storage), always);
	CHECK(contains(&r, "FAIL rounding mode") && contains(&r, "FAIL trapping exceptions"));
	stage_numeric_use_fpu(NULL, NULL);
	stage_run(stage_numeric, &r, storage, sizeof(storage), always);
	CHECK(contains(&r, "# FPSCR: not PowerPC, not read") && r.failures == 0);
}

static void test_maths(void)
{
	static char storage[8192];
	report r;

	stage_run(stage_maths, &r, storage, sizeof(storage), always);
	if (r.failures)
		printf("%s", r.text);
	CHECK(r.failures == 0 && r.checks == MATHS_CHECK_COUNT);
	/* the fake clock moves a millisecond per reading: a function's inputs took that, 10 ns each */
	CHECK(contains(&r, "sin = 10 ns per call") && contains(&r, "pow = 10 ns per call"));

	/* asked three times (before the stage and after sin and cos), then stopped after tan: the rest are said
	not to have run, and have not failed */
	calls_left = 3;
	stage_run(stage_maths, &r, storage, sizeof(storage), a_few_times);
	CHECK(r.failures == 0 && r.checks == 3);
	CHECK(contains(&r, "# asin: not run (stopped)") && contains(&r, "# pow: not run (stopped)") &&
		!contains(&r, "# tan: not run"));
}

static void test_bench(void)
{
	static uint32_t source[1024];
	static uint32_t destination[1024];
	const size_t size = sizeof(source);
	uint32_t sink = 0;
	double chain_sink = 0.0;
	size_t i;

	for (i = 0; i < 1024; i++)
		source[i] = 0x01020304u + (uint32_t)i;

	/* each reading of the fake clock is a millisecond later: a run is a millisecond long, whatever it did */
	CHECK(bench_copy(fake_clock, destination, source, size) == 1e6 / (double)size);
	CHECK(memcmp(destination, source, size) == 0);
	CHECK(bench_fill(fake_clock, destination, size) == 1e6 / (double)size);
	CHECK(((const unsigned char *)destination)[0] == 4 && ((const unsigned char *)destination)[size - 1] == 4);
	CHECK(bench_read(fake_clock, source, size, &sink) == 1e6 / (double)size);
	CHECK(sink != 0);

	/* five runs swap five times: the words are swapped once */
	memcpy(destination, source, size);
	CHECK(bench_swap32(fake_clock, destination, size) == 1e6 / (double)size);
	CHECK(destination[0] == 0x04030201u && destination[1023] == __builtin_bswap32(source[1023]));

	CHECK(bench_float_chain(fake_clock, &chain_sink) == 1e6 / 2e6);
	CHECK(chain_sink > 6.0 && chain_sink < 6.1); /* five runs of about 1.21: (1 + 1e-7 / 1e-7) * e^0.1 - 1 */
	sink = 0;
	CHECK(bench_integer_chain(fake_clock, &sink) == 1e6 / 2e6);
	CHECK(sink != 0);
}

int main(void)
{
	test_bench();
	test_report();
	test_abi();
	test_numeric();
	test_numeric_with_the_non_ieee_bit_set();
	test_maths();
	if (failures)
	{
		printf("%d checks failed\n", failures);
		return 1;
	}
	printf("stage tests passed\n");
	return 0;
}
