/*
WIIU_DEBUG.C

The debug server (wiiu_debug.h has what it is for and the protocol's shape). One thread, on core 0, serves one client at a time: it reads a line,
does it, answers, and between lines pushes the log's new lines to the UDP mirror. The sockets are wut's (nsysnet): accept answers EBUSY when no one is
waiting, and a send takes about a kilobyte at a time, as the Wiiload plugin's did.

A picture is taken by the game's thread, because only it may use the GPU: the server asks, the game's next present copies the screen into a linear
surface (the GPU does the untiling) and says so, and the server sends the pixels from there.
*/

#include "wiiu_debug.h"

#include "wiiu_debug_protocol.h"
#include "wiiu_input.h"
#include "wiiu_shell.h"

#include "port_config.h"

#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/surface.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define STACK_BYTES (96 * 1024)
#define SEND_CHUNK 4096
#define SHOT_WAIT_MS 3000
#define LOG_SLOTS 256
#define LOG_LINE_BYTES 240
#define MIRROR_LINES_AT_A_TIME 16
#define MIRROR_FILE WIIU_DATA_DIRECTORY "/debug_log_host.txt"
#define CLIENT_IDLE_MS 120000u

/* ---------- the log's last lines */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char ring[LOG_SLOTS][LOG_LINE_BYTES];
static uint32_t ring_total; /* lines ever added; line n is in slot n % LOG_SLOTS */
static char log_out[WIIU_DEBUG_LOG_MAX * LOG_LINE_BYTES];

void wiiu_debug_log_line(const char *line)
{
	pthread_mutex_lock(&lock);
	strncpy(ring[ring_total % LOG_SLOTS], line, LOG_LINE_BYTES - 1);
	ring[ring_total % LOG_SLOTS][LOG_LINE_BYTES - 1] = 0;
	ring_total++;
	pthread_mutex_unlock(&lock);
}

/* the last count lines, each with its newline, in log_out; returns the length */
static size_t last_lines(unsigned int count)
{
	size_t used = 0;
	uint32_t first;

	pthread_mutex_lock(&lock);
	if (count > ring_total)
		count = ring_total;
	if (count > LOG_SLOTS)
		count = LOG_SLOTS;
	for (first = ring_total - count; first != ring_total; first++)
	{
		size_t length = strlen(ring[first % LOG_SLOTS]);

		memcpy(log_out + used, ring[first % LOG_SLOTS], length);
		used += length;
		log_out[used++] = '\n';
	}
	pthread_mutex_unlock(&lock);
	return used;
}

/* ---------- the buttons held for the game */

static uint32_t now_ms(void)
{
	return (uint32_t)OSTicksToMilliseconds(OSGetSystemTime());
}

static volatile unsigned int held_buttons;
static volatile uint32_t held_buttons_until;
static float held_gyro[2];
static volatile uint32_t held_gyro_until;
static float held_stick[4];
static volatile uint32_t held_stick_until;
static volatile int holding;

void wiiu_debug_input(struct wiiu_pad *pad)
{
	const uint32_t now = now_ms();

	if (!holding)
		return;
	if ((int32_t)(held_gyro_until - now) > 0)
	{
		pad->gyro_pitch = held_gyro[0];
		pad->gyro_yaw = held_gyro[1];
	}
	if ((int32_t)(held_buttons_until - now) > 0)
		pad->buttons |= held_buttons;
	if ((int32_t)(held_stick_until - now) > 0)
	{
		pad->left_x = held_stick[0];
		pad->left_y = held_stick[1];
		pad->right_x = held_stick[2];
		pad->right_y = held_stick[3];
	}
}

static float held_tap[2];
static volatile uint32_t held_tap_until;
static volatile int tapping;

void wiiu_debug_touch(int *down, float *x, float *y)
{
	if (!tapping || (int32_t)(held_tap_until - now_ms()) <= 0)
		return;
	*down = 1;
	*x = held_tap[0];
	*y = held_tap[1];
}

/* ---------- the pictures */

enum
{
	SHOT_IDLE,
	SHOT_REQUESTED,
	SHOT_BUSY,
	SHOT_READY,
	SHOT_FAILED,
};

static struct
{
	volatile int state; /* changed under the lock */
	int which;
	void *image;
	uint32_t width, height, pitch, format, size;
	const char *why;
} shot;
static volatile uint32_t frames;
/* quit: set by the server, taken by the game's thread at its next present (the route a run that ends by itself takes, which ends cleanly) */
static volatile int quit_asked;
static volatile int quit_made;

/* the colour buffer, copied by the GPU into linear RGBA bytes the CPU can read */
static int copy_frame(const GX2ColorBuffer *source)
{
	GX2Surface target;

	if (source->surface.aa != GX2_AA_MODE1X || ((uint32_t)source->surface.format & 0x3f) != (uint32_t)GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8)
	{
		shot.why = "that screen is not 8 bits a channel, without anti-aliasing";
		return 0;
	}
	if (source->surface.width > 1920)
	{
		shot.why = "that screen is wider than 1920 pixels";
		return 0;
	}
	wiiu_log("Debug: copying a %ux%u surface, format 0x%x, tile mode %u, use 0x%x", (unsigned)source->surface.width, (unsigned)source->surface.height,
		(unsigned)source->surface.format, (unsigned)source->surface.tileMode, (unsigned)source->surface.use);
	memset(&target, 0, sizeof(target));
	target.use = GX2_SURFACE_USE_TEXTURE;
	target.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	target.width = source->surface.width;
	target.height = source->surface.height;
	target.depth = 1;
	target.mipLevels = 1;
	/* (the same format as the source: a copy between two formats froze the console) */
	target.format = source->surface.format;
	target.aa = GX2_AA_MODE1X;
	target.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment(&target);
	target.image = MEMAllocFromDefaultHeapEx(target.imageSize, target.alignment ? target.alignment : 256);
	if (!target.image)
	{
		shot.why = "no memory for the picture";
		return 0;
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, target.image, target.imageSize);
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), source->surface.image, source->surface.imageSize);
	GX2CopySurface(&source->surface, source->viewMip, source->viewFirstSlice, &target, 0, 0);
	GX2DrawDone();
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, target.image, target.imageSize);
	wiiu_log("Debug: the copy is done");
	shot.image = target.image;
	shot.width = source->surface.width;
	shot.height = source->surface.height;
	shot.pitch = target.pitch;
	shot.format = (uint32_t)source->surface.format;
	shot.size = target.imageSize;
	return 1;
}

void wiiu_debug_present(const GX2ColorBuffer *back, const GX2ColorBuffer *tv, const GX2ColorBuffer *drc)
{
	const GX2ColorBuffer *source;
	int which;
	int made;

	frames++;
	if (quit_asked && !quit_made)
	{
		quit_made = 1;
		/* (the game's own quit: pending shader-capture records are written first, then the Menu is asked for) */
		wiiu_request_exit();
	}
	if (shot.state != SHOT_REQUESTED)
		return;
	pthread_mutex_lock(&lock);
	if (shot.state != SHOT_REQUESTED)
	{
		pthread_mutex_unlock(&lock);
		return;
	}
	shot.state = SHOT_BUSY;
	which = shot.which;
	pthread_mutex_unlock(&lock);
	source = which == WIIU_DEBUG_SHOT_TV ? tv : which == WIIU_DEBUG_SHOT_DRC ? drc : back;
	shot.why = "that screen has nothing drawn yet";
	made = source && copy_frame(source);
	pthread_mutex_lock(&lock);
	shot.state = made ? SHOT_READY : SHOT_FAILED;
	pthread_mutex_unlock(&lock);
}

/* ---------- the sockets */

static OSThread thread;
static uint8_t stack[STACK_BYTES] __attribute__((aligned(16)));
static size_t (*memory_text)(char *out, size_t capacity);
static volatile int network_allowed = 1;
static volatile int started;
static int server = -1;
static int client = -1;
static int mirror = -1;
static struct sockaddr_in mirror_address;
static int mirror_wanted;
static uint32_t mirror_sent; /* lines of the ring already sent */

void wiiu_debug_network(int allowed)
{
	network_allowed = allowed;
}

/* may the sockets be open? (not in the background, when the system has taken the network, or when the app is closing) */
static int may_use_network(void)
{
	return network_allowed && wiiu_in_foreground() && !wiiu_exit_requested();
}

static void close_socket(int *fd)
{
	if (*fd >= 0)
	{
		shutdown(*fd, SHUT_RDWR);
		close(*fd);
		*fd = -1;
	}
}

static void sleep_ms(uint32_t milliseconds)
{
	OSSleepTicks(OSMillisecondsToTicks(milliseconds));
}

static void pump_mirror(void);

static int send_all(const void *data, size_t size)
{
	const uint8_t *bytes = (const uint8_t *)data;
	uint32_t last_progress = now_ms();

	while (size > 0)
	{
		size_t n = size < SEND_CHUNK ? size : SEND_CHUNK;
		int32_t sent;

		/* (a picture takes seconds: the log goes on) */
		pump_mirror();
		sent = send(client, bytes, (int32_t)n, 0);

		if (sent > 0)
		{
			bytes += sent;
			size -= (size_t)sent;
			last_progress = now_ms();
		}
		else if (now_ms() - last_progress > 10000u || !may_use_network())
		{
			return 0;
		}
		else
		{
			sleep_ms(1);
		}
	}
	return 1;
}

static int send_text(const char *text)
{
	return send_all(text, strlen(text));
}

/* ---------- the log's mirror */

/* the address in the file on the card, if there is one */
static void read_mirror_file(void)
{
	char text[96];
	FILE *file = fopen(MIRROR_FILE, "r");
	char *colon;
	unsigned long port = WIIU_DEBUG_MIRROR_PORT;

	mirror_wanted = 0;
	if (!file)
		return;
	if (fgets(text, sizeof(text), file))
	{
		size_t length = strcspn(text, " \t\r\n");

		text[length] = 0;
		colon = strchr(text, ':');
		if (colon)
		{
			*colon = 0;
			port = strtoul(colon + 1, NULL, 10);
		}
		memset(&mirror_address, 0, sizeof(mirror_address));
		mirror_address.sin_family = AF_INET;
		mirror_address.sin_port = htons((uint16_t)port);
		if (port && port < 65536 && inet_aton(text, &mirror_address.sin_addr))
		{
			mirror_wanted = 1;
			/* (what was logged before the mirror started is not sent: the card's file has it) */
			pthread_mutex_lock(&lock);
			mirror_sent = ring_total;
			pthread_mutex_unlock(&lock);
		}
	}
	fclose(file);
}

static void pump_mirror(void)
{
	unsigned int sent_now = 0;

	if (!mirror_wanted)
		return;
	if (mirror < 0)
	{
		mirror = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (mirror < 0)
			return;
	}
	while (sent_now < MIRROR_LINES_AT_A_TIME)
	{
		char line[LOG_LINE_BYTES];

		pthread_mutex_lock(&lock);
		if (mirror_sent == ring_total)
		{
			pthread_mutex_unlock(&lock);
			return;
		}
		/* (a mirror that fell more than the ring behind has lost those lines) */
		if (ring_total - mirror_sent > LOG_SLOTS)
			mirror_sent = ring_total - LOG_SLOTS;
		strcpy(line, ring[mirror_sent % LOG_SLOTS]);
		mirror_sent++;
		pthread_mutex_unlock(&lock);
		sendto(mirror, line, (int)strlen(line), 0, (struct sockaddr *)&mirror_address, sizeof(mirror_address));
		sent_now++;
	}
}

/* ---------- the commands */

static void do_hello(void)
{
	char memory[160] = "";
	char text[400];

	if (memory_text)
		memory_text(memory, sizeof(memory));
	snprintf(text, sizeof(text), "ok frames=%u foreground=%d exiting=%d uptime_ms=%u memory=\"%s\"\n", (unsigned)frames, wiiu_in_foreground() ? 1 : 0,
		wiiu_exit_requested() ? 1 : 0, (unsigned)now_ms(), memory);
	send_text(text);
}

static void do_screenshot(int which, unsigned int scale)
{
	const uint32_t started_at = now_ms();
	char text[160];
	int state;

	if (!wiiu_in_foreground())
	{
		send_text("error the app is in the background: nothing is drawn\n");
		return;
	}
	pthread_mutex_lock(&lock);
	if (shot.state != SHOT_IDLE)
	{
		pthread_mutex_unlock(&lock);
		send_text("error a picture is already being taken\n");
		return;
	}
	shot.state = SHOT_REQUESTED;
	shot.which = which;
	pthread_mutex_unlock(&lock);
	for (;;)
	{
		pthread_mutex_lock(&lock);
		state = shot.state;
		if (state == SHOT_REQUESTED && now_ms() - started_at >= SHOT_WAIT_MS)
		{
			shot.state = SHOT_IDLE;
			pthread_mutex_unlock(&lock);
			send_text("error no frame was presented in 3 seconds (loading, or the game thread is stuck)\n");
			return;
		}
		pthread_mutex_unlock(&lock);
		if (state == SHOT_READY || state == SHOT_FAILED)
			break;
		/* (a copy under way is the game thread's: it ends, or the console is lost) */
		if (state == SHOT_BUSY && now_ms() - started_at >= 4u * SHOT_WAIT_MS)
		{
			send_text("error the picture's copy did not finish\n");
			return;
		}
		sleep_ms(5);
	}
	if (state == SHOT_READY)
	{
		const unsigned int across = (shot.width + scale - 1) / scale, down = (shot.height + scale - 1) / scale;

		snprintf(text, sizeof(text), "ok %u %u %u %x %u\n", across, down, across, (unsigned)shot.format, across * down * 4u);
		if (send_text(text))
		{
			uint32_t row[1920 / 1];
			unsigned int y, x;
			int sent_all = 1;

			for (y = 0; y < down && sent_all; y++)
			{
				const uint32_t *pixels = (const uint32_t *)((const uint8_t *)shot.image + (size_t)y * scale * shot.pitch * 4u);

				for (x = 0; x < across; x++)
					row[x] = pixels[x * scale];
				sent_all = send_all(row, across * 4u);
			}
			wiiu_log("Debug: the picture was %s", sent_all ? "sent" : "NOT sent");
		}
		MEMFreeToDefaultHeap(shot.image);
		shot.image = NULL;
	}
	else
	{
		snprintf(text, sizeof(text), "error %s\n", shot.why);
		send_text(text);
	}
	pthread_mutex_lock(&lock);
	shot.state = SHOT_IDLE;
	pthread_mutex_unlock(&lock);
}

/* The Wii U Menu is asked for with every socket closed, by the game's thread at its next present, or by this one if the game does not present within
3 seconds. Measured under Aroma: the app does close (the release and exit callbacks run, "Closed" is logged), but the console may then start the app
again, or stay on the Wii U logo; HOME and Close software is the sure way out. */
static void do_quit(void)
{
	const uint32_t started_at = now_ms();

	send_text("ok the Wii U Menu is asked for\n");
	close_socket(&client);
	close_socket(&server);
	close_socket(&mirror);
	quit_asked = 1;
	while (!quit_made && now_ms() - started_at < 3000u)
		sleep_ms(10);
	if (!quit_made)
	{
		quit_made = 1;
		wiiu_request_exit();
	}
}

int social_command(const char *line, char *reply, int size);

/* console: a script the server took, run by the game's thread at its next frame (wiiu_debug_console_run, from main.c where the telnet console's
are), as its console runs one. The server takes no password, so it runs scripts only with config.toml's debug.wiiu_console (off unless set). One
script waits at a time: the state goes 0 (none) -> 1 (waiting, its text written) -> 0 (taken), each step published with release ordering. */
static char console_script[WIIU_DEBUG_CONSOLE_MAX];
static int console_state;

/* (the hs runtime is the tick thread's: with the tick owners' queues on, the script waits in the queue until that thread
runs it, and the answer is logged when it has; source/hs/hs.c, port/linux/game/tick_owners.h) */
void hs_compile_and_evaluate_queued(char const *expression, void (*done)(long argument, unsigned char succeeded), long argument);

static void console_done(long argument, unsigned char done)
{
	(void)argument;
	wiiu_log("Debug console: %s", done ? "done" : "not done (the log says why)");
}

void wiiu_debug_console_run(void)
{
	char script[WIIU_DEBUG_CONSOLE_MAX];

	if (!__atomic_load_n(&console_state, __ATOMIC_ACQUIRE))
		return;
	memcpy(script, console_script, sizeof(script));
	__atomic_store_n(&console_state, 0, __ATOMIC_RELEASE);
	wiiu_log("Debug console: %s", script);
	/* the friends layer's commands (port/linux/game/social_command.h): "social list", "social add <code>" ...; their answers go to the log, a line each */
	if (!strncmp(script, "social", 6) && (script[6] == ' ' || !script[6]))
	{
		char reply[1536];
		char *line = reply;

		social_command(script + 6, reply, (int)sizeof(reply));
		while (*line)
		{
			char *end = strchr(line, '\n');

			if (end)
				*end = 0;
			wiiu_log("social: %s", line);
			if (!end)
				break;
			line = end + 1;
		}
		return;
	}
	hs_compile_and_evaluate_queued(script, console_done, 0);
}

static void do_request(const wiiu_debug_request *request)
{
	char text[96];

	switch (request->command)
	{
	case WIIU_DEBUG_HELLO:
		do_hello();
		break;
	case WIIU_DEBUG_QUIT:
		do_quit();
		break;
	case WIIU_DEBUG_SCREENSHOT:
		do_screenshot(request->which, request->scale);
		break;
	case WIIU_DEBUG_PRESS:
		held_buttons = request->buttons;
		held_buttons_until = now_ms() + request->milliseconds;
		holding = 1;
		send_text("ok\n");
		break;
	case WIIU_DEBUG_STICK:
		memcpy(held_stick, request->stick, sizeof(held_stick));
		held_stick_until = now_ms() + request->milliseconds;
		holding = 1;
		send_text("ok\n");
		break;
	case WIIU_DEBUG_GYRO:
		held_gyro[0] = request->gyro[0];
		held_gyro[1] = request->gyro[1];
		held_gyro_until = now_ms() + request->milliseconds;
		holding = 1;
		send_text("ok\n");
		break;
	case WIIU_DEBUG_TAP:
		held_tap[0] = request->tap[0];
		held_tap[1] = request->tap[1];
		held_tap_until = now_ms() + request->milliseconds;
		tapping = 1;
		wiiu_log("Debug: a tap at %.0f,%.0f for %u ms", request->tap[0], request->tap[1], request->milliseconds);
		send_text("ok\n");
		break;
	case WIIU_DEBUG_CONSOLE:
		if (!config_boolean("debug.wiiu_console"))
			send_text("error the script console is off (config.toml's debug.wiiu_console)\n");
		else if (__atomic_load_n(&console_state, __ATOMIC_ACQUIRE))
			send_text("error a script is still waiting for the game's next frame\n");
		else
		{
			memcpy(console_script, request->text, sizeof(console_script));
			__atomic_store_n(&console_state, 1, __ATOMIC_RELEASE);
			send_text("ok the game's thread runs it at its next frame (log says what it did)\n");
		}
		break;
	case WIIU_DEBUG_LOG:
	{
		const size_t length = last_lines(request->count);

		snprintf(text, sizeof(text), "ok %u\n", (unsigned)length);
		if (send_text(text))
			send_all(log_out, length);
		break;
	}
	default:
		send_text("error unknown command\n");
		break;
	}
}

/* one client's lines, until it leaves, is idle for two minutes, or the network is taken away */
static void serve_client(void)
{
	char buffer[200];
	size_t used = 0;
	uint32_t last_data = now_ms();

	while (may_use_network() && !quit_asked)
	{
		int32_t got;

		pump_mirror();
		got = recv(client, buffer + used, (int32_t)(sizeof(buffer) - 1 - used), MSG_DONTWAIT);
		if (got == 0)
			return;
		if (got < 0)
		{
			if (now_ms() - last_data > CLIENT_IDLE_MS)
				return;
			sleep_ms(5);
			continue;
		}
		last_data = now_ms();
		used += (size_t)got;
		for (;;)
		{
			char *end = memchr(buffer, '\n', used);
			wiiu_debug_request request;
			size_t consumed;

			if (!end)
				break;
			*end = 0;
			if (end > buffer && end[-1] == '\r')
				end[-1] = 0;
			if (wiiu_debug_parse(buffer, &request))
			{
				do_request(&request);
			}
			else
			{
				char text[128];

				snprintf(text, sizeof(text), "error %s\n", request.error);
				send_text(text);
			}
			consumed = (size_t)(end - buffer) + 1;
			memmove(buffer, buffer + consumed, used - consumed);
			used -= consumed;
		}
		if (used >= sizeof(buffer) - 1)
		{
			/* (longer than any command) */
			used = 0;
		}
	}
}

static int open_server(void)
{
	struct sockaddr_in address;
	uint32_t enable = 1;

	server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (server < 0)
		return 0;
	setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(WIIU_DEBUG_PORT);
	address.sin_addr.s_addr = INADDR_ANY;
	if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(server, 1) < 0)
	{
		close_socket(&server);
		return 0;
	}
	return 1;
}

static int server_main(int argc, const char **argv)
{
	int announced = 0;

	(void)argc;
	(void)argv;
	wiiu_fpu_setup();
	wiiu_tag_thread("debug");
	for (;;)
	{
		if (quit_asked)
		{
			/* (the app is on its way out: nothing is opened again) */
			sleep_ms(50);
			continue;
		}
		if (!may_use_network())
		{
			/* the background, the system's hold on the network, the app closing: nothing open, and nothing done until it is over */
			close_socket(&client);
			close_socket(&server);
			close_socket(&mirror);
			announced = 0;
			while (!may_use_network())
				sleep_ms(50);
			continue;
		}
		if (!announced)
		{
			read_mirror_file();
			announced = 1;
		}
		pump_mirror();
		if (server < 0)
		{
			if (!open_server())
				sleep_ms(500);
			else
				wiiu_log("Debug server listening on port %d%s", WIIU_DEBUG_PORT, mirror_wanted ? ", the log mirrored by UDP" : "");
			continue;
		}
		{
			struct sockaddr_in peer;
			socklen_t length = sizeof(peer);

			client = accept(server, (struct sockaddr *)&peer, &length);
			if (client >= 0)
			{
				wiiu_log("Debug client connected");
				serve_client();
				close_socket(&client);
				wiiu_log("Debug client left");
			}
			else if (errno != EBUSY)
			{
				close_socket(&server);
			}
		}
		sleep_ms(10);
	}
	return 0;
}

void wiiu_debug_start(size_t (*memory)(char *out, size_t capacity))
{
	int32_t priority = OSGetThreadPriority(OSGetCurrentThread()) + 2;

	if (started)
		return;
	started = 1;
	memory_text = memory;
	if (priority > 31)
		priority = 31;
	if (!OSCreateThread(&thread, server_main, 0, NULL, stack + STACK_BYTES, STACK_BYTES, priority, OS_THREAD_ATTRIB_AFFINITY_CPU0))
	{
		wiiu_log("OSCreateThread failed for the debug server");
		return;
	}
	OSSetThreadName(&thread, "debug server");
	OSResumeThread(&thread);
}
