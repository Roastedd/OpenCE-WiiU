/*
WIIU_MBEDTLS.C

The five functions of mbedtls's platform modules that the Wii U build leaves out (their sources are for Unix and Windows: WIIU_EXCLUDE in
tools/wiiu_build.py): zeroing memory, a millisecond clock, gmtime, and a source of entropy.

The entropy is NOT good: the system clock and a mix of the times the calls were made. It stands in until internet play, the one user of mbedtls's random
numbers (the peer-to-peer keys, posix_dtls.c), is written; Cafe OS has a random number service (IOS's /dev/urandom through the MCP/IOSU
modules) that has to be used before any key made here is trusted.
*/

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
	static uint64_t state;
	size_t i;

	(void)data;
	state ^= (uint64_t)OSGetSystemTime() * 0x9E3779B97F4A7C15ull + (uint64_t)OSGetTick();
	for (i = 0; i < length; i++)
	{
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		output[i] = (unsigned char)(state >> 24);
	}
	*output_length = length;
	return 0;
}
