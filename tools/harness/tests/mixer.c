/*
MIXER.C (test)

The real mixer of port/linux/src/dsound_sdl.c (its resampler, voices,
reverb and limiter; test_mixer.py takes them as under_test.inc) on known
signals:
	- the resampler against the same Kaiser-windowed sinc worked out in
	  double precision, tap by tap: noise, sines and an impulse at the
	  game's rates (22 and 44 kHz voices, others pitched up and down, a 48
	  kHz one), so a faster resampler is still the same low pass;
	- sines through it measured as the ear would hear them: the gain across
	  the band, and what else comes out (images and aliases) under the tone;
	- a voice out of earshot, which is not resampled, moves on as one heard
	  does, and is heard the same once it comes into earshot;
	- mono voices heard as stereo ones with both channels the same are.
"render" writes voices at each rate and a scene's whole mix as raw floats
(test_mixer.py compares the resampler adding four taps at a time with it
adding tap by tap), "metrics" prints the figures and "bench" times a game's
worth of voices.
*/

#include "harness.h"

#include <math.h>
#include <pthread.h>

typedef int BOOL;
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned long ULONG;
typedef void *LPVOID;
typedef void (*LPFNXMEDIAOBJECTCALLBACK)(void *, void *, DWORD);
typedef struct { void *lpVtbl; } IDirectSoundStream;
typedef struct { void *pvBuffer; DWORD dwMaxSize; } XMEDIAPACKET;
typedef struct
{
	LONG lRoom, lRoomHF;
	float flRoomRolloffFactor, flDecayTime, flDecayHFRatio;
	LONG lReflections;
	float flReflectionsDelay;
	LONG lReverb;
	float flReverbDelay, flDiffusion, flDensity, flHFReference;
} DSI3DL2LISTENER;
#define DSBVOLUME_MIN (-10000)
#define DS3DMODE_NORMAL 0x00000000
#define DS3DMODE_HEADRELATIVE 0x00000001
#define DS3DMODE_DISABLE 0x00000002

/* the players' voices (voice_audio.c): none */
static void voice_audio_mix(float *output, unsigned long frames)
{
	(void)output;
	(void)frames;
}

#include "under_test.inc"

#define PI 3.14159265358979323846
#define CHUNK 512 /* frames a mix, as SDL asks on Linux */

/* ---------- voices */

/* a voice of frames frames of channels channels at rate, played at frequency
(0: its rate), a 2D voice heard at full volume in both speakers */
static struct sdl_stream *voice_new(const short *samples, unsigned long frames, unsigned long channels,
	unsigned long rate, unsigned long frequency)
{
	struct sdl_stream *stream = calloc(1, sizeof(*stream));

	stream->channels = channels;
	stream->sample_rate = rate;
	stream->frequency = frequency;
	stream->volume = 1.0f;
	stream->mix_left = stream->mix_right = 1.0f;
	stream->room = stream->room_hf = DSBVOLUME_MIN;
	stream->packets[0].samples = malloc(frames * channels * sizeof(short));
	memcpy(stream->packets[0].samples, samples, frames * channels * sizeof(short));
	stream->packets[0].frames = frames;
	stream->packets[0].ready = TRUE;
	stream->packet_count = 1;
	resampler_reset(stream);
	return stream;
}

static void voice_delete(struct sdl_stream *stream)
{
	free(stream->packets[0].samples);
	free(stream);
}

/* frames of the voice mixed alone, CHUNK at a time: the left channel, or
both (stereo: interleaved) */
static void render(struct sdl_stream *stream, float *output, unsigned long frames, BOOL stereo)
{
	static float buffer[CHUNK * 2], send[CHUNK];
	unsigned long done, frame;

	for (done = 0; done < frames; done += CHUNK)
	{
		unsigned long count = frames - done < CHUNK ? frames - done : CHUNK;

		memset(buffer, 0, sizeof(buffer));
		memset(send, 0, sizeof(send));
		mix_voice(stream, buffer, send, count);
		for (frame = 0; frame < count; frame++)
		{
			if (stereo)
			{
				output[(done + frame) * 2] = buffer[frame * 2];
				output[(done + frame) * 2 + 1] = buffer[frame * 2 + 1];
			}
			else
			{
				output[done + frame] = buffer[frame * 2];
			}
		}
	}
}

/* ---------- the reference: the same low pass in double precision */

static double reference_kernel(double distance)
{
	double edge, angle, sinc;

	distance = fabs(distance);
	if (distance >= RESAMPLER_ZERO_CROSSINGS)
		return 0.0;
	edge = distance / RESAMPLER_ZERO_CROSSINGS;
	angle = PI * RESAMPLER_CUTOFF * distance;
	sinc = distance > 0.0 ? sin(angle) / angle : 1.0;
	return RESAMPLER_CUTOFF * sinc * bessel_i0(RESAMPLER_KAISER_BETA * sqrt(1.0 - edge * edge)) /
		bessel_i0(RESAMPLER_KAISER_BETA);
}

/* output frame n of source (frames, mono, full scale 1) resampled at step:
the frames around source time n step, weighted by the low pass (narrowed for a
step over 1, as the mixer's) */
static double reference_frame(const double *source, long frames, double step, unsigned long n)
{
	double scale = step > 1.0 ? 1.0 / (step < RESAMPLER_MAXIMUM_STRETCH ? step : RESAMPLER_MAXIMUM_STRETCH) : 1.0;
	double time = (double)n * step, sum = 0.0;
	long center = (long)floor(time), width = (long)ceil(RESAMPLER_ZERO_CROSSINGS / scale), k;

	for (k = center + 1 - width; k <= center + width; k++)
	{
		if (k >= 0 && k < frames)
			sum += source[k] * scale * reference_kernel((time - (double)k) * scale);
	}
	return sum;
}

/* ---------- signals */

static unsigned long random_state = 1;

static double random_uniform(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return (double)((random_state >> 8) & 0xffffff) / 16777216.0 * 2.0 - 1.0;
}

/* frames of a signal as 16-bit samples (and as the doubles they are) */
static void quantize(const double *signal, short *samples, double *exact, unsigned long frames)
{
	unsigned long frame;

	for (frame = 0; frame < frames; frame++)
	{
		long value = lround(signal[frame] * 32767.0);

		samples[frame] = (short)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
		exact[frame] = samples[frame] / 32768.0;
	}
}

/* ---------- figures */

/* the amplitude of a sine of frequency cycles a frame in signal[first, last),
and the power of the rest relative to the sine's, in dB */
static void tone_fit(const float *signal, unsigned long first, unsigned long last, double frequency,
	double *amplitude, double *rest_db)
{
	double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0, a, b, determinant, rest = 0, tone = 0;
	unsigned long n;

	for (n = first; n < last; n++)
	{
		double s = sin(2.0 * PI * frequency * n), c = cos(2.0 * PI * frequency * n);

		ss += s * s;
		sc += s * c;
		cc += c * c;
		ys += signal[n] * s;
		yc += signal[n] * c;
	}
	determinant = ss * cc - sc * sc;
	a = (ys * cc - yc * sc) / determinant;
	b = (yc * ss - ys * sc) / determinant;
	for (n = first; n < last; n++)
	{
		double fit = a * sin(2.0 * PI * frequency * n) + b * cos(2.0 * PI * frequency * n);

		rest += (signal[n] - fit) * (signal[n] - fit);
		tone += fit * fit;
	}
	*amplitude = sqrt(a * a + b * b);
	*rest_db = 10.0 * log10(rest / tone + 1e-30);
}

static double decibels(double ratio)
{
	return 20.0 * log10(ratio + 1e-30);
}

/* the mixer's output against the reference's for a signal at rate played at
frequency: the error's power relative to the reference's, in dB, and its
largest relative to the largest sample */
static void against_reference(const double *signal, unsigned long frames, unsigned long rate, unsigned long frequency,
	double *error_db, double *peak_error_db)
{
	short *samples = malloc(frames * sizeof(short));
	double *exact = malloc(frames * sizeof(double));
	double step = (double)(frequency ? frequency : rate) / OUTPUT_RATE, error = 0, power = 0, peak = 0, worst = 0;
	unsigned long outputs = (unsigned long)(frames / step), n;
	float *output = calloc(outputs, sizeof(float));
	struct sdl_stream *stream;

	quantize(signal, samples, exact, frames);
	stream = voice_new(samples, frames, 1, rate, frequency);
	render(stream, output, outputs, FALSE);
	for (n = 0; n < outputs; n++)
	{
		double expected = reference_frame(exact, (long)frames, step, n);
		double difference = output[n] - expected;

		error += difference * difference;
		power += expected * expected;
		peak = fabs(expected) > peak ? fabs(expected) : peak;
		worst = fabs(difference) > worst ? fabs(difference) : worst;
	}
	*error_db = 10.0 * log10(error / power + 1e-30);
	*peak_error_db = decibels(worst / peak);
	voice_delete(stream);
	free(output);
	free(exact);
	free(samples);
}

/* a sine of hertz at rate, played at frequency: its gain through the mixer
(dB) and the power of everything else that comes out, relative to it (dB); a
sine the voice plays above the output's band is all "else", relative to the
sine put in */
static void sine_figures(double hertz, unsigned long rate, unsigned long frequency, double *gain_db, double *rest_db)
{
	unsigned long frames = rate, outputs, n;
	double step = (double)(frequency ? frequency : rate) / OUTPUT_RATE, played = hertz * step * OUTPUT_RATE / rate;
	double *signal = malloc(frames * sizeof(double)), *exact = malloc(frames * sizeof(double)), amplitude;
	short *samples = malloc(frames * sizeof(short));
	float *output;
	struct sdl_stream *stream;

	for (n = 0; n < frames; n++)
		signal[n] = 0.5 * sin(2.0 * PI * hertz * n / rate);
	quantize(signal, samples, exact, frames);
	outputs = (unsigned long)(frames / step);
	output = calloc(outputs, sizeof(float));
	stream = voice_new(samples, frames, 1, rate, frequency);
	render(stream, output, outputs, FALSE);
	if (played < 0.5 * OUTPUT_RATE)
	{
		tone_fit(output, outputs / 4, outputs * 3 / 4, played / OUTPUT_RATE, &amplitude, rest_db);
		*gain_db = decibels(amplitude / 0.5);
	}
	else
	{
		double power = 0;

		for (n = outputs / 4; n < outputs * 3 / 4; n++)
			power += (double)output[n] * output[n];
		*gain_db = -999.0;
		*rest_db = 10.0 * log10(power / (outputs / 2) / (0.5 * 0.5 / 2) + 1e-30);
	}
	voice_delete(stream);
	free(output);
	free(samples);
	free(exact);
	free(signal);
}

/* a logarithmic sweep from 20 Hz to top at rate over the voice: what comes out
against the sweep itself at the output's times (an ideal resampler), in dB */
static double sweep_figure(unsigned long rate, double top)
{
	unsigned long frames = 2 * rate, outputs = (unsigned long)((double)frames * OUTPUT_RATE / rate), n;
	double *signal = malloc(frames * sizeof(double)), *exact = malloc(frames * sizeof(double));
	double duration = (double)frames / rate, k = log(top / 20.0), error = 0, power = 0;
	short *samples = malloc(frames * sizeof(short));
	float *output = calloc(outputs, sizeof(float));
	struct sdl_stream *stream;

	for (n = 0; n < frames; n++)
	{
		double t = (double)n / rate;

		signal[n] = 0.5 * sin(2.0 * PI * 20.0 * duration / k * (exp(t / duration * k) - 1.0));
	}
	quantize(signal, samples, exact, frames);
	stream = voice_new(samples, frames, 1, rate, 0);
	render(stream, output, outputs, FALSE);
	for (n = OUTPUT_RATE / 10; n + OUTPUT_RATE / 10 < outputs; n++)
	{
		double t = (double)n / OUTPUT_RATE;
		double ideal = 0.5 * sin(2.0 * PI * 20.0 * duration / k * (exp(t / duration * k) - 1.0));

		error += (output[n] - ideal) * (output[n] - ideal);
		power += ideal * ideal;
	}
	voice_delete(stream);
	free(output);
	free(samples);
	free(exact);
	free(signal);
	return 10.0 * log10(error / power + 1e-30);
}

static double *noise(unsigned long frames)
{
	double *signal = malloc(frames * sizeof(double));
	unsigned long n;

	random_state = 1;
	for (n = 0; n < frames; n++)
		signal[n] = 0.5 * random_uniform();
	return signal;
}

/* ---------- a scene: a game's worth of voices, for "render" and "bench" */

/* voices: mono 22 kHz sounds in the world (indoors sending to the reverb,
half muffled, silent ones out of earshot), stereo 44 kHz music, a few
pitched; each plays for seconds */
static void scene(unsigned long voices, unsigned long silent, BOOL indoor, double seconds)
{
	unsigned long frames = 22050 * 4, v, n;
	short *mono = malloc(frames * sizeof(short)), *stereo = malloc(frames * 2 * sizeof(short));

	random_state = 7;
	for (n = 0; n < frames; n++)
	{
		mono[n] = (short)(8000 * random_uniform());
		stereo[n * 2] = (short)(8000 * random_uniform());
		stereo[n * 2 + 1] = (short)(8000 * random_uniform());
	}
	if (indoor)
	{
		DSI3DL2LISTENER room = { -1000, -100, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f };

		environment = room;
		environment_serial++;
	}
	resampler_initialize();
	resampler_phases_initialize();
	reverb_initialize();
	for (v = 0; v < voices + silent; v++)
	{
		struct sdl_stream *stream;

		if (v == 0)
			stream = voice_new(stereo, frames, 2, 44100, 0);
		else
			stream = voice_new(mono, frames, 1, 22050, v % 5 == 0 ? 22050 + 2000 * (v % 3) : 0);
		if (v)
		{
			stream->has_3d = TRUE;
			stream->mode = DS3DMODE_NORMAL;
			stream->position[0] = (float)(v % 7) - 3.0f;
			stream->position[2] = v < voices ? 2.0f : 1000.0f;
			stream->minimum_distance = 1.0f;
			stream->maximum_distance = v < voices ? 50.0f : 100.0f;
			stream->direct = v < voices ? 0 : DSBVOLUME_MIN;
			stream->direct_hf = v % 2 ? -500 : 0;
			stream->room = v < voices ? 0 : DSBVOLUME_MIN;
			stream->room_hf = 0;
		}
		/* a packet as long as the scene */
		free(stream->packets[0].samples);
		stream->packets[0].frames = (unsigned long)(seconds * 50000);
		stream->packets[0].samples = malloc(stream->packets[0].frames * stream->channels * sizeof(short));
		for (n = 0; n < stream->packets[0].frames * stream->channels; n++)
			stream->packets[0].samples[n] = (v == 0 ? stereo : mono)[n % frames];
		stream->next = streams;
		streams = stream;
	}
	free(mono);
	free(stereo);
}

/* the scene's voices out of earshot come into it */
static void scene_approach(void)
{
	struct sdl_stream *stream;

	for (stream = streams; stream; stream = stream->next)
	{
		if (stream->direct == DSBVOLUME_MIN)
		{
			stream->position[2] = 3.0f;
			stream->direct = stream->room = 0;
		}
	}
}

static void bench(unsigned long voices, unsigned long silent, BOOL indoor, double seconds)
{
	unsigned long chunks = (unsigned long)(seconds * OUTPUT_RATE / CHUNK), chunk;
	static float output[CHUNK * 2];
	struct timespec start, end;
	double elapsed;

	scene(voices, silent, indoor, seconds);
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (chunk = 0; chunk < chunks; chunk++)
		mix(output, CHUNK);
	clock_gettime(CLOCK_MONOTONIC, &end);
	elapsed = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) * 1e-9;
	printf("%lu voices (%lu out of earshot)%s: %.2f%% of a core, %.1f ns a frame\n", voices, silent,
		indoor ? " indoors" : "", 100.0 * elapsed / seconds, elapsed * 1e9 / ((double)chunks * CHUNK));
}

#ifdef RESAMPLER_GAIN_CACHE_TEST
#include "mixer_gain_cache.h"
#endif

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	static const unsigned long rates[][2] = {
		{ 22050, 0 }, { 44100, 0 }, { 48000, 0 }, { 22050, 17000 }, { 22050, 30000 }, { 44100, 60000 },
		{ 44100, 96000 }, { 32000, 0 },
	};
	unsigned long index;

	resampler_initialize();
	resampler_phases_initialize();

#ifdef RESAMPLER_GAIN_CACHE_TEST
	CASE("gain-cache") { gain_cache_dependencies(); return 0; }
	CASE("gain-config") { gain_cache_config(); return 0; }
#endif
	CASE("gain-render")
	{
		float output[CHUNK * 2];
		unsigned step;

		scene(8, 2, TRUE, 1.0);
		for (step = 0; step < 48; step++)
		{
			struct sdl_stream *stream;
			float volume = step % 3 ? 0.75f : 0.25f;
			BOOL enabled = step % 4 != 0;

			for (stream = streams; stream; stream = stream->next)
			{
				stream->volume = step % 5 ? 0.5f : 1.0f;
				stream->position[0] += 0.03125f;
				stream->direct_hf = step % 2 ? -600 : -100;
				if (stream->channels == 2)
				{
					stream->stereo_positioned = step % 3 != 0;
					stream->stereo_pan = step % 2 ? -0.25f : 0.5f;
					stream->stereo_distance = 3.0f;
					stream->stereo_distance_fade = 0.5f;
				}
			}
			listener.position[2] = (float)(step % 3) * 0.125f;
			environment.flHFReference = step % 2 ? 3500.0f : 5000.0f;
			environment.lRoomHF = step % 2 ? -100 : -600;
			environment_serial++;
#ifdef VOICE_GAIN_CACHE
			mixer_settings_publish(volume, enabled);
#else
			master_volume = volume; reverb_enabled = enabled;
#endif
			mix(output, CHUNK);
			fwrite(output, sizeof(float), CHUNK * 2, stdout);
		}
		return 0;
	}

#ifdef RESAMPLER_CACHE_TEST
	CASE("cache-pitch-gate")
	{
		static const unsigned long common[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };
		static const unsigned long uncommon[] = { 16051, 17003, 20011, 22049, 22051, 25013, 30047, 44099, 48001, 60000 };
		short samples[4096];
		float output[128];
		struct sdl_stream *stream;
		unsigned rate, i;

		for (i = 0; i < 4096; i++)
			samples[i] = (short)((i * 73) % 10000 - 5000);
		for (rate = 0; rate < sizeof(common) / sizeof(common[0]); rate++)
		{
			resampler_phases_initialize();
			stream = voice_new(samples, 4096, 1, 22050, common[rate]);
			render(stream, output, 64, TRUE);
			CHECK(resampler_cache_hits + resampler_cache_misses == 64, "common frequency did not use cached coefficients");
			voice_delete(stream);
		}
		for (rate = 0; rate < sizeof(uncommon) / sizeof(uncommon[0]); rate++)
		{
			resampler_phases_initialize();
			stream = voice_new(samples, 4096, 1, 22050, uncommon[rate]);
			render(stream, output, 64, TRUE);
			CHECK(!resampler_cache_hits && !resampler_cache_misses, "arbitrary pitch touched coefficient cache");
			voice_delete(stream);
		}
		resampler_phases_initialize();
		stream = voice_new(samples, 4096, 1, 22050, 0);
		render(stream, output, 64, TRUE);
		CHECK(resampler_cache_hits + resampler_cache_misses == 64, "default frequency did not use source rate");
		voice_delete(stream);
		return 0;
	}
	CASE("cache-keys")
	{
		static const float blends[] = { 0.0f, 0.25f, 0.5f, 0.75f, 0.875f };
		unsigned row, value, tap, valid;
		unsigned long misses;
		const float *weights;
		float evicted_blend = 0.0f;

		CHECK(sizeof(resampler_coefficient_cache) < 256 * 1024, "coefficient cache exceeded its memory bound");
		for (row = 0; row < RESAMPLER_TABLE_STEPS; row++)
		{
			for (value = 0; value < sizeof(blends) / sizeof(blends[0]); value++)
			{
				if (value == RESAMPLER_CACHE_WAYS)
					memcpy(&evicted_blend,
						&resampler_coefficient_cache[row][resampler_cache_victim[row]].blend_bits,
						sizeof(evicted_blend));
				misses = resampler_cache_misses;
				weights = resampler_coefficients(row, blends[value]);
				CHECK(resampler_cache_misses == misses + 1, "new exact key did not miss at row %u", row);
				for (tap = 0; tap < RESAMPLER_TAPS; tap++)
					CHECK(weights[tap] == resampler_phases[row][tap] + resampler_deltas[row][tap] * blends[value],
						"cached coefficient differs: row %u tap %u", row, tap);
				weights = resampler_coefficients(row, blends[value]);
				CHECK(resampler_cache_misses == misses + 1, "repeated exact key did not hit");
			}
			valid = 0;
			for (value = 0; value < RESAMPLER_CACHE_WAYS; value++)
				valid += resampler_coefficient_cache[row][value].valid != FALSE;
			CHECK(valid == RESAMPLER_CACHE_WAYS && resampler_cache_victim[row] == 1,
				"fifth blend did not evict within its bounded row");
			misses = resampler_cache_misses;
			/* Check the key selected for eviction without depending on the
			order in which initially empty entries were populated. */
			weights = resampler_coefficients(row, evicted_blend);
			CHECK(resampler_cache_misses == misses + 1 && resampler_cache_victim[row] == 2,
				"evicted key reused stale coefficients");
			for (tap = 0; tap < RESAMPLER_TAPS; tap++)
				CHECK(weights[tap] == resampler_phases[row][tap] + resampler_deltas[row][tap] * evicted_blend,
					"replacement changed a coefficient");
		}
		CHECK(resampler_cache_hits == RESAMPLER_TABLE_STEPS * 5, "hit accounting");
		return 0;
	}
	CASE("cache-reset")
	{
		unsigned row, way, tap;
		const float *weights = resampler_coefficients(255, 0.5f);

		/* Deliberately corrupt the stored value: reinitializing the tables
		must invalidate it rather than return the old storage as a hit. */
		((float *)weights)[0] += 1.0f;
		resampler_phases_initialize();
		CHECK(!resampler_cache_hits && !resampler_cache_misses, "reset counters");
		for (row = 0; row < RESAMPLER_TABLE_STEPS; row++)
		{
			CHECK(!resampler_cache_victim[row], "reset replacement index");
			for (way = 0; way < RESAMPLER_CACHE_WAYS; way++)
				CHECK(!resampler_coefficient_cache[row][way].valid, "reset left an old cache entry valid");
		}
		weights = resampler_coefficients(255, 0.5f);
		CHECK(resampler_cache_misses == 1 && !resampler_cache_hits, "first key after reset must miss");
		for (tap = 0; tap < RESAMPLER_TAPS; tap++)
			CHECK(weights[tap] == resampler_phases[255][tap] + resampler_deltas[255][tap] * 0.5f,
				"stale coefficient after reset");
		return 0;
	}
#endif

	/* A queued packet that has not finished decoding is not end of stream.
	Wait for it without padding the resampler history with silence, then
	resume the same samples as an immediately ready two-packet voice. */
	CASE("pending-packet")
	{
		short samples[800];
		float reference[512], first[256], resumed[512];
		const unsigned rates[] = { 22050, 48000 };
		unsigned channels, rate_index, i;

		for (i = 0; i < 800; i++)
			samples[i] = (short)(1000 + (i * 37) % 12000);
		for (channels = 1; channels <= 2; channels++)
		for (rate_index = 0; rate_index < sizeof(rates) / sizeof(rates[0]); rate_index++)
		{
			const unsigned rate = rates[rate_index];
			struct sdl_stream *ready = voice_new(samples, 400, channels, rate, 0);
			struct sdl_stream *pending = voice_new(samples, 40, channels, rate, 0);
			unsigned long produced;
			unsigned long initial_center = pending->center;

			pending->packet_count = 2;
			pending->packets[1].samples = malloc(360 * channels * sizeof(short));
			memcpy(pending->packets[1].samples, samples + 40 * channels, 360 * channels * sizeof(short));
			pending->packets[1].frames = 360;
			pending->packets[1].ready = FALSE;
			render(ready, reference, 256, TRUE);
			render(pending, first, 64, TRUE);
			render(pending, first + 128, 64, TRUE);
			produced = (unsigned long)(((pending->center - initial_center) + pending->phase) * 48000.0 / rate + 0.5);
			CHECK(produced > 0 && produced < 128, "pending packet must stop mid-chunk: %lu frames", produced);
			CHECK(pending->silence == 0, "pending packet inserted %lu silent source frames", pending->silence);
			CHECK(pending->history_count == RESAMPLER_ZERO_CROSSINGS * RESAMPLER_MAXIMUM_STRETCH + 40,
				"pending packet changed history beyond its 40 ready frames");
			for (i = 0; i < produced * 2; i++)
				CHECK(fabsf(first[i] - reference[i]) < 1e-5f, "before pending: sample %u", i);
			pending->packets[1].ready = TRUE;
			render(pending, resumed, 256 - produced, TRUE);
			for (i = 0; i < (256 - produced) * 2; i++)
				CHECK(fabsf(resumed[i] - reference[produced * 2 + i]) < 1e-5f, "after pending: sample %u", i);
			free(pending->packets[1].samples);
			voice_delete(pending);
			voice_delete(ready);
		}
		return 0;
	}
	CASE("pending-ramp")
	{
		short samples[200];
		float output[128];
		struct sdl_stream *stream;
		unsigned long initial_center, produced;
		float partial_left, partial_right;

		for (index = 0; index < 200; index++)
			samples[index] = 1000;
		stream = voice_new(samples, 40, 1, 48000, 0);
		stream->packets[1].samples = malloc(160 * sizeof(short));
		memcpy(stream->packets[1].samples, samples + 40, 160 * sizeof(short));
		stream->packets[1].frames = 160;
		stream->packets[1].ready = FALSE;
		stream->packet_count = 2;
		stream->gains_valid = TRUE;
		stream->current_left = 0.2f;
		stream->current_right = 0.4f;
		initial_center = stream->center;
		render(stream, output, 64, TRUE);
		produced = stream->center - initial_center;
		CHECK(produced > 0 && produced < 64, "expected interruption during the ramp");
		partial_left = 0.2f + 0.8f * (float)produced / 64.0f;
		partial_right = 0.4f + 0.6f * (float)produced / 64.0f;
		CHECK(fabsf(stream->current_left - partial_left) < 1e-5f, "left gain jumped across the interrupted ramp");
		CHECK(fabsf(stream->current_right - partial_right) < 1e-5f, "right gain jumped across the interrupted ramp");
		render(stream, output, 64, TRUE);
		CHECK(fabsf(stream->current_left - partial_left) < 1e-5f, "left gain changed while waiting");
		CHECK(fabsf(stream->current_right - partial_right) < 1e-5f, "right gain changed while waiting");
		stream->packets[1].ready = TRUE;
		render(stream, output, 64, TRUE);
		CHECK(fabsf(stream->current_left - 1.0f) < 1e-5f, "left gain did not reach its target after resuming");
		CHECK(fabsf(stream->current_right - 1.0f) < 1e-5f, "right gain did not reach its target after resuming");
		free(stream->packets[1].samples);
		voice_delete(stream);
		return 0;
	}

	/* the resampler is the low pass worked out exactly: noise at every rate,
	the error well under 16-bit's */
	CASE("reference")
	{
		double *signal = noise(30000);

		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			double error_db, peak_db;

			against_reference(signal, 30000, rates[index][0], rates[index][1], &error_db, &peak_db);
			CHECK(error_db < -100.0 && peak_db < -90.0, "%lu Hz played at %lu: the error %.1f dB, its peak %.1f dB",
				rates[index][0], rates[index][1], error_db, peak_db);
		}
		free(signal);
		return 0;
	}
	/* an impulse: the low pass itself */
	CASE("impulse")
	{
		double *signal = calloc(4000, sizeof(double));

		signal[2000] = 1.0;
		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			double error_db, peak_db;

			against_reference(signal, 4000, rates[index][0], rates[index][1], &error_db, &peak_db);
			CHECK(peak_db < -90.0, "%lu Hz played at %lu: the impulse's error peaks at %.1f dB", rates[index][0],
				rates[index][1], peak_db);
		}
		free(signal);
		return 0;
	}
	/* sines as heard: a 22 kHz voice flat within half a dB to 10 kHz, a 44
	kHz one to 20 kHz, the rest 70 dB down in the band, more past it */
	CASE("sines")
	{
		static const struct { unsigned long rate, frequency; double hertz, gain_db, rest_db; } sines[] = {
			{ 22050, 0, 100.0, 0.05, -85.0 }, { 22050, 0, 1000.0, 0.05, -85.0 }, { 22050, 0, 5000.0, 0.05, -80.0 },
			{ 22050, 0, 8000.0, 0.1, -75.0 }, { 22050, 0, 10000.0, 0.5, -70.0 },
			{ 44100, 0, 1000.0, 0.05, -85.0 }, { 44100, 0, 10000.0, 0.05, -80.0 },
			{ 44100, 0, 16000.0, 0.1, -75.0 }, { 44100, 0, 20000.0, 0.5, -70.0 },
			{ 48000, 0, 1000.0, 0.05, -85.0 }, { 48000, 0, 20000.0, 0.5, -70.0 },
			{ 22050, 30000, 5000.0, 0.1, -75.0 },
		};

		for (index = 0; index < sizeof(sines) / sizeof(sines[0]); index++)
		{
			double gain_db, rest_db;

			sine_figures(sines[index].hertz, sines[index].rate, sines[index].frequency, &gain_db, &rest_db);
			CHECK(fabs(gain_db) < sines[index].gain_db && rest_db < sines[index].rest_db,
				"%.0f Hz at %lu Hz, played at %lu: gain %.2f dB, the rest %.1f dB", sines[index].hertz,
				sines[index].rate, sines[index].frequency, gain_db, rest_db);
		}
		return 0;
	}
	/* a tone a voice played faster than the output rate takes over the
	output's band does not come out as an alias */
	CASE("aliases")
	{
		double gain_db, rest_db;

		/* 18 kHz at 44.1 kHz, played at 96 kHz: 39 kHz */
		sine_figures(18000.0, 44100, 96000, &gain_db, &rest_db);
		CHECK(rest_db < -60.0, "an alias of 39 kHz %.1f dB", rest_db);
		/* 15 kHz at 44.1 kHz, played at 88.2 kHz: 30 kHz */
		sine_figures(15000.0, 44100, 88200, &gain_db, &rest_db);
		CHECK(rest_db < -60.0, "an alias of 30 kHz %.1f dB", rest_db);
		return 0;
	}
	/* a voice out of earshot moves on as one heard does: once it is audible
	again, both play the same */
	CASE("silent-voice")
	{
		double *signal = noise(60000);
		short *samples = malloc(60000 * sizeof(short));
		double *exact = malloc(60000 * sizeof(double));
		float *heard = calloc(40000 * 2, sizeof(float)), *skipped = calloc(40000 * 2, sizeof(float));
		struct sdl_stream *loud, *quiet;
		unsigned long n, sizes[] = { 1, 7, 512, 333, 2000, 1024 };

		quantize(signal, samples, exact, 60000);
		for (index = 0; index < 3; index++)
		{
			unsigned long frequency = index == 0 ? 0 : index == 1 ? 23456 : 70000, done = 0, size = 0;

			loud = voice_new(samples, 60000, 1, 22050, frequency);
			quiet = voice_new(samples, 60000, 1, 22050, frequency);
			/* the quiet one turned down for a while, at various mix sizes,
			then up */
			quiet->volume = 0.0f;
			while (done < 20000)
			{
				unsigned long count = sizes[size++ % 6];
				static float scratch[2000 * 2], send[2000];

				memset(scratch, 0, sizeof(scratch));
				memset(send, 0, sizeof(send));
				mix_voice(quiet, scratch, send, count);
				for (n = 0; n < count * 2; n++)
					CHECK(scratch[n] == 0.0f, "a voice turned down was heard");
				memset(scratch, 0, sizeof(scratch));
				mix_voice(loud, scratch, send, count);
				done += count;
			}
			CHECK(quiet->center == loud->center && quiet->phase == loud->phase &&
				quiet->history_count == loud->history_count && quiet->cursor == loud->cursor,
				"played at %lu: turned down, the voice is at %lu + %f, heard at %lu + %f", frequency,
				quiet->center, quiet->phase, loud->center, loud->phase);
			/* (it fades in over a mix from 0; the loud one does the same) */
			quiet->volume = 1.0f;
			loud->current_left = loud->current_right = 0.0f;
			render(quiet, skipped, 40000, TRUE);
			render(loud, heard, 40000, TRUE);
			for (n = 0; n < 40000 * 2; n++)
				CHECK(fabsf(heard[n] - skipped[n]) < 1.0e-6f, "played at %lu: frame %lu differs (%f, %f)",
					frequency, n / 2, heard[n], skipped[n]);
			voice_delete(loud);
			voice_delete(quiet);
		}
		/* a voice that runs out of packets while it is turned down ends as
		one heard does */
		loud = voice_new(samples, 3000, 1, 22050, 0);
		quiet = voice_new(samples, 3000, 1, 22050, 0);
		quiet->volume = 0.0f;
		render(loud, heard, 20000, TRUE);
		render(quiet, skipped, 20000, TRUE);
		CHECK(quiet->center == loud->center && quiet->silence == loud->silence &&
			quiet->packets[0].finished == loud->packets[0].finished, "turned down, the voice ends at %lu (%lu), "
			"heard at %lu (%lu)", quiet->center, quiet->silence, loud->center, loud->silence);
		voice_delete(loud);
		voice_delete(quiet);
		free(heard);
		free(skipped);
		free(exact);
		free(samples);
		free(signal);
		return 0;
	}
	/* a mono voice is heard as a stereo one with the same two channels */
	CASE("mono")
	{
		double *signal = noise(30000);
		short *samples = malloc(30000 * sizeof(short)), *pairs = malloc(30000 * 2 * sizeof(short));
		double *exact = malloc(30000 * sizeof(double));
		float *mono = calloc(50000 * 2, sizeof(float)), *stereo = calloc(50000 * 2, sizeof(float));
		unsigned long n;

		quantize(signal, samples, exact, 30000);
		for (n = 0; n < 30000; n++)
			pairs[n * 2] = pairs[n * 2 + 1] = samples[n];
		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			struct sdl_stream *one = voice_new(samples, 30000, 1, rates[index][0], rates[index][1]);
			struct sdl_stream *two = voice_new(pairs, 30000, 2, rates[index][0], rates[index][1]);

			render(one, mono, 50000, TRUE);
			render(two, stereo, 50000, TRUE);
			for (n = 0; n < 50000 * 2; n++)
				CHECK(fabsf(mono[n] - stereo[n]) < 1.0e-6f, "%lu Hz played at %lu: frame %lu differs (%f, %f)",
					rates[index][0], rates[index][1], n / 2, mono[n], stereo[n]);
			voice_delete(one);
			voice_delete(two);
		}
		free(mono);
		free(stereo);
		free(exact);
		free(pairs);
		free(samples);
		free(signal);
		return 0;
	}
	CASE("metrics")
	{
		static const double tones[] = { 100, 1000, 5000, 8000, 10000, 10500, 15000, 18000, 20000, 21000 };
		double *signal = noise(30000), *impulse = calloc(4000, sizeof(double));

		impulse[2000] = 1.0;
		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			unsigned long rate = rates[index][0], frequency = rates[index][1], tone;
			double error_db, peak_db, impulse_db, impulse_peak_db;

			against_reference(signal, 30000, rate, frequency, &error_db, &peak_db);
			against_reference(impulse, 4000, rate, frequency, &impulse_db, &impulse_peak_db);
			printf("%lu Hz played at %lu: against the exact low pass, noise %.1f dB (peak %.1f), impulse peak %.1f dB\n",
				rate, frequency ? frequency : rate, error_db, peak_db, impulse_peak_db);
			for (tone = 0; tone < sizeof(tones) / sizeof(tones[0]); tone++)
			{
				double gain_db, rest_db;

				if (tones[tone] >= 0.5 * rate)
					continue;
				sine_figures(tones[tone], rate, frequency, &gain_db, &rest_db);
				printf("  %6.0f Hz: gain %+.3f dB, the rest %.1f dB\n", tones[tone], gain_db, rest_db);
			}
			if (!frequency)
				printf("  sweep to 0.9 of its band: %.1f dB from ideal\n", sweep_figure(rate, 0.45 * rate));
		}
		free(signal);
		free(impulse);
		return 0;
	}
	/* noise, mono and stereo, at each rate through the mixer: the output as
	raw floats on stdout, to compare one mixer with another */
	CASE("render")
	{
		static float mixed[CHUNK * 2];
		double *signal = noise(60000);
		short *samples = malloc(30000 * 2 * sizeof(short));
		double *exact = malloc(60000 * sizeof(double));
		float *output = calloc(40000 * 2, sizeof(float));

		quantize(signal, samples, exact, 60000);
		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			unsigned long channels;

			for (channels = 1; channels <= 2; channels++)
			{
				struct sdl_stream *stream = voice_new(samples, 30000, channels, rates[index][0], rates[index][1]);

				render(stream, output, 40000, TRUE);
				fwrite(output, sizeof(float), 40000 * 2, stdout);
				voice_delete(stream);
			}
		}
		/* and a scene's whole mix, reverb and limiter, its voices out of
		earshot coming into it halfway */
		scene(24, 8, TRUE, 3.0);
		for (index = 0; index < 2 * OUTPUT_RATE / CHUNK; index++)
		{
			if (index == OUTPUT_RATE / CHUNK)
				scene_approach();
			mix(mixed, CHUNK);
			fwrite(mixed, sizeof(float), CHUNK * 2, stdout);
		}
		return 0;
	}
	CASE("bench")
	{
		bench(argc > 2 ? strtoul(argv[2], NULL, 0) : 24, argc > 3 ? strtoul(argv[3], NULL, 0) : 16,
			argc > 4 && !strcmp(argv[4], "indoor"), 10.0);
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
