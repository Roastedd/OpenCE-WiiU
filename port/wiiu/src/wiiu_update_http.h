/*
WIIU_UPDATE_HTTP.H

The self-updater's downloads: HTTP/1.1 GETs over TLS 1.2 or 1.3 (Mbed TLS, port/third_party/mbedtls) on BSD sockets, which on
the Wii U are nsysnet's (the console's own HTTPS, NSSL and nlibcurl, has no root that GitHub's file host chains to) and on a
desktop the system's, where it is tested against local servers (tools/test_wiiu_update.py). The server's certificate must chain
to one of the roots the caller gives and name the host; nothing is sent before that is checked.

The sockets are non-blocking and every wait is at most 100 ms, between which the caller's stop hook is asked, so that HOME, the
app closing or the player giving up ends a transfer within a quarter of a second. (Looking a name up is the system's and cannot
be cut short.)

A download goes to the file's .part, is hashed as it is written, and continues where an earlier attempt stopped (a Range request
after a fresh redirect: GitHub's links to its file host expire). Once it is whole and its size and SHA-256 are the manifest's it
is flushed to the card and renamed to the file's .new.
*/

#ifndef HALO_WIIU_UPDATE_HTTP_H
#define HALO_WIIU_UPDATE_HTTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wiiu_http wiiu_http;

typedef struct wiiu_http_hooks
{
	void *context;
	/* nonzero: give up now (asked at least every 100 ms while a transfer waits) */
	int (*stop)(void *context);
	/* the final response (a 200 or a 206): where its body begins in the file (0 for a 200, the range's first byte for a 206) and
	the file's whole size (0 if the server does not say); 0 refuses it */
	int (*start)(void *context, int status, uint64_t offset, uint64_t total);
	/* the body as it arrives; 0 stops the transfer */
	int (*body)(void *context, const unsigned char *data, size_t size);
} wiiu_http_hooks;

typedef struct wiiu_http_result
{
	int status;        /* the final response's status, 0 if none came */
	int stopped;       /* the stop hook ended it */
	int redirects;     /* how many were followed */
	uint64_t received; /* body bytes given to the body hook */
	char host[128];    /* the last host asked */
	char error[200];   /* why it failed */
} wiiu_http_result;

/* The certificate authorities (DER) a server must chain to; NULL with why in error if they cannot be read or the cryptography
cannot start. */
wiiu_http *wiiu_http_create(const unsigned char *const *roots, const size_t *sizes, unsigned count, char *error, size_t error_size);
void wiiu_http_destroy(wiiu_http *http);

/* GET an https:// address, following up to five redirects (each to its own host if it says so), from byte `from` of the file (a
Range request on every hop when it is not 0). A 200 or 206 has its body read to the end, at most `most` bytes; another status
ends the GET with nothing read. 0 when a final response came (result->status says which), -1 if not (result->error). */
int wiiu_http_get(wiiu_http *http, const char *url, uint64_t from, uint64_t most, const wiiu_http_hooks *hooks, wiiu_http_result *result);

/* a small file whole into memory (the manifest, its signature): 0 on a 200 whose body fits */
int wiiu_http_fetch(wiiu_http *http, const char *url, void *buffer, size_t capacity, size_t *size, int (*stop)(void *context),
	void *context, wiiu_http_result *result);

typedef struct wiiu_http_download_hooks
{
	void *context;
	int (*stop)(void *context);
	/* nonzero while the card may be written (the app in the foreground); a write is not begun otherwise */
	int (*may_write)(void *context);
	/* bytes of the file there so far, of its size */
	void (*progress)(void *context, uint32_t done, uint32_t size);
} wiiu_http_download_hooks;

enum
{
	WIIU_HTTP_DOWNLOADED = 1,  /* the .new is in place */
	WIIU_HTTP_INCOMPLETE = 0,  /* stopped or failed: the .part is kept, flushed, for the next attempt */
	WIIU_HTTP_DAMAGED = -1,    /* whole but not the manifest's: the .part is deleted */
	WIIU_HTTP_CARD_ERROR = -2, /* the card could not be written */
};

/* One file of a release downloaded from url to part_path, resumed from what part_path holds, checked, and renamed to new_path;
one of the WIIU_HTTP_* above. */
int wiiu_http_download(wiiu_http *http, const char *url, const char *part_path, const char *new_path, uint32_t size,
	const uint8_t sha256[32], const wiiu_http_download_hooks *hooks, wiiu_http_result *result);

/* 1 if the file at path is size bytes with that SHA-256 */
int wiiu_http_file_matches(const char *path, uint32_t size, const uint8_t sha256[32]);

/* the SHA-256 of bytes in memory */
void wiiu_http_sha256(const void *data, size_t size, uint8_t out[32]);

#ifdef __cplusplus
}
#endif

#endif
