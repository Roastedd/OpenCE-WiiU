/*
ANSWERS.H

The probe's findings in one place: what each stage learned that decides something, as a key and one line, collected as
the stages run and written to the report at the end as a section of their own (and shown on the last page). The
report is long, and the person reading it wants to know first what the console said to each of the questions.
*/

#ifndef HALO_WIIU_GX2_ANSWERS_H
#define HALO_WIIU_GX2_ANSWERS_H

#include "report.h"

#define ANSWERS_MAX 48
#define ANSWER_KEY_SIZE 40
#define ANSWER_TEXT_SIZE 168

typedef struct answers
{
	char key[ANSWERS_MAX][ANSWER_KEY_SIZE];
	char text[ANSWERS_MAX][ANSWER_TEXT_SIZE];
	unsigned count;
	unsigned lost; /* answers that did not fit */
} answers;

void answers_init(answers *a);
/* adds a finding; a key given twice keeps the later text */
void answers_set(answers *a, const char *key, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
	__attribute__((format(printf, 3, 4)))
#endif
	;
const char *answers_get(const answers *a, const char *key); /* NULL if there is none */
/* the section [answers] of the report: key = text, in the order they were set */
void answers_report(const answers *a, report *r);

#endif
