/*
WIIU_SHELL_REPORT.H

The one-file bug report. A player who has a problem is asked for six files (log.txt, log-previous.txt, crash.txt, blackbox.txt, exit-trail.txt, data/debug.txt); with
halo/make_report.txt (the number 1) present at the start, the shell gathers what matters of them into one text file, report.txt, before the new log replaces the last
run's. The text is the files' own, cut to their ends: nothing is added to it that they do not hold. No console calls: a desktop tests it.
*/

#ifndef HALO_WIIU_SHELL_REPORT_H
#define HALO_WIIU_SHELL_REPORT_H

#include <stddef.h>

#define WIIU_REPORT_LIMIT_BYTES (200u * 1024u)

typedef struct wiiu_shell_report_config
{
	const char *output; /* report.txt: written whole, to output + ".tmp" first and then renamed */
	const char *log; /* the last run's log (the top of it names the build, its end says how it went) */
	const char *crash; /* the last run's crash note, if any */
	const char *blackbox; /* its black box, if any */
	const char *exit_trail; /* its close trail */
	const char *debug; /* data/debug.txt */
	const char *free_space; /* a line about the card's free space, or NULL when it is not known cheaply */
	unsigned log_head_lines; /* 30 if 0 */
	unsigned log_tail_lines; /* 400 if 0 */
	unsigned debug_tail_lines; /* 100 if 0 */
} wiiu_shell_report_config;

/* Writes the report: 0 and its size in *bytes when it is made (at most WIIU_REPORT_LIMIT_BYTES); another number if it could not be (nothing is left of a partial one). */
int wiiu_shell_report_make(const wiiu_shell_report_config *config, size_t *bytes);

#endif
