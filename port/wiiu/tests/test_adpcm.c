/* The real Wii U decoder, the unchanged other-platform path and the exact
   isolated native input. None of these routines is copied into this test. */
#include "adpcm_qualified.inc"
#include "adpcm_current.inc"
#include <stdio.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "ADPCM check failed at line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

typedef int (*expand_fn)(int, int *, int *);
typedef short *(*decode_fn)(const unsigned char *, unsigned long, unsigned long, unsigned long *);
static const expand_fn expanders[] = { ima_expand, ima_expand_cached, production_fast_expand, production_legacy_expand };
static const decode_fn decoders[] = { decode_original, decode_candidate, production_fast_decode, production_legacy_decode };
static uint32_t random_state = 0x412215u;

static unsigned random_number(void)
{
	random_state = random_state * 1664525u + 1013904223u;
	return random_state;
}

static int transition(int predictor, int index, unsigned nibble)
{
	int expected_predictor = predictor, expected_index = index;
	int expected = expanders[0]((int)nibble, &expected_predictor, &expected_index);
	unsigned variant;

	for (variant = 1; variant < sizeof(expanders) / sizeof(*expanders); variant++)
	{
		int actual_predictor = predictor, actual_index = index;
		int actual = expanders[variant]((int)nibble, &actual_predictor, &actual_index);

		CHECK(actual == expected);
		CHECK(actual_predictor == expected_predictor);
		CHECK(actual_index == expected_index);
	}
	return 0;
}

static int packet(const unsigned char *source, unsigned size, unsigned channels, int constant)
{
	unsigned variant;
	unsigned long expected_frames;
	short *expected = decoders[0](source, size, channels, &expected_frames);

	CHECK(expected != NULL);
	for (variant = 1; variant < sizeof(decoders) / sizeof(*decoders); variant++)
	{
		unsigned long actual_frames, sample;
		short *actual = decoders[variant](source, size, channels, &actual_frames);

		CHECK(actual != NULL);
		CHECK(actual_frames == expected_frames);
		CHECK(memcmp(actual, expected, actual_frames * channels * sizeof(*actual)) == 0);
		if (constant >= -32768 && constant <= 32767)
			for (sample = 0; sample < actual_frames * channels; sample++)
				CHECK(actual[sample] == constant);
		free(actual);
	}
	free(expected);
	return 0;
}

int main(void)
{
	const int predictors[] = { -32768, -32767, -16384, -1, 0, 1, 16383, 32766, 32767 };
	unsigned index, nibble, predictor, iteration, channels, blocks;
	unsigned char source[36 * 2 * 1024 + 73];

	CHECK(sizeof(ima_decode_table) == 11392);
	CHECK(sizeof(production_fast_table) == sizeof(ima_decode_table));
	CHECK(memcmp(production_fast_table, ima_decode_table, sizeof(ima_decode_table)) == 0);
	for (index = 0; index < 89; index++)
	for (nibble = 0; nibble < 16; nibble++)
	for (predictor = 0; predictor < sizeof(predictors) / sizeof(*predictors); predictor++)
		CHECK(transition(predictors[predictor], (int)index, nibble) == 0);
	for (iteration = 0; iteration < 1000000; iteration++)
	{
		int value = (int)(random_number() & 65535) - 32768;
		int step_index = (int)(random_number() % 89);

		CHECK(transition(value, step_index, random_number() % 16) == 0);
	}
	for (iteration = 0; iteration < 256; iteration++)
	{
		unsigned size, at;

		channels = 1 + iteration % 2;
		blocks = (iteration * 29) % 1025;
		size = 36 * channels * blocks + iteration % (36 * channels);
		for (at = 0; at < size; at++)
			source[at] = (unsigned char)random_number();
		CHECK(packet(source, size, channels, 40000) == 0);
	}
	/* All malformed header step bytes and explicit predictor extremes.
	   The final high nibble is deliberately nonzero in two patterns: it is
	   padding, while the first header sample must remain the first output. */
	for (channels = 1; channels <= 2; channels++)
	for (index = 0; index < 256; index++)
	for (predictor = 0; predictor < 3; predictor++)
	for (iteration = 0; iteration < 3; iteration++)
	{
		const int values[] = { -32768, 0, 32767 };
		const unsigned patterns[] = { 0x00, 0x77, 0xff };
		unsigned channel, size = 36 * channels;
		int constant = values[predictor] == 32767 && iteration == 1 ? 32767 :
			values[predictor] == -32768 && iteration == 2 ? -32768 : 40000;

		memset(source, (int)patterns[iteration], size);
		for (channel = 0; channel < channels; channel++)
		{
			unsigned header = channel * 4;
			uint16_t value = (uint16_t)values[predictor];

			source[header] = (unsigned char)value;
			source[header + 1] = (unsigned char)(value >> 8);
			source[header + 2] = (unsigned char)index;
			source[header + 3] = 0xa5;
		}
		CHECK(packet(source, size, channels, constant) == 0);
	}
	puts("ADPCM exact: 12816 boundary transitions, 1000000 random transitions, 256 complete mono/stereo packets, 4608 malformed-header/extreme/saturation vectors; qualified/current/legacy paths match");
	return 0;
}
