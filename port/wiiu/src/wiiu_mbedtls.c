/*
WIIU_MBEDTLS.C

The five functions of mbedtls's platform modules that the Wii U build leaves out (their sources are for Unix and Windows: WIIU_EXCLUDE in
tools/wiiu_build.py): zeroing memory, a millisecond clock, gmtime, and a source of entropy.

The entropy is wiiu_entropy.c's: several reads of the time base with a yield between, the clocks, addresses and a seed file on the SD card, mixed with
BLAKE2b. It is better than the clock alone and it is not a hardware source: wiiu_entropy.h says what it is and what it is not. Mbed TLS asks for it
when its random generator starts and again every 10000 requests; each such poll also writes a new seed file (the card is read and written only in the
foreground).
*/

#include "wiiu_entropy.h"

#include <coreinit/time.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

void mbedtls_platform_zeroize(void *buffer, size_t length);
void mbedtls_zeroize_and_free(void *buffer, size_t length);
int64_t mbedtls_ms_time(void);
struct tm *mbedtls_platform_gmtime_r(const time_t *time, struct tm *result);
int mbedtls_platform_entropy_poll(void *data, unsigned char *output, size_t length, size_t *output_length);

void mbedtls_platform_zeroize(void *buffer, size_t length)
{
	volatile unsigned char *bytes = (volatile unsigned char *)buffer;

	while (buffer && length--)
		*bytes++ = 0;
}

void free(void *);

void mbedtls_zeroize_and_free(void *buffer, size_t length)
{
	if (buffer)
	{
		mbedtls_platform_zeroize(buffer, length);
		free(buffer);
	}
}

int64_t mbedtls_ms_time(void)
{
	return (int64_t)OSTicksToMilliseconds(OSGetSystemTime());
}

struct tm *mbedtls_platform_gmtime_r(const time_t *time, struct tm *result)
{
	return gmtime_r(time, result);
}

int mbedtls_platform_entropy_poll(void *data, unsigned char *output, size_t length, size_t *output_length)
{
	(void)data;
	wiiu_entropy_bytes(output, length);
	wiiu_entropy_save_seed();
	*output_length = length;
	return 0;
}
