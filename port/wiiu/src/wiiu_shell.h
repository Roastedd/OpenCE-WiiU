/*
WIIU_SHELL.H

What the rest of the Wii U port calls of the shell: the thing that runs around the game on a Wii U (src/wiiu_shell.c).

The shell owns the process. The system's entry (main, src/wiiu_main.c) starts it; it starts ProcUI, the log, the watchdog
and the black box, runs the game's own main on a thread of its own (the system expects the main thread to answer HOME within
moments, which a game cannot promise), and answers the system while the game runs. Everything here is safe to call from any
thread of the game once the shell has started, and does nothing that waits for the SD card or the network: the log is a queue
a worker writes out, the rest are stores to variables.

Not run on a console.
*/

#ifndef HALO_WIIU_SHELL_H
#define HALO_WIIU_SHELL_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The folder of everything the port keeps on the SD card: the log, the black box, crash notes, settings, saves. */
#define WIIU_DATA_DIRECTORY "/vol/external01/wiiu/apps/halo"

/* Runs the shell with the game's main (shell_xbox.c's, renamed halo_game_main by tools/wiiu_build.py) and returns its
result when it has ended, or the system has asked the app to close. */
int wiiu_shell_run(int (*game_main)(void));

/* A line in the log (also the system's debug log); the second form takes text as it is. A line is cut at 239 characters. */
void wiiu_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void wiiu_log_text(const char *text);

/* What this thread is doing now, a string literal: the heartbeat, the stall report and the crash note say it. */
void wiiu_phase(const char *phase);

/* The game's loop went round once (the renderer's present). A stall is told only after the first. */
void wiiu_frame(void);

/* The system has asked the app to close, or the game asked to quit: the game should finish what it is doing and return. */
int wiiu_exit_requested(void);

/* 0 while HOME has the foreground: nothing may be drawn, and the GPU's memory is not the app's. */
int wiiu_in_foreground(void);

/* Quit, as the menus' Quit does (platform_request_quit): the shell leaves its loop and the app closes. */
void wiiu_request_exit(void);

/* Registers the calling thread for the black box under a name (a string literal), until it ends or untags itself. */
void wiiu_tag_thread(const char *name);
void wiiu_untag_thread(void);

#ifdef __cplusplus
}
#endif

#endif
