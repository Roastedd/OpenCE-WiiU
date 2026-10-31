/* Native qualification of the production mip helper. Only gpu_test.txt 2
requests this work. Generated images, uploads, scratch retirements and every
readback remain alive until one final GPU completion wait. */

#include "wiiu_gx2_mip.h"
#include "wiiu_mip.h"
#include "wiiu_shell.h"

#include <coreinit/memdefaultheap.h>
#include <gx2/clear.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/state.h>
#include <gx2/utils.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MIP_TEST_ALLOCATIONS 256
#define MIP_TEST_RETIRED 128
#define MIP_TEST_CHECKS 64
#define MIP_TEST_BYTES (8u * 1024u * 1024u)
#define MIP_TEST_EXPECTED_CHECKS 40
#define MIP_TEST_EXPECTED_DERIVES 20

struct mip_test_check
{
	char name[64];
	GX2Surface readback;
	uint8_t *expected;
	unsigned width, height, level;
};

struct mip_test_state
{
	void *allocations[MIP_TEST_ALLOCATIONS];
	void *retired[MIP_TEST_RETIRED];
	unsigned allocation_count, retired_count, allocation_bytes;
	unsigned check_count, derive_count, failures;
	struct mip_test_check checks[MIP_TEST_CHECKS];
};

/* The helper runs on the GX2 consumer, as does this requested test. */
static struct mip_test_state *mip_test_active;

static void *mip_test_allocate(struct mip_test_state *state, uint32_t size, uint32_t alignment)
{
	void *memory;

	if (!size || state->allocation_count == MIP_TEST_ALLOCATIONS || size > MIP_TEST_BYTES - state->allocation_bytes)
		return NULL;
	memory = MEMAllocFromDefaultHeapEx(size, alignment > 64 ? (int32_t)alignment : 64);
	if (!memory)
		return NULL;
	memset(memory, 0, size);
	state->allocations[state->allocation_count++] = memory;
	state->allocation_bytes += size;
	return memory;
}

static void mip_test_retire(void *memory)
{
	struct mip_test_state *state = mip_test_active;
	unsigned i;

	if (!memory || !state)
		return;
	for (i = 0; i < state->retired_count; i++)
		if (state->retired[i] == memory)
		{
			state->failures++;
			return;
		}
	/* At most two retirements per derive; this test makes fewer than 64
	derives. An unexpected overflow is a failure, never an unsafe early free. */
	if (state->retired_count == MIP_TEST_RETIRED)
	{
		state->failures++;
		return;
	}
	state->retired[state->retired_count++] = memory;
}

static unsigned mip_test_extent(unsigned size, unsigned level)
{
	return size >> level ? size >> level : 1;
}

static int mip_test_surface(struct mip_test_state *state, GX2Surface *surface,
	unsigned width, unsigned height, unsigned levels, GX2TileMode mode, GX2SurfaceUse use)
{
	uint32_t bytes;

	memset(surface, 0, sizeof(*surface));
	surface->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	surface->width = width;
	surface->height = height;
	surface->depth = 1;
	surface->mipLevels = levels;
	surface->format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	surface->aa = GX2_AA_MODE1X;
	surface->tileMode = mode;
	surface->use = use;
	GX2CalcSurfaceSizeAndAlignment(surface);
	bytes = surface->imageSize + surface->mipmapSize;
	if (bytes < surface->imageSize || !bytes)
		return 0;
	surface->image = mip_test_allocate(state, bytes, surface->alignment);
	if (!surface->image)
		return 0;
	surface->mipmaps = levels > 1 ? (uint8_t *)surface->image + surface->mipLevelOffset[0] : NULL;
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, surface->image, bytes);
	return 1;
}

static int mip_test_upload(struct mip_test_state *state, GX2Surface *destination, unsigned level,
	const uint8_t *source, unsigned source_pitch)
{
	GX2Surface linear;
	unsigned y, width = mip_test_extent(destination->width, level), height = mip_test_extent(destination->height, level);

	if (!mip_test_surface(state, &linear, width, height, 1, GX2_TILE_MODE_LINEAR_ALIGNED, GX2_SURFACE_USE_TEXTURE))
		return 0;
	for (y = 0; y < height; y++)
		memcpy((uint8_t *)linear.image + (size_t)y * linear.pitch * 4, source + (size_t)y * source_pitch * 4, width * 4);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, linear.image, linear.imageSize);
	GX2CopySurface(&linear, 0, 0, destination, level, 0);
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), destination->image,
		destination->imageSize + destination->mipmapSize);
	return 1;
}

static int mip_test_poison(struct mip_test_state *state, GX2Surface *surface, unsigned level)
{
	const unsigned width = mip_test_extent(surface->width, level), height = mip_test_extent(surface->height, level);
	uint8_t *bytes = mip_test_allocate(state, width * height * 4, 64);
	unsigned i;

	if (!bytes)
		return 0;
	for (i = 0; i < width * height; i++)
	{
		bytes[i * 4] = 201;
		bytes[i * 4 + 1] = (uint8_t)(17 + level);
		bytes[i * 4 + 2] = 81;
		bytes[i * 4 + 3] = 33;
	}
	return mip_test_upload(state, surface, level, bytes, width);
}

static uint8_t *mip_test_pattern(struct mip_test_state *state, unsigned width, unsigned height, unsigned pitch)
{
	uint8_t *bytes = mip_test_allocate(state, pitch * height * 4, 64);
	unsigned x, y, c;

	if (!bytes)
		return NULL;
	memset(bytes, 0xee, pitch * height * 4);
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			for (c = 0; c < 4; c++)
				bytes[(y * pitch + x) * 4 + c] = (uint8_t)((11 * x + 29 * y + 17 * c) & 255);
	return bytes;
}

static uint8_t *mip_test_expected(struct mip_test_state *state, const uint8_t *source,
	unsigned width, unsigned height, unsigned source_pitch)
{
	const unsigned out_width = width > 1 ? width / 2 : 1, out_height = height > 1 ? height / 2 : 1;
	uint8_t *bytes = mip_test_allocate(state, out_width * out_height * 4, 64);

	if (bytes)
		wiiu_mip_next(source, width, height, source_pitch, bytes, out_width);
	return bytes;
}

static int mip_test_check(struct mip_test_state *state, const char *name, GX2Surface *source,
	unsigned level, const uint8_t *expected, unsigned expected_pitch)
{
	const unsigned width = mip_test_extent(source->width, level), height = mip_test_extent(source->height, level);
	struct mip_test_check *check;
	unsigned y;

	if (state->check_count == MIP_TEST_CHECKS)
		return 0;
	check = &state->checks[state->check_count];
	memset(check, 0, sizeof(*check));
	check->expected = mip_test_allocate(state, width * height * 4, 64);
	if (!check->expected || !mip_test_surface(state, &check->readback, width, height, 1, GX2_TILE_MODE_LINEAR_ALIGNED, GX2_SURFACE_USE_TEXTURE))
		return 0;
	for (y = 0; y < height; y++)
		memcpy(check->expected + (size_t)y * width * 4, expected + (size_t)y * expected_pitch * 4, width * 4);
	snprintf(check->name, sizeof(check->name), "%s", name);
	check->width = width;
	check->height = height;
	check->level = level;
	GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), source->image,
		source->imageSize + source->mipmapSize);
	GX2CopySurface(source, level, 0, &check->readback, 0, 0);
	state->check_count++;
	return 1;
}

static int mip_test_derive(struct mip_test_state *state, GX2Surface *source, unsigned source_level,
	GX2Surface *destination, unsigned destination_level)
{
	if (state->derive_count >= 60 || !wiiu_gx2_mip_derive(source, source_level, destination, destination_level, mip_test_retire))
		return 0;
	state->derive_count++;
	return 1;
}

static int mip_test_case(struct mip_test_state *state, const char *name, const uint8_t *source_bytes,
	unsigned width, unsigned height, unsigned pitch, unsigned source_level, unsigned destination_level)
{
	const unsigned out_width = width > 1 ? width / 2 : 1, out_height = height > 1 ? height / 2 : 1;
	GX2Surface source, destination;
	uint8_t *expected = mip_test_expected(state, source_bytes, width, height, pitch);
	unsigned level;
	char preserved[64];

	if (!expected || !mip_test_surface(state, &source, width << source_level, height << source_level, source_level + 1,
		GX2_TILE_MODE_DEFAULT, GX2_SURFACE_USE_TEXTURE) ||
		!mip_test_surface(state, &destination, out_width << destination_level, out_height << destination_level, destination_level + 1,
		GX2_TILE_MODE_DEFAULT, GX2_SURFACE_USE_TEXTURE))
		return 0;
	for (level = 0; level < source.mipLevels; level++)
		if (level == source_level ? !mip_test_upload(state, &source, level, source_bytes, pitch) : !mip_test_poison(state, &source, level))
			return 0;
	for (level = 0; level < destination.mipLevels; level++)
		if (!mip_test_poison(state, &destination, level))
			return 0;
	if (!mip_test_derive(state, &source, source_level, &destination, destination_level) ||
		!mip_test_check(state, name, &destination, destination_level, expected, out_width))
		return 0;
	snprintf(preserved, sizeof(preserved), "%.48s.source-intact", name);
	return mip_test_check(state, preserved, &source, source_level, source_bytes, pitch);
}

static int mip_test_calibration(struct mip_test_state *state)
{
	static const uint8_t known[4] = { 17, 63, 127, 241 };
	GX2Surface source;
	GX2ColorBuffer color;

	if (!mip_test_surface(state, &source, 1, 1, 1, GX2_TILE_MODE_DEFAULT, GX2_SURFACE_USE_TEXTURE) ||
		!mip_test_upload(state, &source, 0, known, 1) ||
		!mip_test_check(state, "calibration-upload-copy-RGBA", &source, 0, known, 1))
		return 0;
	memset(&color, 0, sizeof(color));
	if (!mip_test_surface(state, &color.surface, 1, 1, 1, GX2_TILE_MODE_DEFAULT,
		(GX2SurfaceUse)(GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE)))
		return 0;
	color.viewNumSlices = 1;
	GX2InitColorBufferRegs(&color);
	GX2ClearColor(&color, 17.0f / 255.0f, 63.0f / 255.0f, 127.0f / 255.0f, 241.0f / 255.0f);
	return mip_test_check(state, "calibration-clear-RGBA", &color.surface, 0, known, 1);
}

static int mip_test_basic(struct mip_test_state *state)
{
	static const uint8_t remainders[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3 };
	static const uint8_t channels[16] = { 255, 0, 0, 1, 255, 0, 1, 1, 255, 0, 2, 0, 255, 255, 3, 0 };
	static const unsigned dimensions[10][2] =
	{
		{ 1, 1 }, { 1, 2 }, { 2, 1 }, { 1, 7 }, { 7, 1 }, { 3, 3 }, { 5, 7 }, { 17, 9 }, { 2, 129 }, { 129, 2 }
	};
	uint8_t *all_sums, *bytes;
	unsigned i, c, n, x, y;
	char name[64];

	if (!mip_test_case(state, "remainders-0-1-2-3", remainders, 2, 2, 2, 0, 0) ||
		!mip_test_case(state, "distinct-channels-alpha-half-up", channels, 2, 2, 2, 0, 0))
		return 0;
	all_sums = mip_test_pattern(state, 64, 64, 67);
	if (!all_sums)
		return 0;
	for (i = 0; i < 1024; i++)
		for (c = 0; c < 4; c++)
		{
			unsigned remaining = (i + c * 257) % 1021;

			for (n = 0; n < 4; n++)
			{
				const unsigned position = (n + i + c) % 4, dx = position & 1, dy = position >> 1;
				const unsigned value = remaining > 255 ? 255 : remaining;

				all_sums[((i / 32 * 2 + dy) * 67 + i % 32 * 2 + dx) * 4 + c] = (uint8_t)value;
				remaining -= value;
			}
		}
	if (!mip_test_case(state, "all-1021-sums-each-RGBA-nonzero-mips", all_sums, 64, 64, 67, 1, 1))
		return 0;
	for (i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); i++)
	{
		const unsigned width = dimensions[i][0], height = dimensions[i][1], pitch = width + 3;

		bytes = mip_test_pattern(state, width, height, pitch);
		snprintf(name, sizeof(name), "dimensions-%ux%u", width, height);
		if (!bytes || !mip_test_case(state, name, bytes, width, height, pitch, 0, 0))
			return 0;
	}
	bytes = mip_test_pattern(state, 5, 7, 9);
	if (!bytes)
		return 0;
	for (y = 0; y < 7; y++)
		for (x = 0; x < 5; x++)
		{
			uint8_t *pixel = bytes + (y * 9 + x) * 4;

			pixel[0] = (uint8_t)(x * 23);
			pixel[1] = (uint8_t)(y * 31);
			pixel[2] = (uint8_t)(x * 11 + y * 7);
			pixel[3] = (uint8_t)(255 - x * 17 - y * 13);
		}
	if (!mip_test_case(state, "odd-edge-interior", bytes, 5, 7, 9, 0, 0))
		return 0;
	/* Upload buffers above are independent retained copies. This CPU input
	can change without altering an already queued upload. */
	for (y = 0; y < 7; y++)
		for (x = 0; x < 5; x++)
			if (x == 4 || y == 6)
			{
				uint8_t *pixel = bytes + (y * 9 + x) * 4;

				pixel[0] = pixel[2] = 255;
				pixel[1] = pixel[3] = 0;
			}
	return mip_test_case(state, "odd-edge-poison-unused", bytes, 5, 7, 9, 0, 0);
}

static int mip_test_chain(struct mip_test_state *state)
{
	GX2Surface chain;
	uint8_t *base = mip_test_allocate(state, 4 * 4 * 4, 64), *level1, *level2, *updated, *updated1, *updated2;
	unsigned i;

	if (!base)
		return 0;
	for (i = 0; i < 3; i++)
		memset(base + ((i / 2 * 2) * 4 + i % 2 * 2) * 4, 2, 4);
	level1 = mip_test_expected(state, base, 4, 4, 4);
	level2 = level1 ? mip_test_expected(state, level1, 2, 2, 2) : NULL;
	if (!level2 || level2[0] != 1 || level2[3] != 1 ||
		!mip_test_surface(state, &chain, 4, 4, 3, GX2_TILE_MODE_DEFAULT, GX2_SURFACE_USE_TEXTURE) ||
		!mip_test_upload(state, &chain, 0, base, 4) || !mip_test_poison(state, &chain, 1) || !mip_test_poison(state, &chain, 2) ||
		!mip_test_derive(state, &chain, 0, &chain, 1) || !mip_test_derive(state, &chain, 1, &chain, 2) ||
		!mip_test_check(state, "chain-half-up-level1", &chain, 1, level1, 2) ||
		!mip_test_check(state, "chain-half-up-level2", &chain, 2, level2, 1) ||
		!mip_test_check(state, "chain-base-intact", &chain, 0, base, 4))
		return 0;
	updated = mip_test_pattern(state, 4, 4, 7);
	updated1 = updated ? mip_test_expected(state, updated, 4, 4, 7) : NULL;
	updated2 = updated1 ? mip_test_expected(state, updated1, 2, 2, 2) : NULL;
	if (!updated2 || !mip_test_upload(state, &chain, 0, updated, 7) ||
		!mip_test_derive(state, &chain, 0, &chain, 1) || !mip_test_derive(state, &chain, 1, &chain, 2) ||
		!mip_test_check(state, "queued-update-level1", &chain, 1, updated1, 2) ||
		!mip_test_check(state, "queued-update-level2", &chain, 2, updated2, 1))
		return 0;
	/* A level explicitly rendered after the base is the next source. This
	checks visibility and preservation; renderer stamp policy is checked by
	the caller's normal scene after this helper qualification. */
	memset(level1, 211, 2 * 2 * 4);
	for (i = 0; i < 4; i++)
		level1[i * 4 + 3] = 97;
	level2 = mip_test_expected(state, level1, 2, 2, 2);
	if (!level2 || !mip_test_upload(state, &chain, 1, level1, 2) ||
		!mip_test_derive(state, &chain, 1, &chain, 2) ||
		!mip_test_check(state, "explicit-intermediate-source", &chain, 1, level1, 2) ||
		!mip_test_check(state, "explicit-intermediate-derived", &chain, 2, level2, 1) ||
		!mip_test_check(state, "explicit-base-preserved", &chain, 0, updated, 7))
		return 0;
	return 1;
}

static unsigned mip_test_results(struct mip_test_state *state)
{
	unsigned index, passed = 0;

	for (index = 0; index < state->check_count; index++)
	{
		struct mip_test_check *check = &state->checks[index];
		unsigned x, y, c, mismatches = 0, maximum = 0, fx = 0, fy = 0, fc = 0, found = 0, wanted = 0;
		uint32_t actual_hash = 2166136261u, expected_hash = 2166136261u;

		GX2Invalidate(GX2_INVALIDATE_MODE_CPU, check->readback.image, check->readback.imageSize);
		for (y = 0; y < check->height; y++)
			for (x = 0; x < check->width; x++)
				for (c = 0; c < 4; c++)
				{
					const unsigned actual = ((const uint8_t *)check->readback.image)[(y * check->readback.pitch + x) * 4 + c];
					const unsigned expected = check->expected[(y * check->width + x) * 4 + c];
					const unsigned error = actual > expected ? actual - expected : expected - actual;

					actual_hash = (actual_hash ^ actual) * 16777619u;
					expected_hash = (expected_hash ^ expected) * 16777619u;
					if (error > maximum)
						maximum = error;
					if (error)
					{
						if (!mismatches)
						{
							fx = x; fy = y; fc = c; found = actual; wanted = expected;
						}
						mismatches++;
					}
				}
		if (!mismatches)
			passed++;
		else
			state->failures++;
		wiiu_log("GPU mip test: %u %s %ux%u L%u bytes %u bad %u max %u hash %08x/%08x first %u,%u,%u=%u/%u", index,
			check->name, check->width, check->height, check->level, check->width * check->height * 4, mismatches, maximum,
			(unsigned)actual_hash, (unsigned)expected_hash, fx, fy, fc, found, wanted);
	}
	return passed;
}

int wiiu_gx2_mip_test(void)
{
	static struct mip_test_state state;
	unsigned i, passed;
	int submitted;

	memset(&state, 0, sizeof(state));
	mip_test_active = &state;
	wiiu_log("GPU mip test: begin exact RGBA half-up; uploads and derives queue before one completion wait");
	submitted = mip_test_calibration(&state) && mip_test_basic(&state) && mip_test_chain(&state);
	if (!submitted)
	{
		state.failures++;
		wiiu_log("GPU mip test: stopped submitting after allocation/helper failure; queued storage is retained until completion");
	}
	GX2Flush();
	GX2DrawDone();
	passed = mip_test_results(&state);
	if (submitted && (state.check_count != MIP_TEST_EXPECTED_CHECKS || state.derive_count != MIP_TEST_EXPECTED_DERIVES))
	{
		state.failures++;
		wiiu_log("GPU mip test: coverage incomplete, expected %u checks and %u derives", MIP_TEST_EXPECTED_CHECKS, MIP_TEST_EXPECTED_DERIVES);
	}
	mip_test_active = NULL;
	for (i = 0; i < state.retired_count; i++)
		MEMFreeToDefaultHeap(state.retired[i]);
	for (i = 0; i < state.allocation_count; i++)
		MEMFreeToDefaultHeap(state.allocations[i]);
	wiiu_log("GPU mip test: complete checks %u passed %u derives %u failures %u allocations %u bytes %u retired %u waits 1",
		state.check_count, passed, state.derive_count, state.failures, state.allocation_count, state.allocation_bytes, state.retired_count);
	return submitted && state.failures == 0 && state.check_count != 0;
}
