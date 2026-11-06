/*
WIIU_HTTPS.H

One HTTPS GET for the platform layer's update_https_request (port/linux/src/update.h), which the server browser fetches the master servers'
lists with (master_list.c). The platform layer (platform_wiiu.c) is built with the game's structure layouts and this unit with the C library's
(as the posix_*.c are), so only integers, strings and pointers cross here, as in port/android/host/host_https.c.
*/

#ifndef HALO_WIIU_HTTPS_H
#define HALO_WIIU_HTTPS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Asks for the https:// address with Mbed TLS over nsysnet (wiiu_update_http.c), the certificate and the host name checked against the roots of
wiiu_update_roots.h; no redirect followed, no plain HTTP, and only while wiiu_network_allowed() says the network may be used. headers: extra request
header lines, each ended by CR LF (or NULL). At most maximum bytes of body are taken. 1 if an answer came (of any status): *status is its status,
response_headers its header lines as "Name: value" and a newline each (a line that does not fit is left out), *body its body (malloc'd, NUL terminated,
*body_size long; the caller frees it). Else 0, and why in error (with Mbed TLS's own words for a TLS failure). At most 60 seconds, apart from the name's
lookup, which is the system's. Any thread may call it; one request at a time is made. */
int wiiu_https_get(const char *url, const char *headers, unsigned long maximum, int *status, char *response_headers, int response_headers_size,
	char **body, unsigned long *body_size, char *error, int error_size);

/* For the tests, on a desktop: trust these roots (DER) in place of the updater's, from the next request on; 0 roots puts the updater's back. */
void wiiu_https_use_roots(const unsigned char *const *roots, const size_t *sizes, unsigned count);

#ifdef __cplusplus
}
#endif

#endif
