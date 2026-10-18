/*
REPORT.H

What the probe finds out, as lines of text: a section heading, a value, a check that passed or
failed, a note. Each line goes into a fixed buffer (nothing here allocates, so it is as safe in a
fault handler's neighbourhood as it can be) and to a sink, which shows it on the screen or writes
it to the card; the buffer is what the report file is written from, a stage at a time.

The lines are plain ASCII, one per line, so that the report is read as well by a person as by a
script:

	[section]
	key = value
	PASS name: detail
	FAIL name: detail
	# note
*/

#ifndef HALO_WIIU_REPORT_H
#define HALO_WIIU_REPORT_H

#include <stddef.h>

typedef enum report_kind
{
	REPORT_SECTION,
	REPORT_VALUE,
	REPORT_PASS,
	REPORT_FAIL,
	REPORT_NOTE
} report_kind;

/* line is without its newline and valid only during the call */
typedef void (*report_sink)(void *user, report_kind kind, const char *line);

typedef struct report
{
	char *text;
	size_t capacity;
	size_t length;
	size_t flushed;
	int truncated;
	unsigned checks;
	unsigned failures;
	report_sink sink;
	void *sink_user;
} report;

#if defined(__GNUC__) || defined(__clang__)
#define REPORT_PRINTF(string, first) __attribute__((format(printf, string, first)))
#else
#define REPORT_PRINTF(string, first)
#endif

/* storage holds the report text (a line that does not fit is dropped and truncated is set) */
void report_init(report *r, char *storage, size_t capacity, report_sink sink, void *sink_user);

void report_section(report *r, const char *format, ...) REPORT_PRINTF(2, 3);
void report_value(report *r, const char *key, const char *format, ...) REPORT_PRINTF(3, 4);
void report_check(report *r, int passed, const char *name, const char *format, ...) REPORT_PRINTF(4, 5);
void report_note(report *r, const char *format, ...) REPORT_PRINTF(2, 3);

/* the "[summary]" section: the checks, the failures, whether lines were lost */
void report_summary(report *r);

/* The text not yet handed out, and its length; report_mark_flushed(r, size) after it is written. */
const char *report_pending(const report *r, size_t *size);
void report_mark_flushed(report *r, size_t size);

#endif
