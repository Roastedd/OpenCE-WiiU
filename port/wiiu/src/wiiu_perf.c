/* wiiu_perf.c: the arithmetic of the detailed overlay's numbers (wiiu_perf.h). No console calls: it is compiled on the host for its test. */

#include "wiiu_perf.h"

#include <stdio.h>
#include <string.h>

uint32_t perf_delta(uint32_t before, uint32_t after)
{
	return (uint32_t)(after - before);
}

void perf_meter_reset(struct perf_meter *meter)
{
	memset(meter, 0, sizeof(*meter));
}

void perf_meter_frame(struct perf_meter *meter, const struct perf_frame *frame)
{
	meter->frames++;
	meter->frame_sum += frame->frame_ms;
	if (frame->frame_ms > meter->worst)
		meter->worst = frame->frame_ms;
	meter->flip_sum += frame->flip_wait_ms;
	meter->busy_sum += frame->gx2_busy_ms;
	meter->draws += frame->draws;
}

void perf_meter_gpu(struct perf_meter *meter, float gpu_ms)
{
	if (gpu_ms <= 0.0f)
		return;
	meter->gpu_reads++;
	meter->gpu_sum += gpu_ms;
}

static float per_frame_ms(uint32_t ticks, double ticks_per_ms, double frames)
{
	return (float)((double)ticks / ticks_per_ms / frames);
}

static float at_least_zero(float value)
{
	return value < 0.0f ? 0.0f : value;
}

int perf_meter_close(struct perf_meter *meter, const struct perf_totals *now, double ticks_per_ms, const struct perf_state *state,
	const struct perf_pool pools[PERF_POOL_COUNT], struct perf_view *view)
{
	const struct perf_totals *base = &meter->base;
	double game_frames, window_ticks, frames = (double)meter->frames;
	uint32_t pass[PERF_PASSES];
	unsigned i;
	int result = 0;

	memset(view, 0, sizeof(*view));
	if (meter->have_base && meter->frames && ticks_per_ms > 0.0)
	{
		const uint32_t game_delta = perf_delta(base->game_frames, now->game_frames);
		game_frames = game_delta ? (double)game_delta : frames;
		view->valid = 1;
		view->frames = meter->frames;
		view->seconds = (float)(meter->frame_sum / 1000.0);
		view->frame_ms = (float)(meter->frame_sum / frames);
		view->fps = view->frame_ms > 0.0f ? 1000.0f / view->frame_ms : 0.0f;
		view->worst_ms = (float)meter->worst;
		window_ticks = meter->frame_sum * ticks_per_ms;
		{
			/* the game thread's frame, less what it waited: Present's wait for a refresh and the 30 Hz throttle (the display's), the waits for the GX2 thread */
			const float loop = per_frame_ms(perf_delta(base->game_work, now->game_work), ticks_per_ms, game_frames) +
				per_frame_ms(perf_delta(base->game_present, now->game_present), ticks_per_ms, game_frames);
			const float slot_wait = per_frame_ms(perf_delta(base->game_slot_wait, now->game_slot_wait), ticks_per_ms, game_frames);
			const float hold = per_frame_ms(perf_delta(base->game_throttle, now->game_throttle), ticks_per_ms, game_frames) +
				per_frame_ms(perf_delta(base->game_vsync_hold, now->game_vsync_hold), ticks_per_ms, game_frames);
			const float flip = (float)(meter->flip_sum / frames);

			view->gpu_wait_ms = slot_wait;
			view->vsync_ms = flip > hold ? flip : hold;
			view->work_ms = at_least_zero(loop - slot_wait - hold);
		}
		view->gpu_ms = meter->gpu_reads ? (float)(meter->gpu_sum / (double)meter->gpu_reads) : 0.0f;
		view->sim_wait_ms = now->overlap_enabled ? per_frame_ms(perf_delta(base->overlap_wait, now->overlap_wait), ticks_per_ms, game_frames) : 0.0f;
		view->sim_ms = per_frame_ms(perf_delta(base->game_sim, now->game_sim), ticks_per_ms, game_frames) + view->sim_wait_ms;
		view->draw_ms = per_frame_ms(perf_delta(base->game_render, now->game_render), ticks_per_ms, game_frames);
		view->gx2_ms = (float)(meter->busy_sum / frames);
		view->gx2_busy_percent = meter->frame_sum > 0.0 ? (float)(100.0 * meter->busy_sum / meter->frame_sum) : 0.0f;
		{
			const uint32_t audio = perf_delta(base->mixer_ticks, now->mixer_ticks) + perf_delta(base->decoder_ticks, now->decoder_ticks);

			view->audio_ms = per_frame_ms(audio, ticks_per_ms, game_frames);
			view->audio_percent = window_ticks > 0.0 ? (float)(100.0 * (double)audio / window_ticks) : 0.0f;
		}
		view->tick_thread_ms = now->overlap_enabled ? per_frame_ms(perf_delta(base->overlap_job, now->overlap_job), ticks_per_ms, game_frames) : 0.0f;
		view->tick_thread_busy_percent = now->overlap_enabled && window_ticks > 0.0 ?
			(float)(100.0 * (double)perf_delta(base->overlap_job, now->overlap_job) / window_ticks) : 0.0f;
		view->ticks_per_second = view->seconds > 0.0f ? (float)perf_delta(base->game_ticks, now->game_ticks) / view->seconds : 0.0f;
		{
			/* (a job per frame; a frame runs a tick or two, or none: the time a tick is the job's time over the ticks the window ran) */
			const uint32_t ticks = perf_delta(base->game_ticks, now->game_ticks);

			view->tick_thread_tick_ms = now->overlap_enabled && ticks ? (float)((double)perf_delta(base->overlap_job, now->overlap_job) / ticks_per_ms / (double)ticks) : 0.0f;
		}
		if (!now->overlap_enabled)
			view->overlap = 0;
		else
			view->overlap = perf_delta(base->overlap_serial, now->overlap_serial) > perf_delta(base->overlap_overlapped, now->overlap_overlapped) ? 2 : 1;
		for (i = 0; i < PERF_PASSES; i++)
			pass[i] = perf_delta(base->pass[i], now->pass[i]);
		/* (the passes, grouped: visibility; begin, which includes the wait for room in the GX2 thread's queue; the objects, with their lightmaps, shadows and
		specular shine and the sky; the decals and the detail objects; the transparent geometry and the effects; the HUD) */
		view->draw_part_ms[0] = per_frame_ms(pass[0], ticks_per_ms, game_frames);
		view->draw_part_ms[1] = per_frame_ms(pass[1], ticks_per_ms, game_frames);
		view->draw_part_ms[2] = per_frame_ms(pass[2] + pass[3] + pass[4] + pass[5] + pass[7], ticks_per_ms, game_frames);
		view->draw_part_ms[3] = per_frame_ms(pass[6] + pass[10], ticks_per_ms, game_frames);
		view->draw_part_ms[4] = per_frame_ms(pass[8] + pass[9], ticks_per_ms, game_frames);
		view->draw_part_ms[5] = per_frame_ms(pass[11], ticks_per_ms, game_frames);
		view->draws = (float)(meter->draws / frames);
		{
			const uint32_t considered = perf_delta(base->objects_considered, now->objects_considered);
			const uint32_t found = perf_delta(base->objects_found, now->objects_found);

			const uint32_t drawn = perf_delta(base->objects_drawn, now->objects_drawn);

			view->objects_found = (float)((double)found / game_frames);
			view->objects_drawn = (float)((double)drawn / game_frames);
			view->objects_culled = (float)((double)(considered > drawn ? considered - drawn : 0) / game_frames);
		}
		if (state)
			view->state = *state;
		if (pools)
			memcpy(view->pool, pools, sizeof(view->pool));
		result = 1;
	}
	/* the next window begins here */
	meter->base = *now;
	meter->have_base = 1;
	meter->frames = meter->gpu_reads = 0;
	meter->frame_sum = meter->worst = meter->flip_sum = meter->busy_sum = meter->gpu_sum = meter->draws = 0.0;
	return result;
}

/* ---------- the five seconds' frame rate */

void perf_average_reset(struct perf_average *average)
{
	memset(average, 0, sizeof(*average));
}

void perf_average_push(struct perf_average *average, unsigned frames, float seconds)
{
	average->frames[average->head] = (float)frames;
	average->seconds[average->head] = seconds;
	average->head = (average->head + 1) % PERF_AVERAGE_WINDOWS;
	if (average->count < PERF_AVERAGE_WINDOWS)
		average->count++;
}

float perf_average_fps(const struct perf_average *average)
{
	double frames = 0.0, seconds = 0.0;
	unsigned i;

	for (i = 0; i < average->count; i++)
	{
		frames += average->frames[i];
		seconds += average->seconds[i];
	}
	return seconds > 0.0 ? (float)(frames / seconds) : 0.0f;
}

/* ---------- pools */

const char *perf_pool_name(int pool)
{
	static const char *const names[PERF_POOL_COUNT] = { "TEX", "GEOM", "SOUND", "STATE", "TAGS", "MEM2", "EDRAM" };

	return pool >= 0 && pool < PERF_POOL_COUNT ? names[pool] : "?";
}

float perf_pool_percent(const struct perf_pool *pool)
{
	if (!pool->known || !pool->capacity_kib)
		return 0.0f;
	return 100.0f * (float)pool->used_kib / (float)pool->capacity_kib;
}

int perf_pool_level(const struct perf_pool *pool)
{
	/* (compared as integers: 85 percent of 20 MiB is 17408 KiB exactly) */
	const uint64_t used = (uint64_t)(pool->lru ? pool->hot_kib : pool->used_kib) * 100u, capacity = pool->capacity_kib;

	if (!pool->known || !pool->capacity_kib || pool->fixed)
		return PERF_LEVEL_OK;
	if (pool->full || used > capacity * 95u)
		return PERF_LEVEL_RED;
	if (used > capacity * 85u)
		return PERF_LEVEL_AMBER;
	return PERF_LEVEL_OK;
}

void perf_mib_text(char *out, size_t size, uint32_t kib)
{
	uint32_t whole = kib / 1024u;
	uint32_t tenths = ((kib % 1024u) * 10u + 512u) / 1024u;

	if (tenths == 10)
	{
		whole++;
		tenths = 0;
	}
	snprintf(out, size, "%u.%u", (unsigned)whole, (unsigned)tenths);
}

/* ---------- the log's lines */

static const char *overlap_word(int overlap)
{
	return overlap == 1 ? "on" : overlap == 2 ? "serial" : "off";
}

/* a pool as "13.4/22.0" (and its hot part), or "?" if it was not read */
static void pool_text(char *out, size_t size, const struct perf_pool *pool, int hot)
{
	char used[16], capacity[16], touched[16];

	if (!pool->known)
	{
		snprintf(out, size, "?");
		return;
	}
	perf_mib_text(used, sizeof(used), pool->used_kib);
	perf_mib_text(capacity, sizeof(capacity), pool->capacity_kib);
	if (hot && pool->hot_kib)
	{
		perf_mib_text(touched, sizeof(touched), pool->hot_kib);
		snprintf(out, size, "%s/%s (hot %s)", used, capacity, touched);
	}
	else
		snprintf(out, size, "%s/%s", used, capacity);
}

int perf_log_line(char *out, size_t size, const struct perf_view *view, int index)
{
	char a[40], b[40], c[40], d[40], e[40], f[48], g[40];
	const struct perf_state *state = &view->state;

	if (index < 0 || index >= PERF_LOG_LINES)
		return 0;
	if (!view->valid)
	{
		snprintf(out, size, "overlay%s: no window yet", index == 3 ? " pools" : "");
		return 1;
	}
	if (index == 0)
	{
		if (view->gpu_ms > 0.0f)
			snprintf(a, sizeof(a), "%.1f", (double)view->gpu_ms);
		else
			snprintf(a, sizeof(a), "--");
		snprintf(out, size, "overlay: frame %.1f ms (%.1f fps, 5 s %.1f, 1%% low %.1f) worst %.1f | work %.1f gpu %s gpu wait %.1f vsync %.1f | sim %.1f (tick wait %.1f) draw %.1f gx2 %.1f (%.0f%%) audio %.1f",
			(double)view->frame_ms, (double)view->fps, (double)view->fps_average, (double)view->low_fps, (double)view->worst_ms, (double)view->work_ms, a, (double)view->gpu_wait_ms, (double)view->vsync_ms,
			(double)view->sim_ms, (double)view->sim_wait_ms, (double)view->draw_ms, (double)view->gx2_ms, (double)view->gx2_busy_percent, (double)view->audio_ms);
	}
	else if (index == 1)
	{
		snprintf(out, size, "overlay: tick thread %.1f ms/job %.1f/tick (%.0f%%) overlap %s %.0f ticks/s | draws %.0f objects %.0f drawn %.0f culled",
			(double)view->tick_thread_ms, (double)view->tick_thread_tick_ms, (double)view->tick_thread_busy_percent, overlap_word(view->overlap), (double)view->ticks_per_second,
			(double)view->draws, (double)view->objects_drawn, (double)view->objects_culled);
	}
	else if (index == 2)
	{
		if (state->governor_step >= 0)
			snprintf(a, sizeof(a), "gov %d/%d", state->governor_step, state->governor_max);
		else
			snprintf(a, sizeof(a), "gov -");
		if (state->drs_on)
			snprintf(b, sizeof(b), "drs %u/%u step %d", state->drs_lines, state->drs_full_lines, state->drs_step);
		else
			snprintf(b, sizeof(b), "drs off");
		snprintf(out, size, "overlay: draw passes vis %.1f begin %.1f obj %.1f dec %.1f fx %.1f hud %.1f | %ux%u %s %dhz %s",
			(double)view->draw_part_ms[0], (double)view->draw_part_ms[1], (double)view->draw_part_ms[2], (double)view->draw_part_ms[3], (double)view->draw_part_ms[4],
			(double)view->draw_part_ms[5], state->render_width, state->render_height, b, state->interpolation ? 60 : 30, a);
	}
	else
	{
		char free_text[16];

		pool_text(a, sizeof(a), &view->pool[PERF_POOL_TEXTURE], 1);
		pool_text(b, sizeof(b), &view->pool[PERF_POOL_GEOMETRY], 0);
		pool_text(c, sizeof(c), &view->pool[PERF_POOL_SOUND], 0);
		pool_text(d, sizeof(d), &view->pool[PERF_POOL_STATE], 0);
		pool_text(e, sizeof(e), &view->pool[PERF_POOL_TAGS], 0);
		pool_text(g, sizeof(g), &view->pool[PERF_POOL_EDRAM], 0);
		if (view->pool[PERF_POOL_MEM2].known && view->pool[PERF_POOL_MEM2].reserved)
			snprintf(f, sizeof(f), "reserved");
		else if (view->pool[PERF_POOL_MEM2].known)
		{
			char largest[16];

			perf_mib_text(free_text, sizeof(free_text), view->pool[PERF_POOL_MEM2].capacity_kib - view->pool[PERF_POOL_MEM2].used_kib);
			perf_mib_text(largest, sizeof(largest), view->pool[PERF_POOL_MEM2].largest_kib);
			snprintf(f, sizeof(f), "free %s (largest %s)", free_text, largest);
		}
		else
			snprintf(f, sizeof(f), "?");
		snprintf(out, size, "overlay pools (MiB, used/capacity): tex %s geom %s sound %s state %s tags %s mem2 %s edram %s", a, b, c, d, e, f, g);
	}
	return 1;
}
