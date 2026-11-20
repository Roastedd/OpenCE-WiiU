/*
WIIU_UPDATE_HTTP.C

The self-updater's HTTPS (wiiu_update_http.h): a GET at a time, each on a connection of its own (Connection: close), the
response read through a buffer of 16 KB. Built with the C library's ABI, as the posix_*.c are; the same source runs on a desktop
for the tests, with the system's sockets. Where nsysnet differs from them: a socket is made non-blocking with SO_NONBLOCK, and
a would-be-blocking call may say EBUSY.
*/

#include "wiiu_update_http.h"

#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h" /* (its error codes: its sockets are not the Wii U build's) */
#include "mbedtls/psa_util.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __WIIU__
#include <coreinit/thread.h>
#include <coreinit/time.h>
#else
#include <fcntl.h>
#include <time.h>
#endif

#define USER_AGENT "OpenCE-WiiU-updater"
#define MOST_REDIRECTS 5
#define CONNECT_MS 15000u
#define HANDSHAKE_MS 20000u
/* (no byte for this long: the transfer is given up, and a download tries again) */
#define IDLE_MS 20000u
#define WAIT_STEP_MS 100
/* (another request holds the TLS stack: waited for this long, then given up) */
#define LOCK_MS 30000u
/* (the longest extra header lines a request takes) */
#define EXTRA_HEADERS_MOST 1024u
#define LINE_MOST 8192u
#define HEADERS_MOST 32768u
#define URL_MOST 2048u
/* the card is written in pieces this large: fewer, larger writes are faster on the console's SD card */
#define WRITE_PIECE (64u * 1024u)

struct wiiu_http
{
	mbedtls_x509_crt roots;
	mbedtls_ssl_config config;
};

typedef struct connection
{
	int socket;
	int tls_ready;
	mbedtls_ssl_context ssl;
	const wiiu_http_hooks *hooks;
	wiiu_http_result *result;
	unsigned char buffer[16384];
	size_t start, end;
	int ended;
	/* the answer's header lines kept, if the caller asked (wiiu_http_request), and the time by which the whole request must be over (0: no limit) */
	char *capture;
	size_t capture_size, captured;
	uint64_t overall;
} connection;

/* Mbed TLS is built without its threading module: its random generator (PSA's) and its contexts have no lock, and the updater's worker, the server
browser's thread and the peer-to-peer DTLS may all want the TLS stack. One connection at a time is made and used here (all of it, from the lookup to
the close), and the cryptography is started under a lock of its own. */
static pthread_mutex_t request_lock = PTHREAD_MUTEX_INITIALIZER;

/* A console whose clock is far behind (a drained clock battery or a reset reads 2000 or 2013) refuses every certificate as "not yet valid", and the server
browser then finds no master. With this switched on (the server browser's retry only: the lists are signed with pinned keys, and the updater's manifest too,
so the lists do not rest on the transport's dates), a certificate that is only too early or too late is accepted while the clock reads before
WIIU_HTTP_CLOCK_FLOOR (1 January 2026, before this build); its chain to a root and the host's name are still checked. */
#define WIIU_HTTP_CLOCK_FLOOR 1767225600LL
static volatile int lenient_dates;

void wiiu_http_set_lenient_dates(int on)
{
	lenient_dates = on;
}

static int verify_dates_lenient(void *context, mbedtls_x509_crt *certificate, int depth, uint32_t *flags)
{
	(void)context;
	(void)certificate;
	(void)depth;
	if (lenient_dates && (long long)time(NULL) < WIIU_HTTP_CLOCK_FLOOR)
		*flags &= ~(uint32_t)(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
	return 0;
}
static pthread_mutex_t crypto_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- the clock, sleeping */

static uint64_t now_ms(void)
{
#ifdef __WIIU__
	return (uint64_t)OSTicksToMilliseconds(OSGetSystemTime());
#else
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static void sleep_ms(unsigned milliseconds)
{
#ifdef __WIIU__
	OSSleepTicks(OSMillisecondsToTicks(milliseconds));
#else
	struct timespec duration;

	duration.tv_sec = milliseconds / 1000u;
	duration.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
	nanosleep(&duration, NULL);
#endif
}

static void fail(wiiu_http_result *result, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void fail(wiiu_http_result *result, const char *format, ...)
{
	va_list arguments;

	/* (the first reason is the one that matters: what follows is the cleanup's) */
	if (result->error[0])
		return;
	va_start(arguments, format);
	vsnprintf(result->error, sizeof(result->error), format, arguments);
	va_end(arguments);
}

static void fail_tls(wiiu_http_result *result, const char *what, int code)
{
	char reason[120];

	mbedtls_strerror(code, reason, sizeof(reason));
	fail(result, "%s: %s (-0x%04x)", what, reason, (unsigned)-code);
}

/* ---------- the certificate authorities */

wiiu_http *wiiu_http_create(const unsigned char *const *roots, const size_t *sizes, unsigned count, char *error, size_t error_size)
{
	static int crypto_started;
	wiiu_http *http;
	unsigned i, parsed = 0;
	int result;

	pthread_mutex_lock(&crypto_lock);
	if (!crypto_started)
	{
		if (psa_crypto_init() != PSA_SUCCESS)
		{
			pthread_mutex_unlock(&crypto_lock);
			snprintf(error, error_size, "the cryptography could not start");
			return NULL;
		}
		crypto_started = 1;
	}
	pthread_mutex_unlock(&crypto_lock);
	http = calloc(1, sizeof(*http));
	if (!http)
	{
		snprintf(error, error_size, "no memory");
		return NULL;
	}
	mbedtls_x509_crt_init(&http->roots);
	mbedtls_ssl_config_init(&http->config);
	for (i = 0; i < count; i++)
		if (mbedtls_x509_crt_parse_der(&http->roots, roots[i], sizes[i]) == 0)
			parsed++;
	if (parsed != count || !count)
	{
		snprintf(error, error_size, "%u of the %u certificate authorities could not be read", count - parsed, count);
		wiiu_http_destroy(http);
		return NULL;
	}
	result = mbedtls_ssl_config_defaults(&http->config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	if (result != 0)
	{
		snprintf(error, error_size, "TLS could not be set up (-0x%04x)", (unsigned)-result);
		wiiu_http_destroy(http);
		return NULL;
	}
	/* (the certificate and the host's name checked, always) */
	mbedtls_ssl_conf_authmode(&http->config, MBEDTLS_SSL_VERIFY_REQUIRED);
	mbedtls_ssl_conf_ca_chain(&http->config, &http->roots, NULL);
	mbedtls_ssl_conf_verify(&http->config, verify_dates_lenient, NULL);
	mbedtls_ssl_conf_rng(&http->config, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
	return http;
}

void wiiu_http_destroy(wiiu_http *http)
{
	if (!http)
		return;
	mbedtls_ssl_config_free(&http->config);
	mbedtls_x509_crt_free(&http->roots);
	free(http);
}

/* ---------- the socket */

static int would_block(int error)
{
	return error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS || error == EALREADY || error == EBUSY || error == EINTR;
}

static int set_nonblocking(int socket)
{
#ifdef __WIIU__
	int on = 1;

	return setsockopt(socket, SOL_SOCKET, SO_NONBLOCK, &on, sizeof(on));
#else
	const int flags = fcntl(socket, F_GETFL, 0);

	return flags < 0 ? -1 : fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif
}

/* 1 when the socket is ready, 0 after a wait of up to 100 ms; -1 when the stop hook says so, -2 past the deadline, -3 on a failure
(in result) */
static int wait_socket(connection *c, int for_writing, uint64_t deadline, const char *what)
{
	struct pollfd descriptor;
	int ready;

	if (c->hooks && c->hooks->stop && c->hooks->stop(c->hooks->context))
	{
		c->result->stopped = 1;
		fail(c->result, "stopped");
		return -1;
	}
	if (c->overall && now_ms() >= c->overall)
	{
		fail(c->result, "the request took too long (%s, while %s)", c->result->host, what);
		return -2;
	}
	if (now_ms() >= deadline)
	{
		fail(c->result, "%s took too long (%s)", what, c->result->host);
		return -2;
	}
	descriptor.fd = c->socket;
	descriptor.events = for_writing ? POLLOUT : POLLIN;
	descriptor.revents = 0;
	ready = poll(&descriptor, 1, WAIT_STEP_MS);
	if (ready < 0)
	{
		if (errno == EINTR)
			return 0;
		fail(c->result, "%s failed: the network went away (errno %d)", what, errno);
		return -3;
	}
	return ready > 0;
}

static int open_socket(connection *c, const char *host, unsigned port)
{
	struct addrinfo hints, *found = NULL, *at;
	uint64_t deadline;
	int result = -1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, NULL, &hints, &found) != 0 || !found)
	{
		fail(c->result, "the name %s could not be looked up", host);
		return -1;
	}
	deadline = now_ms() + CONNECT_MS;
	for (at = found; at && result != 0; at = at->ai_next)
	{
		struct sockaddr_in address;
		int connected, error = 0;
		socklen_t length = sizeof(error);

		if (at->ai_family != AF_INET || at->ai_addrlen < sizeof(address))
			continue;
		memcpy(&address, at->ai_addr, sizeof(address));
		address.sin_port = htons((unsigned short)port);
		c->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (c->socket < 0)
		{
			fail(c->result, "no socket (errno %d)", errno);
			break;
		}
#ifdef SO_NOSIGPIPE
		{
			int on = 1;

			setsockopt(c->socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
		}
#endif
		if (set_nonblocking(c->socket) != 0)
		{
			fail(c->result, "the socket could not be made non-blocking (errno %d)", errno);
			break;
		}
		connected = connect(c->socket, (struct sockaddr *)&address, sizeof(address));
		if (connected != 0 && !would_block(errno))
		{
			fail(c->result, "%s did not answer (errno %d)", host, errno);
			close(c->socket);
			c->socket = -1;
			continue;
		}
		while (connected != 0)
		{
			const int ready = wait_socket(c, 1, deadline, "connecting");

			if (ready < 0)
				break;
			if (ready > 0)
			{
				if (getsockopt(c->socket, SOL_SOCKET, SO_ERROR, &error, &length) != 0)
					error = errno;
				connected = error == 0 ? 0 : -1;
				break;
			}
		}
		if (connected == 0)
		{
			result = 0;
			break;
		}
		if (c->result->stopped)
			break;
		if (error)
			fail(c->result, "%s refused the connection (errno %d)", host, error);
		close(c->socket);
		c->socket = -1;
		if (now_ms() >= deadline)
			break;
	}
	freeaddrinfo(found);
	if (result != 0 && c->socket >= 0)
	{
		close(c->socket);
		c->socket = -1;
	}
	return result;
}

/* ---------- TLS over it */

static int bio_send(void *context, const unsigned char *data, size_t size)
{
	connection *c = context;
#ifdef MSG_NOSIGNAL
	const ssize_t sent = send(c->socket, data, size, MSG_NOSIGNAL);
#else
	const ssize_t sent = send(c->socket, data, size, 0);
#endif

	if (sent >= 0)
		return (int)sent;
	if (would_block(errno))
		return MBEDTLS_ERR_SSL_WANT_WRITE;
	return errno == EPIPE || errno == ECONNRESET ? MBEDTLS_ERR_NET_CONN_RESET : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bio_receive(void *context, unsigned char *data, size_t size)
{
	connection *c = context;
	const ssize_t got = recv(c->socket, data, size, 0);

	if (got >= 0)
		return (int)got;
	if (would_block(errno))
		return MBEDTLS_ERR_SSL_WANT_READ;
	return errno == ECONNRESET ? MBEDTLS_ERR_NET_CONN_RESET : MBEDTLS_ERR_NET_RECV_FAILED;
}

static int tls_open(wiiu_http *http, connection *c, const char *host)
{
	const uint64_t deadline = now_ms() + HANDSHAKE_MS;
	int result;

	mbedtls_ssl_init(&c->ssl);
	c->tls_ready = 1;
	if ((result = mbedtls_ssl_setup(&c->ssl, &http->config)) != 0 || (result = mbedtls_ssl_set_hostname(&c->ssl, host)) != 0)
	{
		fail_tls(c->result, "TLS could not be set up", result);
		return -1;
	}
	mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_receive, NULL);
	for (;;)
	{
		result = mbedtls_ssl_handshake(&c->ssl);
		if (result == 0)
			return 0;
		if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			if (wait_socket(c, result == MBEDTLS_ERR_SSL_WANT_WRITE, deadline, "the TLS handshake") < 0)
				return -1;
			continue;
		}
		{
			const uint32_t flags = mbedtls_ssl_get_verify_result(&c->ssl);

			if (flags && flags != (uint32_t)-1)
			{
				char reason[200];

				mbedtls_x509_crt_verify_info(reason, sizeof(reason), "", flags);
				reason[strcspn(reason, "\n")] = 0;
				fail(c->result, "the certificate of %s was refused: %s", host, reason);
			}
			else
				fail_tls(c->result, "the TLS handshake failed", result);
		}
		return -1;
	}
}

static int tls_write(connection *c, const unsigned char *data, size_t size)
{
	const uint64_t deadline = now_ms() + IDLE_MS;

	while (size)
	{
		const int result = mbedtls_ssl_write(&c->ssl, data, size);

		if (result > 0)
		{
			data += result;
			size -= (size_t)result;
			continue;
		}
		if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			if (wait_socket(c, result == MBEDTLS_ERR_SSL_WANT_WRITE, deadline, "sending") < 0)
				return -1;
			continue;
		}
		fail_tls(c->result, "sending failed", result);
		return -1;
	}
	return 0;
}

/* more of the response into the buffer: 1, 0 at its end (the server closed), -1 on a failure (in result) */
static int fill(connection *c)
{
	const uint64_t deadline = now_ms() + IDLE_MS;

	if (c->ended)
		return 0;
	if (c->start == c->end)
		c->start = c->end = 0;
	if (c->end == sizeof(c->buffer))
	{
		memmove(c->buffer, c->buffer + c->start, c->end - c->start);
		c->end -= c->start;
		c->start = 0;
		if (c->end == sizeof(c->buffer))
		{
			fail(c->result, "a line of %s's answer is too long", c->result->host);
			return -1;
		}
	}
	for (;;)
	{
		const int result = mbedtls_ssl_read(&c->ssl, c->buffer + c->end, sizeof(c->buffer) - c->end);

		if (result > 0)
		{
			c->end += (size_t)result;
			return 1;
		}
		/* (a close without TLS's close notification is still the end: a body's length and the manifest's hashes catch one cut short) */
		if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || result == MBEDTLS_ERR_SSL_CONN_EOF)
		{
			c->ended = 1;
			return 0;
		}
		if (result == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
			continue;
		if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			if (wait_socket(c, result == MBEDTLS_ERR_SSL_WANT_WRITE, deadline, "waiting for the answer") < 0)
				return -1;
			continue;
		}
		if (result == MBEDTLS_ERR_NET_CONN_RESET)
		{
			fail(c->result, "%s broke the connection", c->result->host);
			return -1;
		}
		fail_tls(c->result, "receiving failed", result);
		return -1;
	}
}

/* a line of the answer without its CR LF: 1; 0 if the answer ended first; -1 if it is longer than capacity, has a control
character, or the connection failed */
static int read_line(connection *c, char *line, size_t capacity)
{
	for (;;)
	{
		const unsigned char *begin = c->buffer + c->start;
		const unsigned char *newline = memchr(begin, '\n', c->end - c->start);

		if (newline)
		{
			size_t length = (size_t)(newline - begin), i;

			if (length && begin[length - 1] == '\r')
				length--;
			if (length >= capacity)
			{
				fail(c->result, "a line of %s's answer is too long", c->result->host);
				return -1;
			}
			for (i = 0; i < length; i++)
			{
				if (begin[i] < 0x20 && begin[i] != '\t')
				{
					fail(c->result, "%s's answer has a control character in its headers", c->result->host);
					return -1;
				}
			}
			memcpy(line, begin, length);
			line[length] = 0;
			c->start += (size_t)(newline - begin) + 1;
			return 1;
		}
		if (c->end - c->start >= capacity)
		{
			fail(c->result, "a line of %s's answer is too long", c->result->host);
			return -1;
		}
		{
			const int more = fill(c);

			if (more <= 0)
				return more;
		}
	}
}

static void connection_close(connection *c)
{
	if (c->tls_ready)
	{
		/* (one try at telling the server: nothing waits for it) */
		mbedtls_ssl_close_notify(&c->ssl);
		mbedtls_ssl_free(&c->ssl);
		c->tls_ready = 0;
	}
	if (c->socket >= 0)
	{
		close(c->socket);
		c->socket = -1;
	}
}

/* ---------- HTTP */

/* https://host[:port][/path]: 0 and its parts, -1 if it is not such an address */
static int parse_url(const char *url, char *host, size_t host_size, unsigned *port, char *path, size_t path_size)
{
	const char *start, *end, *colon, *at;
	size_t host_length, i;

	if (strncasecmp(url, "https://", 8) != 0 || strlen(url) >= URL_MOST)
		return -1;
	for (i = 0; url[i]; i++)
		if ((unsigned char)url[i] <= 0x20 || (unsigned char)url[i] >= 0x7F)
			return -1;
	start = url + 8;
	end = start + strcspn(start, "/?#");
	at = memchr(start, '@', (size_t)(end - start));
	if (at)
		return -1;
	colon = memchr(start, ':', (size_t)(end - start));
	host_length = (size_t)((colon ? colon : end) - start);
	if (!host_length || host_length >= host_size)
		return -1;
	for (i = 0; i < host_length; i++)
	{
		const char c = start[i];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
			return -1;
	}
	memcpy(host, start, host_length);
	host[host_length] = 0;
	*port = 443;
	if (colon)
	{
		const char *digit;
		unsigned value = 0;

		if (colon + 1 == end || end - colon > 6)
			return -1;
		for (digit = colon + 1; digit < end; digit++)
		{
			if (*digit < '0' || *digit > '9')
				return -1;
			value = value * 10u + (unsigned)(*digit - '0');
		}
		if (!value || value > 65535u)
			return -1;
		*port = value;
	}
	{
		/* (the fragment is not sent) */
		const size_t path_length = strcspn(end, "#");

		if (!path_length)
		{
			snprintf(path, path_size, "/");
			return 0;
		}
		if (path_length >= path_size)
			return -1;
		if (*end == '/')
			snprintf(path, path_size, "%.*s", (int)path_length, end);
		else
			snprintf(path, path_size, "/%.*s", (int)path_length, end);
	}
	return 0;
}

static int header_is(const char *line, const char *name, const char **value)
{
	const size_t length = strlen(name);

	if (strncasecmp(line, name, length) != 0 || line[length] != ':')
		return 0;
	*value = line + length + 1;
	*value += strspn(*value, " \t");
	return 1;
}

/* a decimal number of a header (digits only, and no more than 2^53) */
static int header_number(const char *text, uint64_t *value, const char **after)
{
	uint64_t result = 0;
	const char *digit = text;

	if (*digit < '0' || *digit > '9')
		return -1;
	while (*digit >= '0' && *digit <= '9')
	{
		if (result > (UINT64_C(1) << 53) / 10u)
			return -1;
		result = result * 10u + (uint64_t)(*digit - '0');
		digit++;
	}
	*value = result;
	if (after)
		*after = digit;
	else if (*digit && *digit != ' ' && *digit != '\t')
		return -1;
	return 0;
}

typedef struct response
{
	int status;
	int chunked;
	int have_length;
	uint64_t length;
	int have_range;
	uint64_t range_first, range_last, range_total;
	char location[URL_MOST];
} response;

static int read_headers(connection *c, response *answer)
{
	char line[LINE_MOST] = "";
	size_t total = 0;
	int got;

	memset(answer, 0, sizeof(*answer));
	got = read_line(c, line, sizeof(line));
	if (got <= 0)
	{
		fail(c->result, "%s did not answer", c->result->host);
		return -1;
	}
	if (strlen(line) < 12 || strncmp(line, "HTTP/1.", 7) != 0 || (line[7] != '0' && line[7] != '1') || line[8] != ' ' || line[9] < '1' || line[9] > '5' ||
		line[10] < '0' || line[10] > '9' || line[11] < '0' || line[11] > '9' || (line[12] && line[12] != ' '))
	{
		fail(c->result, "%s's answer is not HTTP/1", c->result->host);
		return -1;
	}
	answer->status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + (line[11] - '0');
	for (;;)
	{
		const char *value;

		got = read_line(c, line, sizeof(line));
		if (got <= 0)
		{
			fail(c->result, "%s's answer ended in its headers", c->result->host);
			return -1;
		}
		total += strlen(line) + 2;
		if (total > HEADERS_MOST)
		{
			fail(c->result, "%s's headers are longer than %u bytes", c->result->host, (unsigned)HEADERS_MOST);
			return -1;
		}
		if (!line[0])
			return 0;
		if (c->capture)
		{
			/* ("Name: value" and a newline; a line that does not fit is left out, as the desktop's does) */
			const size_t length = strlen(line);

			if (c->captured + length + 2 < c->capture_size)
			{
				memcpy(c->capture + c->captured, line, length);
				c->captured += length;
				c->capture[c->captured++] = '\n';
				c->capture[c->captured] = 0;
			}
		}
		if (header_is(line, "Content-Length", &value))
		{
			uint64_t length;

			if (header_number(value, &length, NULL) != 0 || (answer->have_length && length != answer->length))
			{
				fail(c->result, "%s's Content-Length is not a number", c->result->host);
				return -1;
			}
			answer->length = length;
			answer->have_length = 1;
		}
		else if (header_is(line, "Transfer-Encoding", &value))
		{
			if (strncasecmp(value, "chunked", 7) != 0 || (value[7] && value[7] != ' ' && value[7] != '\t'))
			{
				fail(c->result, "%s sends the body in a coding this client does not read (%.40s)", c->result->host, value);
				return -1;
			}
			answer->chunked = 1;
		}
		else if (header_is(line, "Location", &value))
		{
			if (strlen(value) >= sizeof(answer->location))
			{
				fail(c->result, "%s's redirect is too long", c->result->host);
				return -1;
			}
			snprintf(answer->location, sizeof(answer->location), "%s", value);
			answer->location[strcspn(answer->location, " \t")] = 0;
		}
		else if (header_is(line, "Content-Range", &value))
		{
			const char *at;

			/* bytes FIRST-LAST/TOTAL, or FIRST-LAST and a star for a total not known */
			if (strncasecmp(value, "bytes ", 6) != 0 || header_number(value + 6, &answer->range_first, &at) != 0 || *at != '-' ||
				header_number(at + 1, &answer->range_last, &at) != 0 || *at != '/' || answer->range_last < answer->range_first)
			{
				fail(c->result, "%s's Content-Range cannot be read", c->result->host);
				return -1;
			}
			if (at[1] == '*' && !at[2])
				answer->range_total = 0;
			else if (header_number(at + 1, &answer->range_total, NULL) != 0 || answer->range_total <= answer->range_last)
			{
				fail(c->result, "%s's Content-Range cannot be read", c->result->host);
				return -1;
			}
			answer->have_range = 1;
		}
	}
}

/* up to size bytes of the body: what the buffer holds first */
static int read_some(connection *c, unsigned char **data, size_t most)
{
	size_t available;

	if (c->start == c->end)
	{
		const int more = fill(c);

		if (more <= 0)
			return more;
	}
	available = c->end - c->start;
	if (available > most)
		available = most;
	*data = c->buffer + c->start;
	c->start += available;
	return (int)available;
}

static int give(connection *c, const unsigned char *data, size_t size, uint64_t most)
{
	/* (asked between pieces too: a fast connection may never wait, and HOME or the close must still end it at once) */
	if (c->hooks && c->hooks->stop && c->hooks->stop(c->hooks->context))
	{
		c->result->stopped = 1;
		fail(c->result, "stopped");
		return -1;
	}
	if (c->overall && now_ms() >= c->overall)
	{
		fail(c->result, "the request took too long (%s, while its body arrived)", c->result->host);
		return -1;
	}
	if (c->result->received + size > most)
	{
		fail(c->result, "%s sends more than the %llu bytes asked for", c->result->host, (unsigned long long)most);
		return -1;
	}
	if (c->hooks && c->hooks->body && !c->hooks->body(c->hooks->context, data, size))
	{
		/* (the hook stopped it: its reason is the caller's, or the stop hook's) */
		if (c->hooks->stop && c->hooks->stop(c->hooks->context))
			c->result->stopped = 1;
		fail(c->result, "the transfer was stopped");
		return -1;
	}
	c->result->received += size;
	return 0;
}

static int read_body(connection *c, const response *answer, uint64_t most)
{
	if (answer->chunked)
	{
		for (;;)
		{
			char line[256];
			uint64_t remaining = 0;
			size_t i;

			if (read_line(c, line, sizeof(line)) <= 0)
			{
				fail(c->result, "%s's body broke off", c->result->host);
				return -1;
			}
			/* a size in hexadecimal, then perhaps ";extensions" */
			for (i = 0; line[i] && line[i] != ';' && line[i] != ' ' && line[i] != '\t'; i++)
			{
				const char d = line[i];
				const int digit = d >= '0' && d <= '9' ? d - '0' : d >= 'a' && d <= 'f' ? d - 'a' + 10 : d >= 'A' && d <= 'F' ? d - 'A' + 10 : -1;

				if (digit < 0 || i >= 8)
				{
					fail(c->result, "%s's chunk size cannot be read", c->result->host);
					return -1;
				}
				remaining = remaining * 16u + (uint64_t)digit;
			}
			if (!i)
			{
				fail(c->result, "%s's chunk size cannot be read", c->result->host);
				return -1;
			}
			if (!remaining)
			{
				/* (the trailer's lines, up to the empty one) */
				do
				{
					if (read_line(c, line, sizeof(line)) <= 0)
					{
						fail(c->result, "%s's body broke off", c->result->host);
						return -1;
					}
				} while (line[0]);
				return 0;
			}
			while (remaining)
			{
				unsigned char *data;
				const int count = read_some(c, &data, remaining < 16384u ? (size_t)remaining : 16384u);

				if (count <= 0)
				{
					fail(c->result, "%s's body broke off", c->result->host);
					return -1;
				}
				if (give(c, data, (size_t)count, most) != 0)
					return -1;
				remaining -= (uint64_t)count;
			}
			if (read_line(c, line, sizeof(line)) <= 0 || line[0])
			{
				fail(c->result, "%s's chunk does not end where it says", c->result->host);
				return -1;
			}
		}
	}
	if (answer->have_length && answer->length > most)
	{
		fail(c->result, "%s's body is %llu bytes, more than the %llu asked for", c->result->host, (unsigned long long)answer->length,
			(unsigned long long)most);
		return -1;
	}
	for (;;)
	{
		unsigned char *data;
		size_t wanted = 16384u;
		int count;

		if (answer->have_length)
		{
			const uint64_t left = answer->length - c->result->received;

			if (!left)
				return 0;
			if (left < wanted)
				wanted = (size_t)left;
		}
		count = read_some(c, &data, wanted);
		if (count < 0)
			return -1;
		if (count == 0)
		{
			if (!answer->have_length)
				return 0;
			fail(c->result, "%s's body broke off after %llu of %llu bytes", c->result->host, (unsigned long long)c->result->received,
				(unsigned long long)answer->length);
			return -1;
		}
		if (give(c, data, (size_t)count, most) != 0)
			return -1;
	}
}

static int is_redirect(int status)
{
	return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

/* extra request header lines: printable text in lines each ended by CR LF, no longer than EXTRA_HEADERS_MOST */
static int extra_headers_are_valid(const char *headers)
{
	size_t i, line_start = 0;

	if (!headers || !headers[0])
		return 1;
	if (strlen(headers) > EXTRA_HEADERS_MOST)
		return 0;
	for (i = 0; headers[i]; i++)
	{
		const unsigned char d = (unsigned char)headers[i];

		if (d == '\r')
		{
			/* (a line of at least one character, ended by CR LF) */
			if (headers[i + 1] != '\n' || i == line_start)
				return 0;
			i++;
			line_start = i + 1;
		}
		else if ((d < 0x20 && d != '\t') || d >= 0x7F)
			return 0;
	}
	return line_start == i;
}

/* Take the TLS stack's lock, waiting (and asking the stop hook, and the request's own time) for another connection to end: 0, or -1 with the reason in
result */
static int lock_stack(connection *c)
{
	const uint64_t deadline = now_ms() + LOCK_MS;

	while (pthread_mutex_trylock(&request_lock) != 0)
	{
		if (c->hooks && c->hooks->stop && c->hooks->stop(c->hooks->context))
		{
			c->result->stopped = 1;
			fail(c->result, "stopped");
			return -1;
		}
		if ((c->overall && now_ms() >= c->overall) || now_ms() >= deadline)
		{
			fail(c->result, "another HTTPS request (the updater's, or an earlier list) did not finish in time");
			return -1;
		}
		sleep_ms(20);
	}
	return 0;
}

/* One request on its own connection: the status, the Location in location; -1 on a failure. With options (wiiu_http_request): a request of the
caller's, whose answer is the status, the headers and the body whatever the status, a redirect not followed */
static int one_get(wiiu_http *http, const char *host, unsigned port, const char *path, uint64_t from, uint64_t most,
	const wiiu_http_hooks *hooks, const wiiu_http_request_options *options, wiiu_http_result *result, char *location, size_t location_size)
{
	connection *c = calloc(1, sizeof(connection));
	char request[URL_MOST + 512 + EXTRA_HEADERS_MOST];
	char host_header[160];
	char range[64] = "";
	response answer;
	int status = -1, locked = 0;

	if (!c)
	{
		fail(result, "no memory");
		return -1;
	}
	c->socket = -1;
	c->hooks = hooks;
	c->result = result;
	location[0] = 0;
	snprintf(result->host, sizeof(result->host), "%s", host);
	if (options)
	{
		c->capture = options->response_headers;
		c->capture_size = options->response_headers ? options->response_headers_size : 0;
		if (c->capture && c->capture_size)
			c->capture[0] = 0;
		else
			c->capture = NULL;
		if (options->total_ms)
			c->overall = now_ms() + options->total_ms;
		if (!extra_headers_are_valid(options->headers))
		{
			fail(result, "the request's header lines are not valid");
			goto done;
		}
	}
	if (port == 443)
		snprintf(host_header, sizeof(host_header), "%s", host);
	else
		snprintf(host_header, sizeof(host_header), "%s:%u", host, port);
	if (from)
		snprintf(range, sizeof(range), "Range: bytes=%llu-\r\n", (unsigned long long)from);
	snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " USER_AGENT "\r\nAccept: */*\r\n%s%sConnection: close\r\n\r\n", path, host_header, range,
		options && options->headers ? options->headers : "");
	if (lock_stack(c) != 0)
		goto done;
	locked = 1;
	if (open_socket(c, host, port) != 0 || tls_open(http, c, host) != 0 ||
		tls_write(c, (const unsigned char *)request, strlen(request)) != 0 || read_headers(c, &answer) != 0)
	{
		goto done;
	}
	status = answer.status;
	if (options)
	{
		/* (any answer with a final status is the caller's: it has a body unless the status says it has none) */
		if (status < 200)
		{
			fail(result, "%s answered with an interim status (%d)", host, status);
			status = -1;
			goto done;
		}
		result->status = status;
		if (hooks && hooks->start && !hooks->start(hooks->context, status, 0, answer.have_length && !answer.chunked ? answer.length : 0))
		{
			fail(result, "the answer of %s was refused (%d)", host, status);
			status = -1;
			goto done;
		}
		if (status != 204 && status != 304 && read_body(c, &answer, most) != 0)
			status = -1;
		goto done;
	}
	if (is_redirect(status))
	{
		snprintf(location, location_size, "%s", answer.location);
		goto done;
	}
	if (status != 200 && status != 206)
		goto done;
	result->status = status;
	{
		uint64_t offset = 0, total = answer.have_length && !answer.chunked ? answer.length : 0;

		if (status == 206)
		{
			/* (a range is only taken where it was asked for) */
			if (!from || !answer.have_range || answer.range_first != from)
			{
				fail(result, "%s answered with a range that does not start at byte %llu", host, (unsigned long long)from);
				status = -1;
				goto done;
			}
			offset = answer.range_first;
			total = answer.range_total;
		}
		if (hooks && hooks->start && !hooks->start(hooks->context, status, offset, total))
		{
			fail(result, "the answer of %s was refused (%d from byte %llu of %llu)", host, status, (unsigned long long)offset,
				(unsigned long long)total);
			status = -1;
			goto done;
		}
	}
	if (read_body(c, &answer, most) != 0)
		status = -1;

done:
	connection_close(c);
	if (locked)
		pthread_mutex_unlock(&request_lock);
	free(c);
	return status;
}

int wiiu_http_get(wiiu_http *http, const char *url, uint64_t from, uint64_t most, const wiiu_http_hooks *hooks, wiiu_http_result *result)
{
	char current[URL_MOST], location[URL_MOST];
	int hop;

	memset(result, 0, sizeof(*result));
	if (strlen(url) >= sizeof(current))
	{
		fail(result, "the address is too long");
		return -1;
	}
	snprintf(current, sizeof(current), "%s", url);
	for (hop = 0; hop <= MOST_REDIRECTS; hop++)
	{
		char host[128], path[URL_MOST];
		unsigned port;
		int status;

		if (parse_url(current, host, sizeof(host), &port, path, sizeof(path)) != 0)
		{
			fail(result, "not an https:// address: %.80s", current);
			return -1;
		}
		status = one_get(http, host, port, path, from, most, hooks, NULL, result, location, sizeof(location));
		if (status < 0)
			return -1;
		if (!is_redirect(status))
		{
			result->status = status;
			return 0;
		}
		if (!location[0])
		{
			fail(result, "%s redirects with no address", host);
			return -1;
		}
		result->redirects++;
		if (location[0] == '/')
		{
			char next[URL_MOST];
			int written;

			if (location[1] == '/')
			{
				fail(result, "%s redirects to a network-path address", host);
				return -1;
			}
			written = port == 443 ? snprintf(next, sizeof(next), "https://%s%s", host, location)
			                      : snprintf(next, sizeof(next), "https://%s:%u%s", host, port, location);
			if (written < 0 || (size_t)written >= sizeof(next))
			{
				fail(result, "%s's redirect is too long", host);
				return -1;
			}
			memcpy(current, next, (size_t)written + 1);
		}
		else
			snprintf(current, sizeof(current), "%s", location);
	}
	fail(result, "more than %d redirects", MOST_REDIRECTS);
	return -1;
}

int wiiu_http_request(wiiu_http *http, const char *url, uint64_t most, const wiiu_http_hooks *hooks, const wiiu_http_request_options *options,
	wiiu_http_result *result)
{
	char host[128], path[URL_MOST], location[URL_MOST];
	unsigned port;
	int status;

	memset(result, 0, sizeof(*result));
	if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0)
	{
		fail(result, "not an https:// address: %.80s", url);
		return -1;
	}
	status = one_get(http, host, port, path, 0, most, hooks, options, result, location, sizeof(location));
	if (status < 0)
		return -1;
	result->status = status;
	return 0;
}

/* ---------- a small file into memory */

typedef struct memory_sink
{
	unsigned char *buffer;
	size_t capacity, size;
	int (*stop)(void *context);
	void *context;
} memory_sink;

static int memory_stop(void *context)
{
	memory_sink *sink = context;

	return sink->stop && sink->stop(sink->context);
}

static int memory_start(void *context, int status, uint64_t offset, uint64_t total)
{
	memory_sink *sink = context;

	(void)total;
	sink->size = 0;
	return status == 200 && offset == 0;
}

static int memory_body(void *context, const unsigned char *data, size_t size)
{
	memory_sink *sink = context;

	if (size > sink->capacity - sink->size)
		return 0;
	memcpy(sink->buffer + sink->size, data, size);
	sink->size += size;
	return 1;
}

int wiiu_http_fetch(wiiu_http *http, const char *url, void *buffer, size_t capacity, size_t *size, int (*stop)(void *context),
	void *context, wiiu_http_result *result)
{
	memory_sink sink;
	wiiu_http_hooks hooks;

	sink.buffer = buffer;
	sink.capacity = capacity;
	sink.size = 0;
	sink.stop = stop;
	sink.context = context;
	hooks.context = &sink;
	hooks.stop = memory_stop;
	hooks.start = memory_start;
	hooks.body = memory_body;
	if (wiiu_http_get(http, url, 0, capacity, &hooks, result) != 0)
		return -1;
	if (result->status != 200)
	{
		fail(result, "%s answered %d", result->host, result->status);
		return -1;
	}
	*size = sink.size;
	return 0;
}

/* ---------- a file of the release */

void wiiu_http_sha256(const void *data, size_t size, uint8_t out[32])
{
	mbedtls_sha256_context sha;

	mbedtls_sha256_init(&sha);
	mbedtls_sha256_starts(&sha, 0);
	mbedtls_sha256_update(&sha, data, size);
	mbedtls_sha256_finish(&sha, out);
	mbedtls_sha256_free(&sha);
}

/* the SHA-256 of a file's first `bytes` bytes, into sha (started by the caller); 0, -1 if they cannot be read, -2 if the stop hook
(asked between pieces) ends it */
static int hash_file(const char *path, uint64_t bytes, mbedtls_sha256_context *sha, int (*stop)(void *context), void *context)
{
	static unsigned char piece[WRITE_PIECE];
	FILE *file = fopen(path, "rb");
	uint64_t left = bytes;

	if (!file)
		return -1;
	while (left)
	{
		const size_t wanted = left < sizeof(piece) ? (size_t)left : sizeof(piece);

		if (stop && stop(context))
		{
			fclose(file);
			return -2;
		}
		if (fread(piece, 1, wanted, file) != wanted)
		{
			fclose(file);
			return -1;
		}
		mbedtls_sha256_update(sha, piece, wanted);
		left -= wanted;
	}
	fclose(file);
	return 0;
}

static long long file_size(const char *path)
{
	struct stat information;

	return stat(path, &information) == 0 && S_ISREG(information.st_mode) ? (long long)information.st_size : -1;
}

int wiiu_http_file_matches(const char *path, uint32_t size, const uint8_t sha256[32], int (*stop)(void *context), void *context)
{
	mbedtls_sha256_context sha;
	uint8_t digest[32];
	int read;

	if (file_size(path) != (long long)size)
		return 0;
	mbedtls_sha256_init(&sha);
	mbedtls_sha256_starts(&sha, 0);
	read = hash_file(path, size, &sha, stop, context);
	mbedtls_sha256_finish(&sha, digest);
	mbedtls_sha256_free(&sha);
	if (read == -2)
		return -1;
	return read == 0 && !memcmp(digest, sha256, sizeof(digest));
}

typedef struct download
{
	FILE *file;
	const char *part_path;
	uint32_t size;
	/* on the card (and hashed), and received but not yet written: what is not written when a transfer stops is asked for again */
	uint32_t written;
	unsigned char *pending;
	uint32_t pending_size;
	mbedtls_sha256_context sha;
	const wiiu_http_download_hooks *hooks;
	int paused;
	int card_error;
	int restarted;
} download;

static int download_may_continue(download *d)
{
	return !d->hooks->may_write || d->hooks->may_write(d->hooks->context);
}

static int download_stop(void *context)
{
	download *d = context;

	if (d->hooks->stop && d->hooks->stop(d->hooks->context))
		return 1;
	if (!download_may_continue(d))
	{
		d->paused = 1;
		return 1;
	}
	return 0;
}

/* what has arrived onto the card, if it may be written now */
static int download_write_pending(download *d)
{
	if (!d->pending_size)
		return 0;
	if (!download_may_continue(d))
	{
		/* (dropped, not written: asked for again when the transfer resumes) */
		d->pending_size = 0;
		d->paused = 1;
		return -1;
	}
	if (fwrite(d->pending, 1, d->pending_size, d->file) != d->pending_size)
	{
		d->card_error = 1;
		d->pending_size = 0;
		return -1;
	}
	mbedtls_sha256_update(&d->sha, d->pending, d->pending_size);
	d->written += d->pending_size;
	d->pending_size = 0;
	if (d->hooks->progress)
		d->hooks->progress(d->hooks->context, d->written, d->size);
	return 0;
}

static int download_start(void *context, int status, uint64_t offset, uint64_t total)
{
	download *d = context;

	d->pending_size = 0;
	if (total && total != d->size)
		return 0;
	if (status == 206)
		return offset == d->written;
	if (status != 200 || offset != 0)
		return 0;
	if (d->written)
	{
		/* the server ignored the range: the file begins again */
		fclose(d->file);
		d->file = fopen(d->part_path, "wb");
		if (!d->file)
		{
			d->card_error = 1;
			return 0;
		}
		setvbuf(d->file, NULL, _IONBF, 0);
		mbedtls_sha256_free(&d->sha);
		mbedtls_sha256_init(&d->sha);
		mbedtls_sha256_starts(&d->sha, 0);
		d->written = 0;
		d->restarted = 1;
	}
	return 1;
}

static int download_body(void *context, const unsigned char *data, size_t size)
{
	download *d = context;

	if ((uint64_t)d->written + d->pending_size + size > d->size)
		return 0;
	while (size)
	{
		const size_t room = WRITE_PIECE - d->pending_size;
		const size_t taken = size < room ? size : room;

		memcpy(d->pending + d->pending_size, data, taken);
		d->pending_size += (uint32_t)taken;
		data += taken;
		size -= taken;
		if (d->pending_size == WRITE_PIECE && download_write_pending(d) != 0)
			return 0;
	}
	return 1;
}

static int download_finish(download *d, int keep_part)
{
	int result = 0;

	if (d->file)
	{
		if (fflush(d->file) != 0 || (keep_part && fsync(fileno(d->file)) != 0))
			result = -1;
		if (fclose(d->file) != 0)
			result = -1;
		d->file = NULL;
	}
	return result;
}

int wiiu_http_download(wiiu_http *http, const char *url, const char *part_path, const char *new_path, uint32_t size,
	const uint8_t sha256[32], const wiiu_http_download_hooks *hooks, wiiu_http_result *result)
{
	/* (one download at a time: the updater's worker's) */
	static unsigned char pending[WRITE_PIECE];
	download d;
	wiiu_http_hooks get_hooks;
	uint8_t digest[32];
	unsigned failures = 0;
	long long have;

	memset(&d, 0, sizeof(d));
	memset(result, 0, sizeof(*result));
	d.part_path = part_path;
	d.size = size;
	d.hooks = hooks;
	mbedtls_sha256_init(&d.sha);
	mbedtls_sha256_starts(&d.sha, 0);
	/* what an earlier attempt left, hashed again (a part longer than the file is no part of it) */
	have = file_size(part_path);
	if (have > (long long)size)
	{
		remove(part_path);
		have = -1;
	}
	if (have > 0)
	{
		const int read = hash_file(part_path, (uint64_t)have, &d.sha, hooks->stop, hooks->context);

		if (read == -2)
		{
			/* (the app closing, or the player stopping it, while the part was read again: it is kept as it is) */
			mbedtls_sha256_free(&d.sha);
			result->stopped = 1;
			fail(result, "stopped");
			return WIIU_HTTP_INCOMPLETE;
		}
		if (read != 0)
		{
			mbedtls_sha256_free(&d.sha);
			mbedtls_sha256_init(&d.sha);
			mbedtls_sha256_starts(&d.sha, 0);
			remove(part_path);
			have = -1;
		}
	}
	d.written = have > 0 ? (uint32_t)have : 0;
	d.pending = pending;
	d.file = fopen(part_path, "ab");
	if (!d.file)
	{
		fail(result, "%s cannot be written", part_path);
		mbedtls_sha256_free(&d.sha);
		return WIIU_HTTP_CARD_ERROR;
	}
	/* (no buffer of stdio's: what is written goes to the card in this unit's pieces, and nothing is left to write once a transfer stops) */
	setvbuf(d.file, NULL, _IONBF, 0);
	if (hooks->progress)
		hooks->progress(hooks->context, d.written, size);
	get_hooks.context = &d;
	get_hooks.stop = download_stop;
	get_hooks.start = download_start;
	get_hooks.body = download_body;
	while (d.written < size)
	{
		if (hooks->stop && hooks->stop(hooks->context))
		{
			result->stopped = 1;
			fail(result, "stopped");
			break;
		}
		if (!download_may_continue(&d))
		{
			/* HOME, or the menus gone: no socket and no card until it is over; then the address is asked for again (GitHub's links to its
			file host expire) with the range from here */
			sleep_ms(50);
			continue;
		}
		d.paused = 0;
		{
			wiiu_http_result attempt;
			const int got = wiiu_http_get(http, url, d.written, size, &get_hooks, &attempt);

			/* (what arrived since the last piece, if the transfer ended well or on a pause the card may still take) */
			if (!d.card_error)
				download_write_pending(&d);
			d.pending_size = 0;
			if (d.card_error)
			{
				fail(result, "%s could not be written", part_path);
				break;
			}
			if (d.written == size)
			{
				*result = attempt;
				break;
			}
			if (d.paused || (attempt.stopped && !(hooks->stop && hooks->stop(hooks->context))))
				continue;
			if (attempt.stopped)
			{
				*result = attempt;
				result->stopped = 1;
				break;
			}
			if (got == 0 && attempt.status == 416)
			{
				/* (a part the server's file does not have: it is not that file's beginning) */
				download_finish(&d, 0);
				remove(part_path);
				*result = attempt;
				fail(result, "%s does not have byte %u of the file", attempt.host, (unsigned)d.written);
				mbedtls_sha256_free(&d.sha);
				return WIIU_HTTP_DAMAGED;
			}
			*result = attempt;
			if (got == 0 && attempt.status != 200 && attempt.status != 206)
			{
				fail(result, "%s answered %d", attempt.host, attempt.status);
				break;
			}
			if (++failures >= 3)
				break;
			/* (a connection that broke: tried again after a moment, from where it got to) */
			result->error[0] = 0;
			{
				unsigned waited;

				for (waited = 0; waited < 2000u && !(hooks->stop && hooks->stop(hooks->context)); waited += 50u)
					sleep_ms(50);
			}
		}
	}
	if (d.written < size)
	{
		/* (kept for the next attempt, on the card as far as it got, when the card may be written) */
		download_finish(&d, download_may_continue(&d));
		mbedtls_sha256_free(&d.sha);
		if (!result->error[0])
			fail(result, "the download of %s stopped at %u of %u bytes", part_path, (unsigned)d.written, (unsigned)size);
		return d.card_error ? WIIU_HTTP_CARD_ERROR : WIIU_HTTP_INCOMPLETE;
	}
	mbedtls_sha256_finish(&d.sha, digest);
	mbedtls_sha256_free(&d.sha);
	while (!download_may_continue(&d) && !(hooks->stop && hooks->stop(hooks->context)))
		sleep_ms(50);
	if (download_finish(&d, 1) != 0)
	{
		fail(result, "%s could not be flushed to the card", part_path);
		return WIIU_HTTP_CARD_ERROR;
	}
	if (memcmp(digest, sha256, sizeof(digest)) != 0)
	{
		remove(part_path);
		fail(result, "the download is not the manifest's file (its SHA-256 differs)");
		return WIIU_HTTP_DAMAGED;
	}
	remove(new_path);
	if (rename(part_path, new_path) != 0)
	{
		fail(result, "%s could not be renamed (errno %d)", part_path, errno);
		return WIIU_HTTP_CARD_ERROR;
	}
	return WIIU_HTTP_DOWNLOADED;
}
