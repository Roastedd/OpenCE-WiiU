/*
WIIU_SHELL_REPORT.C

The one-file bug report (wiiu_shell_report.h).
*/

#include "wiiu_shell_report.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* what each section may take of the 200 KB (the sum is less) */
#define HEAD_BYTES (10u * 1024u)
#define CRASH_BYTES (16u * 1024u)
#define BLACKBOX_BYTES (12u * 1024u)
#define TRAIL_BYTES (12u * 1024u)
#define LOG_TAIL_BYTES (96u * 1024u)
#define DEBUG_TAIL_BYTES (40u * 1024u)
#define WINDOW_BYTES (LOG_TAIL_BYTES + 4096u)

static char window[WINDOW_BYTES];

/* the last `lines` lines of the file, at most `capacity` bytes of them (the front is dropped to a line's start); returns the length at *start in window, or -1 if the file
cannot be read, 0 if it is empty */
static long file_end(const char *path, unsigned lines, size_t capacity, size_t *start)
{
	FILE *file;
	long size, from;
	size_t got, begin, seen = 0;

	if (!path || !(file = fopen(path, "rb")))
		return -1;
	if (capacity > sizeof(window))
		capacity = sizeof(window);
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	if (size <= 0)
	{
		fclose(file);
		return 0;
	}
	from = size > (long)capacity ? size - (long)capacity : 0;
	fseek(file, from, SEEK_SET);
	got = fread(window, 1, capacity, file);
	fclose(file);
	begin = 0;
	if (from > 0)
	{
		/* (cut in a line: it starts at the next) */
		while (begin < got && window[begin] != '\n')
			begin++;
		if (begin < got)
			begin++;
	}
	if (got > begin && window[got - 1] == '\n')
		got--;
	/* the last `lines` lines */
	{
		size_t at = got;

		while (at > begin)
		{
			if (window[at - 1] == '\n' && ++seen == lines)
				break;
			at--;
		}
		begin = at;
	}
	*start = begin;
	return (long)(got - begin);
}

/* the first `lines` lines of the file, at most `capacity` bytes */
static long file_head(const char *path, unsigned lines, size_t capacity, size_t *start)
{
	FILE *file;
	size_t got, at, seen = 0;

	if (!path || !(file = fopen(path, "rb")))
		return -1;
	if (capacity > sizeof(window))
		capacity = sizeof(window);
	got = fread(window, 1, capacity, file);
	fclose(file);
	for (at = 0; at < got; at++)
		if (window[at] == '\n' && ++seen == lines)
			break;
	if (at < got)
		got = at;
	*start = 0;
	return (long)got;
}

static int put(FILE *out, size_t *total, const char *text, size_t length)
{
	if (*total + length > WIIU_REPORT_LIMIT_BYTES)
		return -1;
	if (length && fwrite(text, 1, length, out) != length)
		return -1;
	*total += length;
	return 0;
}

static int put_text(FILE *out, size_t *total, const char *text)
{
	return put(out, total, text, strlen(text));
}

/* a heading, then the text read from the file (or what is to be said if there is none) */
static int section(FILE *out, size_t *total, const char *heading, const char *path, int head, unsigned lines, size_t capacity, const char *none)
{
	size_t start = 0;
	long length;
	char line[400];

	snprintf(line, sizeof(line), "\n==== %s ====\n", heading);
	if (put_text(out, total, line))
		return -1;
	length = head ? file_head(path, lines, capacity, &start) : file_end(path, lines, capacity, &start);
	if (length < 0)
		return put_text(out, total, none);
	if (length == 0)
		return put_text(out, total, "(the file is empty)\n");
	if (put(out, total, window + start, (size_t)length) || put_text(out, total, "\n"))
		return -1;
	return 0;
}

int wiiu_shell_report_make(const wiiu_shell_report_config *config, size_t *bytes)
{
	char temporary[512];
	FILE *out;
	size_t total = 0;
	int failed = 0;
	const unsigned head_lines = config->log_head_lines ? config->log_head_lines : 30;
	const unsigned tail_lines = config->log_tail_lines ? config->log_tail_lines : 400;
	const unsigned debug_lines = config->debug_tail_lines ? config->debug_tail_lines : 100;
	char heading[160];

	if (!config->output || strlen(config->output) + 5 > sizeof(temporary))
		return 1;
	snprintf(temporary, sizeof(temporary), "%s.tmp", config->output);
	out = fopen(temporary, "wb");
	if (!out)
		return 2;
	failed |= put_text(out, &total, "OpenCE for the Wii U: report of the last run (made at the start of the next; the files' own text, cut to their ends)\n");
	failed |= put_text(out, &total, config->free_space ? config->free_space : "Free space on the card: not read (reading it takes seconds on a big card)");
	failed |= put_text(out, &total, "\n");
	failed |= section(out, &total, "build and start of the last run (the top of log.txt)", config->log, 1, head_lines, HEAD_BYTES, "(there was no log.txt)\n");
	failed |= section(out, &total, "crash note (crash.txt)", config->crash, 1, 400, CRASH_BYTES, "(none: the last run left no crash note)\n");
	failed |= section(out, &total, "black box (blackbox.txt)", config->blackbox, 1, 400, BLACKBOX_BYTES, "(none: the last run ended cleanly, or left no black box)\n");
	failed |= section(out, &total, "close trail (exit-trail.txt)", config->exit_trail, 1, 400, TRAIL_BYTES, "(none)\n");
	snprintf(heading, sizeof(heading), "the last %u lines of the last run's log (log.txt)", tail_lines);
	failed |= section(out, &total, heading, config->log, 0, tail_lines, LOG_TAIL_BYTES, "(there was no log.txt)\n");
	snprintf(heading, sizeof(heading), "the last %u lines of data/debug.txt", debug_lines);
	failed |= section(out, &total, heading, config->debug, 0, debug_lines, DEBUG_TAIL_BYTES, "(there was no data/debug.txt)\n");
	failed |= put_text(out, &total, "\n==== end of the report ====\n");
	if (fclose(out) != 0)
		failed = 1;
	if (failed)
	{
		unlink(temporary);
		return 3;
	}
	unlink(config->output);
	if (rename(temporary, config->output) != 0)
	{
		unlink(temporary);
		return 4;
	}
	if (bytes)
		*bytes = total;
	return 0;
}
