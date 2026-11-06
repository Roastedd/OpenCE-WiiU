/*
The Wii U's HTTPS GET for the server browser (port/wiiu/src/wiiu_https.c over wiiu_update_http.c) on a desktop, driven by tools/test_wiiu_https.py
against local TLS servers with a test CA (and, when asked, the master servers themselves with the console's own roots).

    test_https get URL MAXIMUM BODY_OUT HEADERS_OUT [ROOTS [EXTRA_HEADERS [DENY_AFTER_MS]]]
    test_https parallel URL THREADS ROOTS
    test_https method METHOD URL

ROOTS is a comma-separated list of DER files, or "embedded" (the console's own, wiiu_update_roots.h). EXTRA_HEADERS has \r and \n written as
those two letters after a backslash. DENY_AFTER_MS makes wiiu_network_allowed() say no from then on (HOME). Each mode prints one line of key=value
results; the last field, error=, is the rest of the line.

"method" goes through the platform layer's contract instead (a copy of platform_wiiu.c's update_https_request, which this build cannot compile): a
method other than GET is refused.
*/

#include "wiiu_https.h"

#include <pthread.h>
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

static volatile unsigned long long deny_at;

/* the shell's (wiiu_shell.c) */
int wiiu_network_allowed(void)
{
	return !deny_at || now_ms() < deny_at;
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

static int use_roots(const char *roots)
{
	static const unsigned char *ders[16];
	static size_t sizes[16];
	unsigned count = 0;
	char list[2048], *name, *rest;

	if (!strcmp(roots, "embedded"))
		return 0;
	snprintf(list, sizeof(list), "%s", roots);
	for (name = strtok_r(list, ",", &rest); name && count < 16; name = strtok_r(NULL, ",", &rest))
	{
		ders[count] = read_file(name, &sizes[count]);
		if (!ders[count])
		{
			printf("status=0 body=0 headers=0 error=the root %s cannot be read\n", name);
			return -1;
		}
		count++;
	}
	wiiu_https_use_roots(ders, sizes, count);
	return 0;
}

static char *unescape(const char *text)
{
	char *out = malloc(strlen(text) + 1), *at = out;

	for (; *text; text++)
	{
		if (text[0] == '\\' && text[1] == 'r')
			*at++ = '\r', text++;
		else if (text[0] == '\\' && text[1] == 'n')
			*at++ = '\n', text++;
		else
			*at++ = *text;
	}
	*at = 0;
	return out;
}

/* a copy of platform_wiiu.c's update_https_request, for the method's check (it takes the game's struct update_response there) */
static int request(const char *method, const char *url, const char *headers, const void *body, unsigned long body_size, unsigned long maximum,
	int *status, char *response_headers, int response_headers_size, char **answer, unsigned long *answer_size, char *error, int error_size)
{
	*status = 0;
	*answer = NULL;
	*answer_size = 0;
	if (!method || strcmp(method, "GET"))
	{
		snprintf(error, (size_t)error_size, "the Wii U makes only GET requests (not %.16s)", method ? method : "none");
		return 0;
	}
	if (body || body_size)
	{
		snprintf(error, (size_t)error_size, "a GET request with a body is not made");
		return 0;
	}
	return wiiu_https_get(url, headers, maximum, status, response_headers, response_headers_size, answer, answer_size, error, error_size);
}

struct job
{
	const char *url;
	int ok, bad;
};

static void *job_main(void *argument)
{
	struct job *job = argument;
	int i;

	for (i = 0; i < 3; i++)
	{
		char headers[4096], error[300];
		char *body;
		unsigned long size;
		int status;

		if (wiiu_https_get(job->url, NULL, 100000, &status, headers, sizeof(headers), &body, &size, error, sizeof(error)) && status == 200 && size > 0)
			job->ok++;
		else
			job->bad++;
		free(body);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	if (argc >= 6 && !strcmp(argv[1], "get"))
	{
		char headers[4096] = "", error[300] = "";
		char *body = NULL, *extra = argc > 7 ? unescape(argv[7]) : NULL;
		unsigned long size = 0;
		int status = 0, ok;
		unsigned long long started;

		if (argc > 6 && use_roots(argv[6]) != 0)
			return 0;
		if (argc > 8)
			deny_at = now_ms() + strtoull(argv[8], NULL, 10);
		started = now_ms();
		ok = request("GET", argv[2], extra && extra[0] ? extra : NULL, NULL, 0, strtoul(argv[3], NULL, 10), &status, headers, sizeof(headers), &body,
			&size, error, sizeof(error));
		if (ok)
		{
			FILE *file = fopen(argv[4], "wb");

			if (file)
			{
				fwrite(body, 1, size, file);
				fclose(file);
			}
			/* (the buffer is NUL terminated: its length by strlen is the size unless the body has a NUL) */
			printf("ok=1 status=%d body=%lu nul=%d ms=%llu error=\n", status, size, body[size] == 0, now_ms() - started);
			file = fopen(argv[5], "wb");
			if (file)
			{
				fwrite(headers, 1, strlen(headers), file);
				fclose(file);
			}
		}
		else
			printf("ok=0 status=%d body=%lu nul=0 ms=%llu error=%s\n", status, size, now_ms() - started, error);
		free(body);
		free(extra);
		return 0;
	}
	if (argc == 5 && !strcmp(argv[1], "parallel"))
	{
		enum { MOST = 16 };
		pthread_t threads[MOST];
		struct job jobs[MOST];
		int count = atoi(argv[3]), i, ok = 0, bad = 0;

		if (count < 1 || count > MOST || use_roots(argv[4]) != 0)
			return 1;
		for (i = 0; i < count; i++)
		{
			jobs[i].url = argv[2];
			jobs[i].ok = jobs[i].bad = 0;
			pthread_create(&threads[i], NULL, job_main, &jobs[i]);
		}
		for (i = 0; i < count; i++)
		{
			pthread_join(threads[i], NULL);
			ok += jobs[i].ok;
			bad += jobs[i].bad;
		}
		printf("ok=%d bad=%d error=\n", ok, bad);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "method"))
	{
		char headers[4096], error[300] = "";
		char *body = NULL;
		unsigned long size = 0;
		int status = 0, ok = request(argv[2], argv[3], NULL, NULL, 0, 1000, &status, headers, sizeof(headers), &body, &size, error, sizeof(error));

		printf("ok=%d status=%d error=%s\n", ok, status, error);
		free(body);
		return 0;
	}
	fprintf(stderr, "usage: see the top of this file\n");
	return 2;
}
