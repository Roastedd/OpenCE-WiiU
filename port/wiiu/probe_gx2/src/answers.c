#include "answers.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void answers_init(answers *a)
{
	memset(a, 0, sizeof(*a));
}

void answers_set(answers *a, const char *key, const char *format, ...)
{
	unsigned i;
	va_list arguments;

	for (i = 0; i < a->count; i++)
		if (!strcmp(a->key[i], key))
			break;
	if (i == a->count)
	{
		if (a->count == ANSWERS_MAX)
		{
			a->lost++;
			return;
		}
		snprintf(a->key[i], ANSWER_KEY_SIZE, "%s", key);
		a->count++;
	}
	va_start(arguments, format);
	vsnprintf(a->text[i], ANSWER_TEXT_SIZE, format, arguments);
	va_end(arguments);
}

const char *answers_get(const answers *a, const char *key)
{
	unsigned i;

	for (i = 0; i < a->count; i++)
		if (!strcmp(a->key[i], key))
			return a->text[i];
	return NULL;
}

void answers_report(const answers *a, report *r)
{
	unsigned i;

	report_section(r, "answers");
	for (i = 0; i < a->count; i++)
		report_value(r, a->key[i], "%s", a->text[i]);
	if (a->lost)
		report_note(r, "%u answers did not fit", a->lost);
}
