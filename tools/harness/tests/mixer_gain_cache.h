/* Exact target-cache dependencies, independent of their key array indices. */
static void gain_cache_check(struct sdl_stream *stream, BOOL invalidated)
{
	float expected[7], actual[7];
	unsigned long before;
	BOOL expected_hrtf, actual_hrtf;
	struct hrtf_parameters hrtf;

	/* (the cache holds the pair, [5] and [6], too; the HRTF is off in these voices: the cache is not used for a voice it takes) */
	voice_gains_uncached(stream, &expected[0], &expected[1], &expected[5], &expected[2], &expected[3], &expected[4], &expected_hrtf, &hrtf);
	before = voice_gain_recalculations;
	voice_gains(stream, &actual[0], &actual[1], &actual[5], &actual[2], &actual[3], &actual[4], &actual_hrtf, &hrtf);
	CHECK(!expected_hrtf && !actual_hrtf, "the HRTF took a voice with it off");
	CHECK(voice_gain_recalculations - before == (unsigned long)invalidated, "gain-cache miss/hit differs");
	CHECK(!memcmp(expected, actual, sizeof(actual)), "cached gain/filter targets differ byte for byte");
}

#define GAIN_CHANGE(statement) do { statement; gain_cache_check(stream, TRUE); gain_cache_check(stream, FALSE); } while (0)

static void gain_cache_dependencies(void)
{
	short samples[400] = { 0 };
	struct sdl_stream *stream = voice_new(samples, 400, 1, 22050, 0);
	unsigned i;

	if (sizeof(unsigned long) == 4)
		CHECK(sizeof(struct voice_gain_inputs) + sizeof(stream->gain_cache_target) + sizeof(BOOL) == 176,
			"gain cache exceeded its Wii U per-stream bound");
	stream->has_3d = TRUE;
	stream->mode = DS3DMODE_NORMAL;
	stream->position[0] = 1; stream->position[2] = 3;
	stream->minimum_distance = 1; stream->maximum_distance = 50;
	stream->direct = -100; stream->direct_hf = -700;
	stream->room = -400; stream->room_hf = -1000;
	environment.lRoom = -1000; environment.lRoomHF = -100;
	gain_cache_check(stream, TRUE);
	for (i = 0; i < 100; i++)
		gain_cache_check(stream, FALSE);
	GAIN_CHANGE(stream->volume += 0.03125f);
	GAIN_CHANGE(stream->mix_left += 0.03125f);
	GAIN_CHANGE(stream->mix_right += 0.03125f);
	GAIN_CHANGE(stream->stereo_pan += 0.03125f);
	GAIN_CHANGE(stream->stereo_distance += 0.03125f);
	GAIN_CHANGE(stream->stereo_distance_fade += 0.03125f);
	GAIN_CHANGE(stream->position[0] += 0.03125f);
	GAIN_CHANGE(stream->position[1] += 0.03125f);
	GAIN_CHANGE(stream->position[2] += 0.03125f);
	GAIN_CHANGE(stream->minimum_distance += 0.03125f);
	GAIN_CHANGE(stream->maximum_distance += 0.03125f);
	GAIN_CHANGE(stream->room_rolloff_factor += 0.03125f);
	GAIN_CHANGE(master_volume += 0.03125f);
	GAIN_CHANGE(listener.position[0] += 0.03125f);
	GAIN_CHANGE(listener.position[1] += 0.03125f);
	GAIN_CHANGE(listener.position[2] += 0.03125f);
	GAIN_CHANGE(listener.front[0] += 0.03125f);
	GAIN_CHANGE(listener.front[1] += 0.03125f);
	GAIN_CHANGE(listener.front[2] += 0.03125f);
	GAIN_CHANGE(listener.top[0] += 0.03125f);
	GAIN_CHANGE(listener.top[1] += 0.03125f);
	GAIN_CHANGE(listener.top[2] += 0.03125f);
	GAIN_CHANGE(listener.rolloff_factor += 0.03125f);
	GAIN_CHANGE(environment.flHFReference += 0.03125f);
	GAIN_CHANGE(environment.flRoomRolloffFactor += 0.03125f);
	GAIN_CHANGE(stream->channels = 2);
	GAIN_CHANGE(stream->has_3d = !stream->has_3d);
	GAIN_CHANGE(stream->mode = DS3DMODE_HEADRELATIVE);
	GAIN_CHANGE(stream->stereo_positioned = !stream->stereo_positioned);
	GAIN_CHANGE(stream->direct += 1);
	GAIN_CHANGE(stream->direct_hf += 1);
	GAIN_CHANGE(stream->room += 1);
	GAIN_CHANGE(stream->room_hf += 1);
	GAIN_CHANGE(reverb_enabled = !reverb_enabled);
	GAIN_CHANGE(environment.lRoom += 1);
	GAIN_CHANGE(environment.lRoomHF += 1);
	/* Exercise inputs while the corresponding branch is active too. */
	stream->has_3d = TRUE; stream->mode = DS3DMODE_NORMAL; reverb_enabled = TRUE;
	gain_cache_check(stream, TRUE);
	GAIN_CHANGE(listener.position[0] += 2.0f);
	GAIN_CHANGE(listener.front[0] = -0.25f);
	GAIN_CHANGE(listener.top[2] = 0.5f);
	GAIN_CHANGE(environment.flHFReference = 3500.0f);
	GAIN_CHANGE(environment.lRoomHF = -600);
	GAIN_CHANGE(stream->mode = DS3DMODE_HEADRELATIVE);
	stream->has_3d = FALSE; stream->channels = 2; stream->stereo_positioned = TRUE;
	gain_cache_check(stream, TRUE);
	GAIN_CHANGE(stream->stereo_pan = -0.25f);
	GAIN_CHANGE(stream->stereo_distance = 8);
	GAIN_CHANGE(stream->stereo_distance_fade = 0.5f);
	GAIN_CHANGE(stream->room = -600);
	GAIN_CHANGE(stream->direct_hf = -300);
	GAIN_CHANGE(reverb_enabled = FALSE);
	GAIN_CHANGE(stream->stereo_positioned = FALSE);
	GAIN_CHANGE(stream->mix_left = 0.125f);
	GAIN_CHANGE(master_volume = 0.25f);
	/* Cursor, history, pitch and ramp state are not gain inputs. */
	stream->cursor++; stream->phase += 0.25; stream->current_left = 0.3f;
	stream->history_count++; stream->frequency++; stream->headroom = 0.5f;
	listener.distance_factor += 1.0f;
	gain_cache_check(stream, FALSE);
	GAIN_CHANGE(stream->mix_left = 0.0f);
	GAIN_CHANGE(stream->mix_left = -0.0f);
	{
		union { unsigned bits; float value; } nan = { 0x7fc12345 };

		GAIN_CHANGE(stream->mix_left = nan.value);
	}
	voice_delete(stream);
}

static void gain_cache_config(void)
{
	short samples[400] = { 0 };
	struct sdl_stream *stream = voice_new(samples, 400, 1, 22050, 0);
	unsigned before;

	stream->has_3d = TRUE; stream->minimum_distance = 1; stream->maximum_distance = 50;
	stream->position[2] = 3; stream->room = -100; stream->room_hf = -300;
	environment.lRoom = -1000; environment.lRoomHF = -200;
	gain_cache_check(stream, TRUE);
	gain_config_volume = 0.25; gain_config_reverb = FALSE;
	before = gain_config_lock_calls;
	gain_config_do_work();
	CHECK(gain_config_lock_calls == before + 1, "settings published without the mixer lock");
	CHECK(gain_config_reads == 4 && !gain_config_read_locked, "config read while holding mixer lock");
	CHECK(master_volume == 0.25f && !reverb_enabled, "config settings were not published together");
	gain_cache_check(stream, TRUE);
	gain_cache_check(stream, FALSE);
	gain_config_do_work();
	CHECK(gain_config_reads == 4 && gain_config_lock_calls == before + 1, "unchanged config was republished");
	gain_config_serial++;
	gain_config_volume = 0.75; gain_config_reverb = TRUE;
	gain_config_do_work();
	CHECK(gain_config_reads == 8 && !gain_config_read_locked && gain_config_completions == 3,
		"config reload/completions changed");
	CHECK(master_volume == 0.75f && reverb_enabled, "new config settings missing");
	gain_cache_check(stream, TRUE);
	voice_delete(stream);
}

#undef GAIN_CHANGE
