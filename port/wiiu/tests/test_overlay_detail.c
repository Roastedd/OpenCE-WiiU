/* test_overlay_detail.c: the detailed overlay's numbers (wiiu_perf.c: the window arithmetic, the pools' levels, the log's lines) and its panel
(wiiu_overlay_detail.c). With an argument it also writes the panel as a PNG, over a stand-in for a game picture, at the scale the TV draws it
at (3), to look at: test_overlay_detail out.png [stressed]. Build and run (from port/wiiu):

	cc -std=gnu11 -O2 -Wall -Wextra -o /tmp/test_overlay_detail tests/test_overlay_detail.c src/wiiu_perf.c src/wiiu_overlay_detail.c src/wiiu_overlay.c src/wiiu_font.c
	/tmp/test_overlay_detail [out.png [stressed]]
*/

#include "../src/wiiu_overlay.h"
#include "../src/wiiu_perf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

#define NEAR(value, wanted, tolerance) (((value) > (wanted) - (tolerance)) && ((value) < (wanted) + (tolerance)))

/* ---------- a minimal PNG (stored deflate blocks) */

static uint32_t crc_table[256];

static void crc_init(void)
{
	uint32_t n, k;

	for (n = 0; n < 256; n++)
	{
		uint32_t c = n;

		for (k = 0; k < 8; k++)
			c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
		crc_table[n] = c;
	}
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		crc = crc_table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
	return crc;
}

static void put32(FILE *file, uint32_t value)
{
	fputc((int)(value >> 24), file);
	fputc((int)(value >> 16 & 255), file);
	fputc((int)(value >> 8 & 255), file);
	fputc((int)(value & 255), file);
}

static void chunk(FILE *file, const char *type, const uint8_t *data, size_t size)
{
	uint32_t crc = crc_update(0xffffffffu, (const uint8_t *)type, 4);

	put32(file, (uint32_t)size);
	fwrite(type, 1, 4, file);
	if (size)
	{
		fwrite(data, 1, size, file);
		crc = crc_update(crc, data, size);
	}
	put32(file, crc ^ 0xffffffffu);
}

static void write_png(const char *path, const uint8_t *rgb, unsigned width, unsigned height)
{
	const size_t row = (size_t)width * 3 + 1, raw_size = row * height;
	uint8_t *raw = malloc(raw_size), *zlib = malloc(raw_size + raw_size / 65535 * 5 + 64);
	uint8_t header[13];
	size_t at = 0, left, used = 0, y;
	uint32_t a = 1, b = 0;
	FILE *file = fopen(path, "wb");

	if (!file || !raw || !zlib)
		return;
	for (y = 0; y < height; y++)
	{
		raw[y * row] = 0;
		memcpy(raw + y * row + 1, rgb + y * (size_t)width * 3, (size_t)width * 3);
	}
	for (at = 0; at < raw_size; at++)
	{
		a = (a + raw[at]) % 65521u;
		b = (b + a) % 65521u;
	}
	zlib[used++] = 0x78;
	zlib[used++] = 0x01;
	for (at = 0, left = raw_size; left;)
	{
		const size_t n = left > 65535 ? 65535 : left;

		zlib[used++] = left == n;
		zlib[used++] = (uint8_t)(n & 255);
		zlib[used++] = (uint8_t)(n >> 8);
		zlib[used++] = (uint8_t)(~n & 255);
		zlib[used++] = (uint8_t)(~n >> 8 & 255);
		memcpy(zlib + used, raw + at, n);
		used += n;
		at += n;
		left -= n;
	}
	zlib[used++] = (uint8_t)(b >> 8);
	zlib[used++] = (uint8_t)(b & 255);
	zlib[used++] = (uint8_t)(a >> 8);
	zlib[used++] = (uint8_t)(a & 255);
	header[0] = (uint8_t)(width >> 24);
	header[1] = (uint8_t)(width >> 16);
	header[2] = (uint8_t)(width >> 8);
	header[3] = (uint8_t)width;
	header[4] = (uint8_t)(height >> 24);
	header[5] = (uint8_t)(height >> 16);
	header[6] = (uint8_t)(height >> 8);
	header[7] = (uint8_t)height;
	header[8] = 8;
	header[9] = 2;
	header[10] = header[11] = header[12] = 0;
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, file);
	chunk(file, "IHDR", header, 13);
	chunk(file, "IDAT", zlib, used);
	chunk(file, "IEND", NULL, 0);
	fclose(file);
	free(raw);
	free(zlib);
}

/* ---------- views */

static struct perf_view sample_view(int stressed)
{
	struct perf_view view;
	unsigned i;

	memset(&view, 0, sizeof(view));
	view.valid = 1;
	view.frames = 15;
	view.seconds = 0.25f;
	if (!stressed)
	{
		view.frame_ms = 16.7f;
		view.fps = 59.9f;
		view.worst_ms = 22.4f;
		view.work_ms = 9.8f;
		view.gpu_ms = 7.1f;
		view.gpu_wait_ms = 0.4f;
		view.vsync_ms = 6.3f;
		view.sim_ms = 4.1f;
		view.sim_wait_ms = 0.9f;
		view.draw_ms = 5.2f;
		view.gx2_ms = 3.0f;
		view.gx2_busy_percent = 18.0f;
		view.audio_ms = 0.5f;
		view.tick_thread_ms = 2.9f;
		view.tick_thread_busy_percent = 41.0f;
		view.ticks_per_second = 60.0f;
		view.overlap = 1;
		view.draw_part_ms[0] = 0.6f;
		view.draw_part_ms[1] = 0.9f;
		view.draw_part_ms[2] = 3.1f;
		view.draw_part_ms[3] = 0.8f;
		view.draw_part_ms[4] = 1.9f;
		view.draw_part_ms[5] = 0.4f;
		view.draws = 612.0f;
		view.objects_found = 184.0f;
		view.objects_culled = 226.0f;
		view.state.render_width = 1280;
		view.state.render_height = 720;
		view.state.drs_lines = 720;
		view.state.drs_full_lines = 720;
		view.state.drs_on = 1;
		view.state.drs_step = 0;
		view.state.interpolation = 1;
		view.state.governor_step = -1;
		view.state.governor_max = 0;
		for (i = 0; i < PERF_POOL_COUNT; i++)
			view.pool[i].known = 1;
		view.pool[PERF_POOL_TEXTURE].used_kib = 13722u;
		view.pool[PERF_POOL_TEXTURE].capacity_kib = 22u * 1024u;
		view.pool[PERF_POOL_TEXTURE].hot_kib = 9u * 1024u;
		view.pool[PERF_POOL_GEOMETRY].used_kib = 41u * 1024u;
		view.pool[PERF_POOL_GEOMETRY].capacity_kib = 128u * 1024u;
		view.pool[PERF_POOL_SOUND].used_kib = 3u * 1024u + 300u;
		view.pool[PERF_POOL_SOUND].capacity_kib = 4u * 1024u;
		view.pool[PERF_POOL_STATE].used_kib = 17u * 1024u + 300u;
		view.pool[PERF_POOL_STATE].capacity_kib = 20u * 1024u;
		view.pool[PERF_POOL_TAGS].used_kib = 16u * 1024u;
		view.pool[PERF_POOL_TAGS].capacity_kib = 22u * 1024u;
		view.pool[PERF_POOL_MEM2].used_kib = 693u * 1024u + 700u;
		view.pool[PERF_POOL_MEM2].capacity_kib = 1536u * 1024u;
		view.pool[PERF_POOL_EDRAM].used_kib = 17u * 1024u + 600u;
		view.pool[PERF_POOL_EDRAM].capacity_kib = 32u * 1024u;
		return view;
	}
	view.frame_ms = 33.3f;
	view.fps = 30.0f;
	view.worst_ms = 52.0f;
	view.work_ms = 25.4f;
	view.gpu_ms = 31.9f;
	view.gpu_wait_ms = 3.2f;
	view.vsync_ms = 0.0f;
	view.sim_ms = 13.8f;
	view.sim_wait_ms = 6.4f;
	view.draw_ms = 9.2f;
	view.gx2_ms = 12.5f;
	view.gx2_busy_percent = 37.0f;
	view.audio_ms = 1.5f;
	view.tick_thread_ms = 14.0f;
	view.tick_thread_busy_percent = 88.0f;
	view.ticks_per_second = 30.0f;
	view.overlap = 2;
	for (i = 0; i < 6; i++)
		view.draw_part_ms[i] = 1.0f + (float)i;
	view.draws = 1840.0f;
	view.objects_found = 410.0f;
	view.objects_culled = 1200.0f;
	view.state.render_width = 960;
	view.state.render_height = 540;
	view.state.drs_lines = 540;
	view.state.drs_full_lines = 720;
	view.state.drs_on = 1;
	view.state.drs_step = 3;
	view.state.interpolation = 0;
	view.state.governor_step = 4;
	view.state.governor_max = 7;
	for (i = 0; i < PERF_POOL_COUNT; i++)
	{
		view.pool[i].known = 1;
		view.pool[i].capacity_kib = 20u * 1024u;
	}
	view.pool[PERF_POOL_TEXTURE].used_kib = 21u * 1024u + 500u; /* 97.6 percent: red */
	view.pool[PERF_POOL_TEXTURE].capacity_kib = 22u * 1024u;
	view.pool[PERF_POOL_TEXTURE].hot_kib = 20u * 1024u;
	view.pool[PERF_POOL_GEOMETRY].used_kib = 100u * 1024u;
	view.pool[PERF_POOL_GEOMETRY].capacity_kib = 128u * 1024u;
	view.pool[PERF_POOL_SOUND].used_kib = 3u * 1024u + 800u; /* 92.2 percent: amber */
	view.pool[PERF_POOL_SOUND].capacity_kib = 4u * 1024u;
	view.pool[PERF_POOL_STATE].used_kib = 19u * 1024u + 900u;
	view.pool[PERF_POOL_STATE].capacity_kib = 20u * 1024u;
	view.pool[PERF_POOL_TAGS].used_kib = 22u * 1024u;
	view.pool[PERF_POOL_TAGS].capacity_kib = 22u * 1024u;
	view.pool[PERF_POOL_MEM2].used_kib = 1500u * 1024u;
	view.pool[PERF_POOL_MEM2].capacity_kib = 1536u * 1024u;
	view.pool[PERF_POOL_EDRAM].known = 0;
	return view;
}

/* a stand-in for a game picture behind the panel: a sky and a snow field with some noise */
static void backdrop(uint8_t *rgb, unsigned width, unsigned height)
{
	unsigned x, y, seed = 12345;

	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			const unsigned noise = (seed = seed * 1664525u + 1013904223u) >> 28;
			const int base = y < height / 2 ? 110 + (int)(y * 60 / height) : 190 - (int)((y - height / 2) * 80 / height);
			uint8_t *p = rgb + ((size_t)y * width + x) * 3;

			p[0] = (uint8_t)(base - 30 + (int)noise);
			p[1] = (uint8_t)(base - 10 + (int)noise);
			p[2] = (uint8_t)(base + 15 + (int)noise);
		}
}

static void composite(uint8_t *rgb, unsigned width, const uint8_t *panel, unsigned panel_height, unsigned scale, unsigned margin)
{
	unsigned x, y, sx, sy;

	for (y = 0; y < panel_height; y++)
		for (x = 0; x < OVERLAY_WIDTH; x++)
		{
			const uint8_t *p = panel + ((size_t)y * OVERLAY_WIDTH + x) * 4;

			for (sy = 0; sy < scale; sy++)
				for (sx = 0; sx < scale; sx++)
				{
					uint8_t *d = rgb + (((size_t)(y * scale + sy)) * width + margin + x * scale + sx) * 3;
					unsigned c;

					for (c = 0; c < 3; c++)
						d[c] = (uint8_t)((p[c] * p[3] + d[c] * (255 - p[3])) / 255);
				}
		}
}

/* ---------- the tests */

static struct perf_totals totals(double ticks_per_ms, unsigned frame, int overlap_enabled)
{
	/* a game that runs at 16.7 ms a frame and spends in a frame: 9.8 ms of work, 6.3 of Present's hold, 0.4 waiting for slots, 2.0 in the throttle's sleeps,
	4.1 in sim, 5.2 in draw, 0.9 waiting for the tick thread, 2.9 in a tick thread job, 1 tick a frame */
	struct perf_totals t;
	unsigned i;

	memset(&t, 0, sizeof(t));
	t.game_frames = frame;
	t.game_work = (uint32_t)(frame * (9.8 + 0.4) * ticks_per_ms);
	t.game_present = (uint32_t)(frame * 6.3 * ticks_per_ms);
	t.game_sim = (uint32_t)(frame * 3.2 * ticks_per_ms);
	t.game_render = (uint32_t)(frame * 5.2 * ticks_per_ms);
	t.game_ticks = frame;
	t.game_slot_wait = (uint32_t)(frame * 0.4 * ticks_per_ms);
	t.game_vsync_hold = (uint32_t)(frame * 6.3 * ticks_per_ms);
	t.overlap_enabled = (uint32_t)overlap_enabled;
	t.overlap_frames = frame;
	t.overlap_overlapped = frame;
	t.overlap_wait = (uint32_t)(frame * 0.9 * ticks_per_ms);
	t.overlap_job = (uint32_t)(frame * 2.9 * ticks_per_ms);
	t.overlap_jobs = frame;
	for (i = 0; i < PERF_PASSES; i++)
		t.pass[i] = (uint32_t)(frame * 0.4 * ticks_per_ms);
	t.pass_windows = frame;
	t.mixer_ticks = (uint32_t)(frame * 0.4 * ticks_per_ms);
	t.mixer_chunks = frame * 12;
	t.decoder_ticks = (uint32_t)(frame * 0.1 * ticks_per_ms);
	t.objects_considered = frame * 400;
	t.objects_found = frame * 180;
	return t;
}

static void test_window(void)
{
	const double tpm = 62500.0; /* the Wii U's time base: 62.5 MHz */
	struct perf_meter meter;
	struct perf_view view;
	struct perf_state state;
	struct perf_pool pools[PERF_POOL_COUNT];
	struct perf_frame frame = { 16.7f, 6.3f, 3.0f, 612 };
	struct perf_totals t0 = totals(tpm, 0, 1), t1;
	unsigned i;

	memset(&state, 0, sizeof(state));
	memset(pools, 0, sizeof(pools));
	state.governor_step = -1;
	perf_meter_reset(&meter);
	/* the first close only takes the totals */
	CHECK(!perf_meter_close(&meter, &t0, tpm, &state, pools, &view) && !view.valid);
	for (i = 0; i < 15; i++)
	{
		perf_meter_frame(&meter, &frame);
		if (i % 2)
			perf_meter_gpu(&meter, 7.0f + (float)(i % 3) * 0.1f);
	}
	t1 = totals(tpm, 15, 1);
	CHECK(perf_meter_close(&meter, &t1, tpm, &state, pools, &view));
	CHECK(view.valid && view.frames == 15);
	CHECK(NEAR(view.frame_ms, 16.7f, 0.01f) && NEAR(view.fps, 59.88f, 0.05f) && NEAR(view.worst_ms, 16.7f, 0.01f));
	CHECK(NEAR(view.work_ms, 9.8f, 0.05f)); /* the game thread's loop (10.2 + 6.3) less its slot wait (0.4) and its hold for the display (6.3) */
	CHECK(NEAR(view.gpu_wait_ms, 0.4f, 0.01f) && NEAR(view.vsync_ms, 6.3f, 0.05f));
	CHECK(NEAR(view.gpu_ms, 7.1f, 0.15f));
	CHECK(NEAR(view.sim_wait_ms, 0.9f, 0.01f) && NEAR(view.sim_ms, 4.1f, 0.01f)); /* (3.2 in the part, and the wait for the tick thread) */
	CHECK(NEAR(view.draw_ms, 5.2f, 0.01f));
	CHECK(NEAR(view.gx2_ms, 3.0f, 0.01f) && NEAR(view.gx2_busy_percent, 17.96f, 0.1f));
	CHECK(NEAR(view.audio_ms, 0.5f, 0.01f));
	CHECK(NEAR(view.tick_thread_ms, 2.9f, 0.01f) && NEAR(view.tick_thread_busy_percent, 17.4f, 0.2f));
	CHECK(NEAR(view.ticks_per_second, 59.88f, 0.1f) && view.overlap == 1);
	CHECK(NEAR(view.draws, 612.0f, 0.1f) && NEAR(view.objects_found, 180.0f, 0.1f) && NEAR(view.objects_culled, 220.0f, 0.1f));
	CHECK(NEAR(view.draw_part_ms[0], 0.4f, 0.01f) && NEAR(view.draw_part_ms[2], 2.0f, 0.02f) && NEAR(view.draw_part_ms[3], 0.8f, 0.01f));
	/* the window began again: a second window is made from the change */
	for (i = 0; i < 10; i++)
		perf_meter_frame(&meter, &frame);
	frame.frame_ms = 40.0f;
	perf_meter_frame(&meter, &frame);
	t0 = totals(tpm, 26, 0);
	t0.game_work = t1.game_work + (uint32_t)(11 * 20.0 * tpm);
	t0.overlap_enabled = 0;
	CHECK(perf_meter_close(&meter, &t0, tpm, &state, pools, &view));
	CHECK(view.frames == 11 && NEAR(view.worst_ms, 40.0f, 0.01f) && view.gpu_ms == 0.0f && view.overlap == 0 && view.tick_thread_ms == 0.0f);
	/* serial fallback: the overlap is on and most frames of the window were the serial loop's */
	for (i = 0; i < 5; i++)
		perf_meter_frame(&meter, &frame);
	t1 = t0;
	t1.game_frames += 5;
	t1.overlap_enabled = 1;
	t1.overlap_serial += 4;
	t1.overlap_overlapped += 1;
	CHECK(perf_meter_close(&meter, &t1, tpm, &state, pools, &view) && view.overlap == 2);
	/* the counters wrap: 32-bit ticks of the time base wrap in 68 seconds, and a window across a wrap is right */
	CHECK(perf_delta(0xfffffff0u, 0x10u) == 0x20u && perf_delta(5u, 5u) == 0u);
	t0 = totals(tpm, 100, 1);
	t0.game_work = 0xffffff00u;
	perf_meter_reset(&meter);
	perf_meter_close(&meter, &t0, tpm, &state, pools, &view);
	for (i = 0; i < 4; i++)
		perf_meter_frame(&meter, &frame);
	t1 = t0;
	t1.game_frames += 4;
	t1.game_work = t0.game_work + (uint32_t)(4 * 10.0 * tpm); /* wraps */
	CHECK(t1.game_work < t0.game_work);
	t1.game_present = t0.game_present;
	CHECK(perf_meter_close(&meter, &t1, tpm, &state, pools, &view) && NEAR(view.work_ms, 10.0f, 0.02f));
}

static void test_pools(void)
{
	struct perf_pool pool = { 0, 0, 0, 1, 0 };
	char text[16];

	/* 20 MiB: 85 percent is 17408 KiB, 95 percent 19456 */
	pool.capacity_kib = 20u * 1024u;
	pool.used_kib = 17408u;
	CHECK(perf_pool_level(&pool) == PERF_LEVEL_OK); /* (at 85, not over) */
	pool.used_kib = 17409u;
	CHECK(perf_pool_level(&pool) == PERF_LEVEL_AMBER);
	pool.used_kib = 19456u;
	CHECK(perf_pool_level(&pool) == PERF_LEVEL_AMBER);
	pool.used_kib = 19457u;
	CHECK(perf_pool_level(&pool) == PERF_LEVEL_RED);
	pool.used_kib = 1024u;
	pool.full = 1;
	CHECK(perf_pool_level(&pool) == PERF_LEVEL_RED); /* (a pool that turned a request away is red whatever it holds) */
	pool.full = 0;
	pool.known = 0;
	CHECK(perf_pool_level(&pool) == PERF_LEVEL_OK && perf_pool_percent(&pool) == 0.0f);
	pool.known = 1;
	pool.capacity_kib = 0;
	CHECK(perf_pool_level(&pool) == PERF_LEVEL_OK && perf_pool_percent(&pool) == 0.0f); /* (no divide by zero) */
	pool.capacity_kib = 4u * 1024u * 1024u;
	pool.used_kib = 0xffffffffu;
	CHECK(perf_pool_level(&pool) == PERF_LEVEL_RED); /* (4 GiB of 4 GiB in KiB: no overflow of the products) */
	CHECK(NEAR(perf_pool_percent(&(struct perf_pool){ 2048, 4096, 0, 1, 0 }), 50.0f, 0.01f));
	perf_mib_text(text, sizeof(text), 13722u);
	CHECK(!strcmp(text, "13.4"));
	perf_mib_text(text, sizeof(text), 22u * 1024u);
	CHECK(!strcmp(text, "22.0"));
	perf_mib_text(text, sizeof(text), 1023u);
	CHECK(!strcmp(text, "1.0")); /* (rounds up into the whole) */
	perf_mib_text(text, sizeof(text), 1536u * 1024u + 51u);
	CHECK(!strcmp(text, "1536.0"));
	CHECK(!strcmp(perf_pool_name(PERF_POOL_TEXTURE), "TEX") && !strcmp(perf_pool_name(PERF_POOL_EDRAM), "EDRAM"));
}

static void test_log(void)
{
	char line[400];
	struct perf_view view = sample_view(0), stressed = sample_view(1), empty;
	int i;

	memset(&empty, 0, sizeof(empty));
	for (i = 0; i < PERF_LOG_LINES; i++)
	{
		CHECK(perf_log_line(line, sizeof(line), &view, i) && strlen(line) < 199 && !strncmp(line, "overlay", 7));
		CHECK(perf_log_line(line, sizeof(line), &stressed, i) && strlen(line) < 199);
		CHECK(perf_log_line(line, sizeof(line), &empty, i) && strstr(line, "no window yet"));
	}
	CHECK(!perf_log_line(line, sizeof(line), &view, PERF_LOG_LINES));
	perf_log_line(line, sizeof(line), &view, 0);
	CHECK(strstr(line, "frame 16.7 ms") && strstr(line, "work 9.8") && strstr(line, "gpu 7.1") && strstr(line, "vsync 6.3"));
	perf_log_line(line, sizeof(line), &view, 2);
	CHECK(strstr(line, "tex 13.4/22.0") && strstr(line, "mem2 free 842.3") && strstr(line, "edram 17.6/32.0"));
	perf_log_line(line, sizeof(line), &stressed, 2);
	CHECK(strstr(line, "edram ?"));
	perf_log_line(line, sizeof(line), &stressed, 1);
	CHECK(strstr(line, "overlap serial") && strstr(line, "gov 4/7") && strstr(line, "30hz"));
	printf("%s\n", (perf_log_line(line, sizeof(line), &view, 0), line));
	printf("%s\n", (perf_log_line(line, sizeof(line), &view, 1), line));
	printf("%s\n", (perf_log_line(line, sizeof(line), &view, 2), line));
	printf("%s\n", (perf_log_line(line, sizeof(line), &stressed, 1), line));
}

static void test_modes(void)
{
	CHECK(overlay_mode_from_file(0) == OVERLAY_MODE_OFF && overlay_mode_from_file(1) == OVERLAY_MODE_COMPACT && overlay_mode_from_file(2) == OVERLAY_MODE_DETAIL);
	CHECK(overlay_mode_from_file(7) == OVERLAY_MODE_COMPACT && overlay_mode_from_file(-1) == OVERLAY_MODE_OFF);
	/* L + R + - goes round: off, the compact strip, the detailed panel, off */
	CHECK(overlay_mode_next(OVERLAY_MODE_OFF) == OVERLAY_MODE_COMPACT && overlay_mode_next(OVERLAY_MODE_COMPACT) == OVERLAY_MODE_DETAIL &&
		overlay_mode_next(OVERLAY_MODE_DETAIL) == OVERLAY_MODE_OFF);
}

static void test_panel(const char *png, int stressed)
{
	static struct { uint8_t before[64], pixels[OVERLAY_WIDTH * OVERLAY_DETAIL_HEIGHT * 4], after[64]; } guarded;
	struct perf_view view = sample_view(stressed), empty, extreme;
	unsigned i, lit = 0, meters = 0;
	clock_t start;
	const int repeats = 200;

	memset(&guarded, 0x5a, sizeof(guarded));
	overlay_render_detail(&view, guarded.pixels);
	for (i = 0; i < sizeof(guarded.before); i++)
		CHECK(guarded.before[i] == 0x5a && guarded.after[i] == 0x5a);
	for (i = 0; i < OVERLAY_WIDTH * OVERLAY_DETAIL_HEIGHT; i++)
	{
		const unsigned alpha = guarded.pixels[i * 4 + 3];

		if (alpha == 0)
		{
			printf("FAIL a pixel with no alpha at %u\n", i);
			failures++;
			break;
		}
		lit += alpha == 255;
		/* a meter's red or green, in the pools' rows (y 91 to 143) */
		if (i / OVERLAY_WIDTH >= 91 && i % OVERLAY_WIDTH >= 112 && i % OVERLAY_WIDTH < 208 && alpha == 255 && guarded.pixels[i * 4 + 1] > 0xa0)
			meters++;
	}
	CHECK(lit > 1500 && meters > 100);
	/* a view with nothing in it, and one with absurd numbers, stay inside the panel and leave the same ground */
	memset(&empty, 0, sizeof(empty));
	memset(&extreme, 0, sizeof(extreme));
	extreme.valid = 1;
	extreme.frame_ms = 1e9f;
	extreme.fps = 1e9f;
	extreme.worst_ms = 1e9f;
	extreme.work_ms = extreme.gpu_ms = extreme.gpu_wait_ms = extreme.vsync_ms = extreme.sim_ms = extreme.draw_ms = 1e9f;
	extreme.draws = extreme.objects_found = extreme.objects_culled = 1e9f;
	extreme.state.render_width = extreme.state.render_height = 100000;
	for (i = 0; i < PERF_POOL_COUNT; i++)
	{
		extreme.pool[i].known = 1;
		extreme.pool[i].used_kib = 0xffffffffu;
		extreme.pool[i].capacity_kib = 1;
		extreme.pool[i].hot_kib = 0xffffffffu;
	}
	for (i = 0; i < 2; i++)
	{
		memset(&guarded, 0x5a, sizeof(guarded));
		overlay_render_detail(i ? &extreme : &empty, guarded.pixels);
		{
			unsigned j;

			for (j = 0; j < sizeof(guarded.before); j++)
				CHECK(guarded.before[j] == 0x5a && guarded.after[j] == 0x5a);
		}
	}
	/* the panel is the same size whatever the numbers */
	{
		static uint8_t a[OVERLAY_WIDTH * OVERLAY_DETAIL_HEIGHT * 4], b[OVERLAY_WIDTH * OVERLAY_DETAIL_HEIGHT * 4];
		struct perf_view one = sample_view(0), two = sample_view(1);

		overlay_render_detail(&one, a);
		overlay_render_detail(&two, b);
		for (i = 0; i < OVERLAY_WIDTH * OVERLAY_DETAIL_HEIGHT; i++)
			if ((a[i * 4 + 3] == 0) != (b[i * 4 + 3] == 0))
			{
				printf("FAIL the panels differ in size\n");
				failures++;
				break;
			}
	}
	/* the cost: a draw of the panel, on the host (the console's Espresso is slower; the panel is drawn four times a second) */
	start = clock();
	for (i = 0; i < (unsigned)repeats; i++)
		overlay_render_detail(&view, guarded.pixels);
	printf("overlay_render_detail: %.3f ms a draw on this host (%d draws)\n", (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC / (double)repeats, repeats);
	if (png)
	{
		const unsigned scale = 3, margin = 38, width = 1024, height = OVERLAY_DETAIL_HEIGHT * scale + 40;
		uint8_t *rgb = malloc((size_t)width * height * 3);

		crc_init();
		backdrop(rgb, width, height);
		overlay_render_detail(&view, guarded.pixels);
		composite(rgb, width, guarded.pixels, OVERLAY_DETAIL_HEIGHT, scale, margin);
		write_png(png, rgb, width, height);
		free(rgb);
	}
}

int main(int argc, char **argv)
{
	test_window();
	test_pools();
	test_log();
	test_modes();
	test_panel(argc > 1 ? argv[1] : NULL, argc > 2);
	printf(failures ? "%d failures\n" : "overlay detail: all passed\n", failures);
	return failures != 0;
}
