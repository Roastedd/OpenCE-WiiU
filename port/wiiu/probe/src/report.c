#include "report.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LINE_SIZE 240

static void append(report *r, report_kind kind, char *line)
{
	size_t length = strlen(line);

	if (kind == REPORT_PASS || kind == REPORT_FAIL)
		r->checks++;
	if (kind == REPORT_FAIL)
		r->failures++;
	if (r->length + length + 2 <= r->capacity)
	{
		memcpy(r->text + r->length, line, length);
		r->length += length;
		r->text[r->length++] = '\n';
		r->text[r->length] = 0;
	}
	else
		r->truncated = 1;
	if (r->sink)
		r->sink(r->sink_user, kind, line);
}

static void sanitize(char *line)
{
	size_t i;

	for (i = 0; line[i]; i++)
		if ((unsigned char)line[i] < 0x20 || (unsigned char)line[i] > 0x7E)
			line[i] = '?';
}

/* head, then the text; a line longer than it can be ends in "...", and nothing but printable ASCII is kept */
static void emit(report *r, report_kind kind, const char *head, const char *format, va_list arguments)
{
	char line[LINE_SIZE];
	size_t used = 0;
	int written;

	written = snprintf(line, sizeof(line), "%s", head);
	if (written > 0)
		used = (size_t)written < sizeof(line) ? (size_t)written : sizeof(line) - 1;
	written = vsnprintf(line + used, sizeof(line) - used, format, arguments);
	if (written >= 0 && (size_t)written >= sizeof(line) - used)
	{
		used = sizeof(line) - 1;
		line[used - 3] = '.';
		line[used - 2] = '.';
		line[used - 1] = '.';
	}
	sanitize(line);
	append(r, kind, line);
}

void report_init(report *r, char *storage, size_t capacity, report_sink sink, void *sink_user)
{
	memset(r, 0, sizeof(*r));
	r->text = storage;
	r->capacity = capacity;
	r->sink = sink;
	r->sink_user = sink_user;
	if (capacity)
		storage[0] = 0;
}

void report_section(report *r, const char *format, ...)
{
	char line[LINE_SIZE];
	va_list arguments;
	size_t length;

	line[0] = '[';
	va_start(arguments, format);
	vsnprintf(line + 1, sizeof(line) - 2, format, arguments);
	va_end(arguments);
	length = strlen(line);
	line[length] = ']';
	line[length + 1] = 0;
	sanitize(line);
	append(r, REPORT_SECTION, line);
}

void report_value(report *r, const char *key, const char *format, ...)
{
	va_list arguments;
	char head[LINE_SIZE];

	snprintf(head, sizeof(head), "%s = ", key);
	va_start(arguments, format);
	emit(r, REPORT_VALUE, head, format, arguments);
	va_end(arguments);
}

void report_check(report *r, int passed, const char *name, const char *format, ...)
{
	va_list arguments;
	char head[LINE_SIZE];

	snprintf(head, sizeof(head), "%s %s: ", passed ? "PASS" : "FAIL", name);
	va_start(arguments, format);
	emit(r, passed ? REPORT_PASS : REPORT_FAIL, head, format, arguments);
	va_end(arguments);
}

void report_note(report *r, const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	emit(r, REPORT_NOTE, "# ", format, arguments);
	va_end(arguments);
}

void report_summary(report *r)
{
	report_section(r, "summary");
	report_value(r, "checks", "%u", r->checks);
	report_value(r, "failed", "%u", r->failures);
	report_value(r, "report truncated", "%s", r->truncated ? "yes" : "no");
}

const char *report_pending(const report *r, size_t *size)
{
	*size = r->length - r->flushed;
	return r->text + r->flushed;
}

void report_mark_flushed(report *r, size_t size)
{
	r->flushed += size;
	if (r->flushed > r->length)
		r->flushed = r->length;
}
