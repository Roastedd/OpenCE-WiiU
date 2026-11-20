/*
WIIU_HTTPS.C

An HTTPS GET for the server browser (wiiu_https.h): wiiu_update_http.c's Mbed TLS client, with the roots of the updater (wiiu_update_roots.h), made on
first use and kept. Built with the C library's ABI, as wiiu_update_http.c is; the same source runs on a desktop for tests/test_https.c.

The network is used only while wiiu_network_allowed() says so (the foreground, not closing, not stopped by the system): that is also the stop hook
of the transfer, asked at least every 100 ms, so HOME or the close ends a request within a quarter of a second. Nothing is sent before the server's
certificate chains to a root and names the host. No plain HTTP is made, and no redirect followed.
*/

#include "wiiu_https.h"

#include "wiiu_shell.h"
#include "wiiu_update_http.h"
#include "wiiu_update_roots.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* the whole request, name lookup apart (a dead network or a server that trickles must not hold the list's thread for long; the test shortens it) */
#ifndef WIIU_HTTPS_TOTAL_MS
#define WIIU_HTTPS_TOTAL_MS 60000u
#endif

static pthread_mutex_t creation_lock = PTHREAD_MUTEX_INITIALIZER;
static wiiu_http *shared_http;

typedef struct sink
{
	unsigned char *data;
	size_t size, capacity, maximum;
} sink;

static int sink_stop(void *context)
{
	(void)context;
	return !wiiu_network_allowed();
}

static int sink_start(void *context, int status, uint64_t offset, uint64_t total)
{
	sink *s = context;

	(void)status;
	(void)offset;
	(void)total;
	s->size = 0;
	return 1;
}

static int sink_body(void *context, const unsigned char *data, size_t size)
{
	sink *s = context;

	if (size > s->maximum - s->size)
		return 0;
	/* (room for the NUL after the body) */
	if (s->size + size + 1 > s->capacity)
	{
		size_t capacity = s->capacity ? s->capacity : 16384u;
		unsigned char *grown;

		while (capacity < s->size + size + 1)
			capacity *= 2;
		grown = realloc(s->data, capacity);
		if (!grown)
			return 0;
		s->data = grown;
		s->capacity = capacity;
	}
	memcpy(s->data + s->size, data, size);
	s->size += size;
	s->data[s->size] = 0;
	return 1;
}

/* (the tests' own roots, if they set any) */
static const unsigned char *const *other_roots;
static const size_t *other_root_sizes;
static unsigned other_root_count;

static wiiu_http *http_for_requests(char *error, size_t error_size)
{
	wiiu_http *http;

	pthread_mutex_lock(&creation_lock);
	if (!shared_http)
	{
		if (other_root_count)
			shared_http = wiiu_http_create(other_roots, other_root_sizes, other_root_count, error, error_size);
		else
			shared_http = wiiu_http_create(wiiu_update_roots, wiiu_update_root_sizes, WIIU_UPDATE_ROOT_COUNT, error, error_size);
	}
	http = shared_http;
	pthread_mutex_unlock(&creation_lock);
	return http;
}

void wiiu_https_use_roots(const unsigned char *const *roots, const size_t *sizes, unsigned count)
{
	pthread_mutex_lock(&creation_lock);
	wiiu_http_destroy(shared_http);
	shared_http = NULL;
	other_roots = roots;
	other_root_sizes = sizes;
	other_root_count = count;
	pthread_mutex_unlock(&creation_lock);
}

/* what the console's clock says, for a failure that may be the clock's (a certificate is checked against it) */
static void say_clock(char *error, int error_size)
{
	time_t now = time(NULL);
	struct tm broken;
	char text[48];
	size_t used = strlen(error);

	if (now == (time_t)-1 || !gmtime_r(&now, &broken) || !strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S UTC", &broken))
		return;
	if (used + 1 < (size_t)error_size)
		snprintf(error + used, (size_t)error_size - used, " (the clock reads %s)", text);
}

int wiiu_https_get(const char *url, const char *headers, unsigned long maximum, int *status, char *response_headers, int response_headers_size,
	char **body, unsigned long *body_size, char *error, int error_size)
{
	wiiu_http_hooks hooks;
	wiiu_http_request_options options;
	wiiu_http_result result;
	sink s;
	wiiu_http *http;
	char reason[200];
	int request_result;

	*status = 0;
	*body = NULL;
	*body_size = 0;
	if (response_headers && response_headers_size > 0)
		response_headers[0] = 0;
	if (!error || error_size <= 0)
		return 0;
	error[0] = 0;
	if (!url || strncasecmp(url, "https://", 8) != 0)
	{
		snprintf(error, (size_t)error_size, "not an https:// address (the Wii U makes no plain HTTP requests): %.100s", url ? url : "(none)");
		return 0;
	}
	if (!wiiu_network_allowed())
	{
		snprintf(error, (size_t)error_size, "the network may not be used now (HOME has the foreground, the app is closing, or the system stopped it)");
		return 0;
	}
	reason[0] = 0;
	http = http_for_requests(reason, sizeof(reason));
	if (!http)
	{
		snprintf(error, (size_t)error_size, "HTTPS cannot be used: %s", reason);
		return 0;
	}
	memset(&s, 0, sizeof(s));
	s.maximum = maximum;
	hooks.context = &s;
	hooks.stop = sink_stop;
	hooks.start = sink_start;
	hooks.body = sink_body;
	memset(&options, 0, sizeof(options));
	options.headers = headers;
	options.response_headers = response_headers;
	options.response_headers_size = response_headers && response_headers_size > 0 ? (size_t)response_headers_size : 0;
	options.total_ms = WIIU_HTTPS_TOTAL_MS;
	request_result = wiiu_http_request(http, url, maximum, &hooks, &options, &result);
	if (request_result != 0)
	{
		free(s.data);
		snprintf(error, (size_t)error_size, "%s", result.error[0] ? result.error : "the request failed");
		if (strstr(error, "certificate") || strstr(error, "handshake"))
			say_clock(error, error_size);
		if (response_headers && response_headers_size > 0)
			response_headers[0] = 0;
		return 0;
	}
	if (!s.data)
		s.data = calloc(1, 1);
	if (!s.data)
	{
		snprintf(error, (size_t)error_size, "no memory");
		return 0;
	}
	*status = result.status;
	*body = (char *)s.data;
	*body_size = (unsigned long)s.size;
	return 1;
}
