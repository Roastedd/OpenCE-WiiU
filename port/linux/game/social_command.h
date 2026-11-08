/*
SOCIAL_COMMAND.H

The friends layer as lines of text, for testing it before the screens exist: the Wii U's debug server ("console social ..." with debug.wiiu_console on), the script console
(debug.telnet_console: "social ...") and the harness. Every line runs on the game's thread through social.h's calls, and the answer is lines of text.

	help | code | status | list | requests | sent | invites | events | last
	add <code> | accept <n> | ignore <n> | cancel <n> | remove <n> | block <n> | unblock <n> | nick <n> <text>
	invite <n> | join <n> | joininvite <n> | joinstatus | joincancel | joindone
	party | party make | party invite <n> | party join <n> | party joinfriend <n> | party leave | party kick <m> | party transfer <m> | party chat <text> | party open | party close
	party follow | party unfollow
	name <text> | share online|offline | details none|basic|full | requests on|off | activity menus|lobby|game|solo|busy
	timing

A number is the place in the last list (from 1). */

#ifndef __HALO_LINUX_SOCIAL_COMMAND_H
#define __HALO_LINUX_SOCIAL_COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Runs a line; the answer in reply (lines ended by newlines, at most size - 1 characters). 1 if the line was a command, 0 if it was not one. */
int social_command(const char *line, char *reply, int size);

#ifdef __cplusplus
}
#endif

#endif
