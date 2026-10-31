/*
The Wii U self-updater's HTTPS (port/wiiu/src/wiiu_update_http.c) on a desktop, driven by tools/test_wiiu_update.py against local
TLS servers (openssl s_server, and Python's for redirects, ranges and broken answers) and, when asked, GitHub itself.

    test_update_http get ROOTS URL FROM MOST OUT [STOP_AFTER_MS]
    test_update_http fetch ROOTS URL CAPACITY OUT
    test_update_http download ROOTS URL PART NEW SIZE SHA256 [PAUSE_AT_BYTES PAUSE_MS [CANCEL_AT_BYTES]]
    test_update_http matches PATH SIZE SHA256 [STOP_AFTER_PIECES]

ROOTS is a comma-separated list of DER files, or "embedded" for the console's own (wiiu_update_roots.h). Each prints one line of
key=value results.
*/

#include "wiiu_update_http.h"
#include "wiiu_update_roots.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned long long now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000u + (unsigned long long)now.tv_nsec / 1000000u;
}

static unsigned char *read_file(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (length < 0)
	{
		fclose(file);
		return NULL;
	}
	data = malloc(length > 0 ? (size_t)length : 1);
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	*size = (size_t)length;
	return data;
}

static wiiu_http *open_http(const char *roots)
{
	const unsigned char *ders[16];
	size_t sizes[16];
	unsigned count = 0;
	char error[200] = "", list[2048];
	char *name, *rest;
	wiiu_http *http;

	if (!strcmp(roots, "embedded"))
		http = wiiu_http_create(wiiu_update_roots, wiiu_update_root_sizes, WIIU_UPDATE_ROOT_COUNT, error, sizeof(error));
	else
	{
		snprintf(list, sizeof(list), "%s", roots);
		for (name = strtok_r(list, ",", &rest); name && count < 16; name = strtok_r(NULL, ",", &rest))
		{
			ders[count] = read_file(name, &sizes[count]);
			if (!ders[count])
			{
				printf("error=the root %s cannot be read\n", name);
				while (count)
					free((void *)ders[--count]);
				return NULL;
			}
			count++;
		}
		http = wiiu_http_create(ders, sizes, count, error, sizeof(error));
		/* (Mbed TLS keeps its own copy of each) */
		while (count)
			free((void *)ders[--count]);
	}
	if (!http)
		printf("error=%s\n", error);
	return http;
}

static int parse_hash(const char *hex, unsigned char out[32])
{
	unsigned i;

	if (strlen(hex) != 64)
		return -1;
	for (i = 0; i < 32; i++)
	{
		unsigned value;

		if (sscanf(hex + i * 2, "%2x", &value) != 1)
			return -1;
		out[i] = (unsigned char)value;
	}
	return 0;
}

/* ---------- get */

typedef struct sink
{
	FILE *file;
	/* when the stop is asked for, and when the hook first said so: what is printed is the time from the asking to the GET's end */
	unsigned long long stop_at;
	unsigned long long stopped_at;
	int start_status;
	unsigned long long start_offset, start_total;
} sink;

static int sink_stop(void *context)
{
	sink *s = context;

	if (s->stop_at && now_ms() >= s->stop_at)
	{
		if (!s->stopped_at)
			s->stopped_at = now_ms();
		return 1;
	}
	return 0;
}

static int sink_start(void *context, int status, uint64_t offset, uint64_t total)
{
	sink *s = context;

	s->start_status = status;
	s->start_offset = offset;
	s->start_total = total;
	return 1;
}

static int sink_body(void *context, const unsigned char *data, size_t size)
{
	sink *s = context;

	return fwrite(data, 1, size, s->file) == size;
}

static int get(int argc, char **argv)
{
	wiiu_http *http = open_http(argv[2]);
	wiiu_http_hooks hooks;
	wiiu_http_result result;
	sink s;
	int got;

	if (!http)
		return 1;
	memset(&s, 0, sizeof(s));
	s.file = fopen(argv[6], "wb");
	if (!s.file)
	{
		printf("error=%s cannot be written\n", argv[6]);
		wiiu_http_destroy(http);
		return 1;
	}
	if (argc > 7)
		s.stop_at = now_ms() + strtoull(argv[7], NULL, 10);
	hooks.context = &s;
	hooks.stop = sink_stop;
	hooks.start = sink_start;
	hooks.body = sink_body;
	got = wiiu_http_get(http, argv[3], strtoull(argv[4], NULL, 10), strtoull(argv[5], NULL, 10), &hooks, &result);
	fclose(s.file);
	printf("got=%d status=%d received=%llu redirects=%d stopped=%d start=%d offset=%llu total=%llu after_stop_ms=%llu host=%s error=%s\n", got,
		result.status, (unsigned long long)result.received, result.redirects, result.stopped, s.start_status, s.start_offset, s.start_total,
		s.stopped_at ? now_ms() - s.stop_at : 0ull, result.host, result.error);
	wiiu_http_destroy(http);
	return 0;
}

static int fetch(char **argv)
{
	wiiu_http *http = open_http(argv[2]);
	wiiu_http_result result;
	size_t capacity = strtoul(argv[4], NULL, 10), size = 0;
	unsigned char *buffer = malloc(capacity ? capacity : 1);
	int got;

	if (!http || !buffer)
	{
		free(buffer);
		return 1;
	}
	got = wiiu_http_fetch(http, argv[3], buffer, capacity, &size, NULL, NULL, &result);
	if (got == 0)
	{
		FILE *file = fopen(argv[5], "wb");

		if (file)
		{
			fwrite(buffer, 1, size, file);
			fclose(file);
		}
	}
	printf("got=%d status=%d size=%lu redirects=%d error=%s\n", got, result.status, (unsigned long)size, result.redirects, result.error);
	free(buffer);
	wiiu_http_destroy(http);
	return 0;
}

/* ---------- download */

typedef struct pausing
{
	uint32_t pause_at, cancel_at, done;
	unsigned pause_ms;
	unsigned long long paused_since;
	int pauses, paused;
	unsigned progress_calls;
} pausing;

static int pausing_stop(void *context)
{
	pausing *p = context;

	return p->cancel_at && p->done >= p->cancel_at;
}

/* HOME, as the console's: the card not written and the network not used for a while, once so much has arrived */
static int pausing_may_write(void *context)
{
	pausing *p = context;

	if (p->pause_at && !p->paused && p->done >= p->pause_at)
	{
		p->paused = 1;
		p->pauses++;
		p->paused_since = now_ms();
	}
	if (p->paused == 1)
	{
		if (now_ms() - p->paused_since < p->pause_ms)
			return 0;
		p->paused = 2;
	}
	return 1;
}

static void pausing_progress(void *context, uint32_t done, uint32_t size)
{
	pausing *p = context;

	(void)size;
	p->done = done;
	p->progress_calls++;
}

static int download(int argc, char **argv)
{
	wiiu_http *http = open_http(argv[2]);
	wiiu_http_download_hooks hooks;
	wiiu_http_result result;
	pausing p;
	unsigned char hash[32];
	int got;

	if (!http || parse_hash(argv[7], hash) != 0)
		return 1;
	memset(&p, 0, sizeof(p));
	if (argc > 9)
	{
		p.pause_at = (uint32_t)strtoul(argv[8], NULL, 10);
		p.pause_ms = (unsigned)strtoul(argv[9], NULL, 10);
	}
	if (argc > 10)
		p.cancel_at = (uint32_t)strtoul(argv[10], NULL, 10);
	hooks.context = &p;
	hooks.stop = pausing_stop;
	hooks.may_write = pausing_may_write;
	hooks.progress = pausing_progress;
	got = wiiu_http_download(http, argv[3], argv[4], argv[5], (uint32_t)strtoul(argv[6], NULL, 10), hash, &hooks, &result);
	printf("result=%d pauses=%d done=%u stopped=%d status=%d error=%s\n", got, p.pauses, (unsigned)p.done, result.stopped, result.status,
		result.error);
	wiiu_http_destroy(http);
	return 0;
}

/* a stop hook that says stop once it has been asked so many times (the pieces of a file being hashed) */
static int counting_stop(void *context)
{
	unsigned *left = context;

	if (!*left)
		return 1;
	(*left)--;
	return 0;
}

int main(int argc, char **argv)
{
	/* (a server that closes while the client writes must not end the test with SIGPIPE) */
	signal(SIGPIPE, SIG_IGN);
	if (argc >= 7 && !strcmp(argv[1], "get"))
		return get(argc, argv);
	if (argc == 6 && !strcmp(argv[1], "fetch"))
		return fetch(argv);
	if (argc >= 8 && !strcmp(argv[1], "download"))
		return download(argc, argv);
	if ((argc == 5 || argc == 6) && !strcmp(argv[1], "matches"))
	{
		unsigned char hash[32];
		unsigned stop_after = argc == 6 ? (unsigned)strtoul(argv[5], NULL, 10) : 0;

		if (parse_hash(argv[4], hash) != 0)
			return 1;
		printf("%d\n", wiiu_http_file_matches(argv[2], (uint32_t)strtoul(argv[3], NULL, 10), hash, argc == 6 ? counting_stop : NULL, &stop_after));
		return 0;
	}
	fprintf(stderr, "usage: see the comment at the top of %s\n", __FILE__);
	return 2;
}
