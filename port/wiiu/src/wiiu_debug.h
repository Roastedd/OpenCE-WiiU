/*
WIIU_DEBUG.H

The debug server inside the game's own shell: a text protocol on TCP port 4310 (wiiu_debug_protocol.h; the client is tools/wiiu/wiiu_debug.py) that
quits the app, takes a picture of what is on the screens, presses buttons and sticks, and reads the log, and a UDP mirror of the log's lines to the
developer's machine. It exists so that a change can be run, looked at and judged without a person at the console.

Replies are one line, "ok ..." or "error ...", and a screenshot or a log is followed by the bytes it names. The server's thread uses only the
network; the picture is taken by the game's thread at its next present, so nothing here touches the GPU in the background. The sockets are closed
whenever the app is in the background, closing, or told by the system to leave the network.

The log mirror is off unless the file WIIU_DATA_DIRECTORY/debug_log_host.txt names an address ("192.168.1.10" or "192.168.1.10:4311"); each log
line is then one datagram to it.
*/

#ifndef HALO_WIIU_DEBUG_H
#define HALO_WIIU_DEBUG_H

#include <gx2/surface.h>

#include <stddef.h>

#define WIIU_DEBUG_PORT 4310
#define WIIU_DEBUG_MIRROR_PORT 4311

struct wiiu_pad;

/* The shell, once the game's thread runs. memory writes a line about the memory into out (and returns its length); it may be NULL. */
void wiiu_debug_start(size_t (*memory)(char *out, size_t capacity));

/* Every log line, from any thread (the shell's echo) */
void wiiu_debug_log_line(const char *line);

/* The system allows (1) or stops (0) networking for the app */
void wiiu_debug_network(int allowed);

/* The GX2 backend, at the end of each present, on the game's thread: takes the picture asked for, if one was. back is the game's own render target
(may be NULL); tv and drc are the screens' colour buffers as they were just drawn. */
void wiiu_debug_present(const GX2ColorBuffer *back, const GX2ColorBuffer *tv, const GX2ColorBuffer *drc);

/* wiiu_input_read, for the GamePad's port: adds what the debug client is holding */
void wiiu_debug_input(struct wiiu_pad *pad);

#endif
