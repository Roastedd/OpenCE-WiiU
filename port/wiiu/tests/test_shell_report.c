/* port/wiiu/src/wiiu_shell_report.c (the one-file bug report) on its own.
   cc -std=gnu11 -Wall -Werror port/wiiu/tests/test_shell_report.c port/wiiu/src/wiiu_shell_report.c   (argv[1]: a fresh folder it may write in)
   Exit status 0 when everything holds; tools/test_wiiu_shell_report.py runs it. */

#include "../src/wiiu_shell_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { printf("check failed, line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

static void lines(const char *path, const char *prefix, int count)
{
	FILE *f = fopen(path, "w");
	int i;

	for (i = 1; i <= count; i++)
		fprintf(f, "%s line %d\n", prefix, i);
	fclose(f);
}

static char *slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	char *text;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	text = calloc(*size + 1, 1);
	if (fread(text, 1, *size, f) != *size)
		*size = 0;
	fclose(f);
	return text;
}

int main(int argc, char **argv)
{
	char log[600], crash[600], debug[600], trail[600], output[600], temporary[600], missing[600];
	wiiu_shell_report_config config;
	size_t bytes = 0, size = 0;
	char *text;

	if (argc < 2)
		return 2;
	snprintf(log, sizeof(log), "%s/log.txt", argv[1]);
	snprintf(crash, sizeof(crash), "%s/crash.txt", argv[1]);
	snprintf(debug, sizeof(debug), "%s/debug.txt", argv[1]);
	snprintf(trail, sizeof(trail), "%s/exit-trail-previous.txt", argv[1]);
	snprintf(output, sizeof(output), "%s/report.txt", argv[1]);
	snprintf(temporary, sizeof(temporary), "%s/report.txt.tmp", argv[1]);
	snprintf(missing, sizeof(missing), "%s/blackbox.txt", argv[1]);
	lines(log, "log", 3000);
	lines(debug, "debug", 500);
	lines(trail, "trail", 4);
	lines(crash, "crash", 6);

	memset(&config, 0, sizeof(config));
	config.output = output;
	config.log = log;
	config.crash = crash;
	config.blackbox = missing; /* (no such file: a clean stop) */
	config.exit_trail = trail;
	config.debug = debug;
	CHECK(wiiu_shell_report_make(&config, &bytes) == 0);
	text = slurp(output, &size);
	CHECK(text && size == bytes && bytes < WIIU_REPORT_LIMIT_BYTES);
	CHECK(access(temporary, F_OK) != 0);
	if (text)
	{
		/* the top of the log, the end of the log (400 lines), the end of debug.txt (100 lines), the crash note whole, the black box said to be none */
		CHECK(strstr(text, "log line 1\n") && strstr(text, "log line 30\n") && !strstr(text, "log line 31\nlog line 32\nlog line 33\nlog line 34\nlog line 35\nlog line 36\nlog line 37\nlog line 38\nlog line 39\nlog line 40\nlog line 41\nlog line 42\nlog line 43\nlog line 44\nlog line 45\nlog line 46\nlog line 47\nlog line 48\nlog line 49\nlog line 50\n"));
		CHECK(strstr(text, "log line 3000\n") && strstr(text, "log line 2601\n") && !strstr(text, "log line 2600\n"));
		CHECK(strstr(text, "debug line 500\n") && strstr(text, "debug line 401\n") && !strstr(text, "debug line 400\n"));
		CHECK(strstr(text, "crash line 1\n") && strstr(text, "crash line 6\n"));
		CHECK(strstr(text, "trail line 4\n"));
		CHECK(strstr(text, "(none: the last run ended cleanly, or left no black box)"));
		CHECK(strstr(text, "Free space on the card: not read"));
	}
	free(text);

	/* big files and long lines: the report stays under the limit and keeps the ends */
	{
		FILE *f = fopen(log, "w");
		int i;

		for (i = 1; i <= 3000; i++)
			fprintf(f, "L%d %0230d\n", i, 7);
		fclose(f);
		f = fopen(debug, "w");
		for (i = 1; i <= 3000; i++)
			fprintf(f, "D%d %0300d\n", i, 7);
		fclose(f);
	}
	config.free_space = "Free space on the card: 1234 MB";
	CHECK(wiiu_shell_report_make(&config, &bytes) == 0 && bytes <= WIIU_REPORT_LIMIT_BYTES);
	text = slurp(output, &size);
	CHECK(text && strstr(text, "L3000 ") && strstr(text, "D3000 ") && strstr(text, "L1 ") && strstr(text, "Free space on the card: 1234 MB"));
	free(text);

	/* a folder that cannot be written: an error, nothing left */
	snprintf(output, sizeof(output), "%s/nowhere/report.txt", argv[1]);
	config.output = output;
	CHECK(wiiu_shell_report_make(&config, &bytes) != 0);

	printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
