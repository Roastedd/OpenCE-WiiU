/* Adapted from OpenH1/OpenH1, revision 6750db5a6c9716857bd1e63d9ec188a20c49f1d3.
 * Copyright OpenH1 contributors; AGPL-3.0-or-later.
 * Provenance and hardware measurements: docs/reading/xbox_audio.md. */

/*
DSOUND_SDL.C

Xbox DirectSound for all ports: a software mixer on an SDL3 audio stream.

The game plays most sounds through DirectSound streams: 16-bit stereo PCM
(music and other uncompressed sounds) and Xbox ADPCM, mono or stereo, at 22
or 44 kHz. A packet is decoded to 16-bit PCM when the game submits it, since
the sound cache may reuse its memory once the packet completes. The mixer
runs on SDL's audio thread; for every voice it resamples to the output rate
with the measured Xbox resampling kernel (which is how SetFrequency changes
pitch; resampling) and applies:
	- the stream volume (millibels),
	- the front left and right mix bin volumes of 2D voices,
	- for 3D voices, DirectSound's inverse distance rolloff between the
	  minimum and maximum distance, an equal power pan from the source's
	  direction in listener space when HRTF is disabled, and the I3DL2 direct path, obstruction
	  and occlusion levels, their high frequency levels a low pass at the
	  environment's HF reference: a sound behind a wall is muffled,
	- for 3D voices, the Doppler shift of the voice's and the listener's
	  velocities, which moves the resampling step (doppler_ratio). Positioned
	  stereo sources also shift when audio.stereo_doppler is enabled.
3D voices also send to an I3DL2 reverb of the environment the listener is in
(dsound_xbox_reverb.inl). Mono 3D voices use the measured Xbox HRTF
on stereo and surround outputs (dsound_xbox_hrtf.inl), enabled by default.
Cones are not modelled. A look-ahead limiter keeps the sum under
full scale (limit). A voice the game cuts off while it plays fades out over
5 ms rather than stopping dead, which clicked (voice_cut).

The output is stereo or 5.1 (audio.channels). For 5.1 the speaker config
reports AC-3, as an Xbox set to Dolby Digital does: the game then sends its
2D voices to the center and back mix bins too (dsound_initialize_channel),
3D voices and stereo voices positioned in the world are panned around the
front and back pairs (surround_split), and the reverb plays from the
front and back pairs. The mix bins 0-5 are in SDL's 5.1 order (FL, FR, FC,
LFE, BL, BR), so bin n is output channel n. The game sends nothing to the
LFE bin. An explicit LFE send is low-pass filtered at 86 Hz.

Packets the mixer has finished are completed from DirectSoundDoWork, which
the game calls every frame, and from Flush, never from the audio thread:
the game's completion callback is not meant to run concurrently with it.
On the desktop DirectSoundDoWork also runs on sdl_platform.c's swap sound
thread, but only while the game's thread is blocked in the driver showing a
frame (an unfocused window's long swaps), and the game's thread waits for it
to finish before going on: never at once with the game's.

Without an audio device, a clock thread runs the same mixer into a scratch
buffer, so streams still drain at their real rate.

On the Wii U (DSOUND_DECODE_THREAD) a packet is decoded by a thread of its
own on the GX2 thread's core, not the game thread as it is submitted: the
mixer waits for a packet until it is decoded. And the mixer lets go of its
lock between voices when the game thread waits for it (DSOUND_MIXER_YIELDS):
it held it for all the voices, a millisecond, and in a fight the game thread
spent a fifth of its time waiting for it and a twelfth decoding.

The loading screen's four pitched chant voices use DirectSound buffers.
Their PCM or Xbox ADPCM data is copied and decoded when set, then mixed
dry into the front speakers under the master and effects volumes.

audio.volume sets the master volume (default 1.0); audio.reverb = false
turns the reverb off; audio.hrtf = false selects the legacy pan;
audio.resampler = "xbox" uses the measured Xbox kernel by default. "sinc"
selects a band-limited low pass, and "linear" interpolates neighbouring frames; audio.enabled = false
skips opening a device; audio.channels chooses stereo or 5.1
(port_config.c).
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "voice_audio.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef HALO_WIIU
#include <coreinit/time.h>
#include <coreinit/thread.h>
#include "../../wiiu/src/wiiu_shell.h"
#include "../../wiiu/src/wiiu_decoder_profile.h"
#include "../../wiiu/src/wiiu_mixer_profile.h"
#include "wiiu_resampler_ps.h"
#include "../../wiiu/src/wiiu_packet_pool.h"
#define packet_allocate wiiu_packet_pool_allocate
#define packet_deallocate wiiu_packet_pool_release
#else
#define packet_allocate malloc
#define packet_deallocate free
#endif

#define OUTPUT_RATE 48000
#define OUTPUT_CHANNELS 2 /* the stereo mix's, and the reverb's outputs */
#define MAXIMUM_OUTPUT_CHANNELS 6
#define MAXIMUM_STREAM_PACKETS 64
#define MIX_CHUNK_FRAMES 1024
/* the frames a voice cut off fades out over (voice_cut): 5 ms */
#define CUT_FADE_FRAMES 240

#ifdef HALO_WIIU
#define DSOUND_DECODE_THREAD
#define DSOUND_MIXER_YIELDS
#endif

#if defined(HALO_WIIU) || defined(RESAMPLER_GAIN_CACHE_TEST)
#define VOICE_GAIN_CACHE
#endif

#define XBOX_ADPCM_BLOCK_BYTES 36
#define XBOX_ADPCM_BLOCK_SAMPLES 64
#include "dsound_xbox_types.h"


/* ---------- voices */

struct voice_packet
{
	XMEDIAPACKET packet;
	short *samples;           /* interleaved, source channel count */
	unsigned long frames;
	BOOL finished;            /* played out by the mixer, not yet completed */
	BOOL ready;               /* decoded (DSOUND_DECODE_THREAD: the decoder sets it) */
	unsigned long serial;     /* the packet's, among every stream's (the decoder finds it by it) */
};

#ifdef VOICE_GAIN_CACHE
/* Every input read by voice_gains, spatialize and distance_attenuation.
   Byte comparison preserves signed zero and stable NaN payloads. */
struct voice_gain_inputs
{
	float real[25];
	unsigned long integer[11];
};
#endif

struct sdl_stream
{
	/* must be first: in C an IDirectSoundStream is just { lpVtbl } */
	IDirectSoundStream object;
	struct sdl_stream *next;
	ULONG reference_count;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;

	/* format */
	BOOL adpcm;
	unsigned long channels;
	DWORD sample_rate;
	DWORD frequency;

	BOOL paused;

	/* 2D gains */
	float volume;             /* SetVolume */
	float mix_left, mix_right;
	/* a stereo voice of a sound in the world: panned towards it, -1 left
	to 1 right, its distance, and the fade of its volume with distance that
	the game made (dsound_sdl_stream_set_stereo_position) */
	BOOL stereo_positioned;
	float stereo_pan;
	float stereo_distance;
	float stereo_distance_fade;
	/* how fast it comes towards the listener, in world units a second, for
	its Doppler effect (doppler_ratio) */
	float stereo_approach;
	float headroom;
	/* on 5.1 (output_channels 6): the gains from each source channel to
	each speaker mix bin (SetMixBins, SetMixBinVolumes) */
	float surround_bins[2][MAXIMUM_OUTPUT_CHANNELS];

	/* 3D */
	BOOL has_3d;
	DWORD mode;
	float position[3];
	float minimum_distance, maximum_distance;
	/* the I3DL2 direct path's level and high frequency level, in millibels
	with obstruction and occlusion (SetI3DL2Source) */
	LONG direct, direct_hf;
	/* the I3DL2 room (reverb) send's level and high frequency level, in
	millibels with occlusion, and its rolloff factor (SetI3DL2Source) */
	LONG room, room_hf;
	float room_rolloff_factor;
	/* the voice's velocity in the game's units a second (SetVelocity) */
	float velocity[3];

	struct voice_packet packets[MAXIMUM_STREAM_PACKETS];
	unsigned long packet_head;
	unsigned long packet_count;
	/* the next frame to take from the first packet not finished */
	unsigned long cursor;
	/* the resampler's (resampler_reset): the last RESAMPLER_HISTORY frames
	taken, each channel's apart and each frame twice, RESAMPLER_HISTORY apart,
	so that the frames a low pass reaches are side by side however the ring
	wraps (resampler_dot); how many were taken, the frame the output is at and
	how far past it, and the frames of silence taken since the packets ran
	out */
	float history[2][2 * RESAMPLER_HISTORY];
	unsigned long history_count;
	unsigned long center;
	double phase;
	unsigned long silence;
	/* gains and low pass coefficients the mixer is ramping from, to avoid
	clicks */
	float current_left, current_right, current_room;
	float current_direct_lowpass, current_room_lowpass;
	float current_surround[2][MAXIMUM_OUTPUT_CHANNELS];
	BOOL gains_valid;
#ifdef VOICE_GAIN_CACHE
	/* Five exact targets, reused until an input changes: 168 bytes a voice
	on the Wii U. The ramp and filter state are not cached. */
	BOOL gain_cache_valid;
	struct voice_gain_inputs gain_cache_inputs;
	float gain_cache_target[7];
#endif
	/* the direct path's low pass, for each channel, and the room send's */
	float direct_lowpass[2];
	float room_lowpass;
	/* what the voice was playing when the game cut it off, faded out
	(voice_cut): frames of output_channels floats and their room send, and
	how many of them have played */
	float cut[CUT_FADE_FRAMES * MAXIMUM_OUTPUT_CHANNELS];
	float cut_send[CUT_FADE_FRAMES];
	unsigned long cut_played;
	/* being flushed (stream_flush): the mixer leaves the packets not yet
	given back, which the cut has played already, while the game's callback
	runs without the lock */
	BOOL flushing;
	double current_step;
	float current_doppler;
	float target_left, target_right, target_room;
	double target_step;
	float target_direct_lowpass, target_room_lowpass;
	float target_surround[2][MAXIMUM_OUTPUT_CHANNELS];
	unsigned long ramp_frames;
	BOOL hrtf_on;
	float hrtf_mix;
	struct hrtf_parameters hrtf_current, hrtf_target;
	float hrtf_frames[2][HRTF_FRAMES];
	float hrtf_bands[HRTF_BANDS - 1][2];
};

static pthread_mutex_t mixer_lock = PTHREAD_MUTEX_INITIALIZER;
static struct sdl_stream *streams;

#ifdef DSOUND_DECODE_THREAD
/* the mixes in which a voice waited for its next packet to be decoded (logged by the decoder) */
static unsigned long decoder_voices_waited;
/* Caller-side decoding when a queue or allocation is unavailable. */
static unsigned long decoder_profile_fallbacks;
/* the decoder's time in packets (the detailed overlay's AUDIO, port/wiiu/src/wiiu_perf.h): a monotonic count of the time base's ticks, written by the decoder
thread only, in 32 bits so that another thread reads it whole */
static volatile uint32_t wiiu_decoder_perf_ticks_value;

uint32_t wiiu_decoder_perf_ticks(void);

uint32_t wiiu_decoder_perf_ticks(void)
{
	return wiiu_decoder_perf_ticks_value;
}
#endif

#ifdef DSOUND_MIXER_YIELDS
/* how many threads other than the mixer wait for its lock, and the voice the
mixer goes on with once it has the lock back (stream_release moves it past a
voice it removes) */
static int mixer_lock_wanted;
static struct sdl_stream *mixer_walk_next;

static void mixer_lock_take(void)
{
	if (pthread_mutex_trylock(&mixer_lock) == 0)
		return;
	__atomic_add_fetch(&mixer_lock_wanted, 1, __ATOMIC_ACQ_REL);
	pthread_mutex_lock(&mixer_lock);
	__atomic_sub_fetch(&mixer_lock_wanted, 1, __ATOMIC_ACQ_REL);
}
#else
#define mixer_lock_take() pthread_mutex_lock(&mixer_lock)
#endif

/* the listener, in DirectSound's left-handed +y up space */
static struct
{
	float position[3];
	float front[3];
	float top[3];
	float rolloff_factor;
	/* meters a unit (SetDistanceFactor): the game sets 3.048, a world unit
	being 10 feet. Only Doppler uses it (doppler_ratio) */
	float distance_factor;
	/* the listener's velocity in the game's units a second (SetVelocity):
	the game sets none, and gives each voice its velocity relative to the
	listener (sound_manager.c) */
	float velocity[3];
	/* DirectSound's Doppler factor, which the game leaves at 1: a multiple
	of the velocities */
	float doppler_factor;
} listener = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, 1.0f, 1.0f, { 0, 0, 0 }, 1.0f };

static float master_volume = 1.0f;

/* Buffers own decoded samples independently of the game's loading memory.
All mutable buffer state and the list are protected by mixer_lock. */
struct sdl_buffer
{
	struct sdl_buffer *next;
	ULONG reference_count;
	unsigned long channels, block_bytes, block_frames;
	BOOL adpcm, playing, looping;
	short *samples;
	unsigned long frames, loop_start, loop_end;
	double position, rate;
	float volume, current_volume;
};

static struct sdl_buffer *buffers;
/* 2 or 6 (audio.channels), chosen when the device first opens; the game
sets its mix bins by it, so it holds while the game runs */
static unsigned long output_channels = 2;
static BOOL hrtf_enabled = TRUE;

/* the I3DL2 listener properties (SetI3DL2Listener), which the reverb models:
DirectSound's defaults until the game sets an environment, their room level
leaving it silent; serial counts the changes */
static DSI3DL2LISTENER environment =
{
	DSBVOLUME_MIN, 0, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f
};
static unsigned long environment_serial;
/* audio.reverb */
static BOOL reverb_enabled = TRUE;
/* audio.stereo_doppler: positioned stereo voices shift as 3D ones do
(doppler_ratio); the Xbox played stereo sounds at their own pitch */
static BOOL stereo_doppler_enabled = FALSE;

static float gain_from_millibels(LONG millibels)
{
	if (millibels <= DSBVOLUME_MIN)
		return 0.0f;
	return powf(10.0f, (float)millibels / 2000.0f);
}

/* value, or 0 once it is far too small to hear: the state of a filter or of
the reverb fed silence decays into the denormals, which many CPUs work out
slowly, and neither x86 nor ARM flushes them to zero by default */
static float voice_level(float gain)
{
	float size = fabsf(gain), level;

	if (size >= VOICE_LEVEL_KNEE)
		return gain;
	if (size <= 0.0f)
		return 0.0f;
	level = (VOICE_LEVEL_SILENT + 20.0f * log10f(size)) * VOICE_LEVEL_STEP;
	if (level <= 0.0f)
		return 0.0f;
	return gain < 0.0f ? -level : level;
}

static float flush_denormal(float value)
{
	return fabsf(value) < 1.0e-20f ? 0.0f : value;
}

/* cos(2 pi f / OUTPUT_RATE) of a filter's frequency f, within what the
output rate holds */
static float frequency_cosine(float frequency)
{
	if (frequency < 20.0f)
		frequency = 20.0f;
	if (frequency > 0.45f * OUTPUT_RATE)
		frequency = 0.45f * OUTPUT_RATE;
	return cosf(2.0f * 3.14159265f * frequency / OUTPUT_RATE);
}

/* the coefficient a of the one-pole low pass y = x + a (y' - x) whose gain is
gain (at most 1) at the frequency of cosine (frequency_cosine) */
static float lowpass_coefficient(float gain, float cosine)
{
	float a, b;

	if (gain >= 1.0f)
		return 0.0f;
	if (gain < 0.001f)
		gain = 0.001f;
	a = 1.0f - gain * gain;
	b = 1.0f - gain * gain * cosine;
	return (b - sqrtf(b * b - a * a)) / a;
}

/* ---------- decoding */

#ifdef HALO_WIIU
/* port: cache the exact IMA nibble difference and next index; packet
   decoding keeps its predictor clamp, headers and sample order. */
struct ima_decode_entry { int difference, next_index; };
static const struct ima_decode_entry ima_decode_table[89][16] =
{
	{{0,0},{1,0},{3,0},{4,0},{7,2},{8,4},{10,6},{11,8},{0,0},{-1,0},{-3,0},{-4,0},{-7,2},{-8,4},{-10,6},{-11,8}},
	{{1,0},{3,0},{5,0},{7,0},{9,3},{11,5},{13,7},{15,9},{-1,0},{-3,0},{-5,0},{-7,0},{-9,3},{-11,5},{-13,7},{-15,9}},
	{{1,1},{3,1},{5,1},{7,1},{10,4},{12,6},{14,8},{16,10},{-1,1},{-3,1},{-5,1},{-7,1},{-10,4},{-12,6},{-14,8},{-16,10}},
	{{1,2},{3,2},{6,2},{8,2},{11,5},{13,7},{16,9},{18,11},{-1,2},{-3,2},{-6,2},{-8,2},{-11,5},{-13,7},{-16,9},{-18,11}},
	{{1,3},{3,3},{6,3},{8,3},{12,6},{14,8},{17,10},{19,12},{-1,3},{-3,3},{-6,3},{-8,3},{-12,6},{-14,8},{-17,10},{-19,12}},
	{{1,4},{4,4},{7,4},{10,4},{13,7},{16,9},{19,11},{22,13},{-1,4},{-4,4},{-7,4},{-10,4},{-13,7},{-16,9},{-19,11},{-22,13}},
	{{1,5},{4,5},{7,5},{10,5},{14,8},{17,10},{20,12},{23,14},{-1,5},{-4,5},{-7,5},{-10,5},{-14,8},{-17,10},{-20,12},{-23,14}},
	{{1,6},{4,6},{8,6},{11,6},{15,9},{18,11},{22,13},{25,15},{-1,6},{-4,6},{-8,6},{-11,6},{-15,9},{-18,11},{-22,13},{-25,15}},
	{{2,7},{6,7},{10,7},{14,7},{18,10},{22,12},{26,14},{30,16},{-2,7},{-6,7},{-10,7},{-14,7},{-18,10},{-22,12},{-26,14},{-30,16}},
	{{2,8},{6,8},{10,8},{14,8},{19,11},{23,13},{27,15},{31,17},{-2,8},{-6,8},{-10,8},{-14,8},{-19,11},{-23,13},{-27,15},{-31,17}},
	{{2,9},{6,9},{11,9},{15,9},{21,12},{25,14},{30,16},{34,18},{-2,9},{-6,9},{-11,9},{-15,9},{-21,12},{-25,14},{-30,16},{-34,18}},
	{{2,10},{7,10},{12,10},{17,10},{23,13},{28,15},{33,17},{38,19},{-2,10},{-7,10},{-12,10},{-17,10},{-23,13},{-28,15},{-33,17},{-38,19}},
	{{2,11},{7,11},{13,11},{18,11},{25,14},{30,16},{36,18},{41,20},{-2,11},{-7,11},{-13,11},{-18,11},{-25,14},{-30,16},{-36,18},{-41,20}},
	{{3,12},{9,12},{15,12},{21,12},{28,15},{34,17},{40,19},{46,21},{-3,12},{-9,12},{-15,12},{-21,12},{-28,15},{-34,17},{-40,19},{-46,21}},
	{{3,13},{10,13},{17,13},{24,13},{31,16},{38,18},{45,20},{52,22},{-3,13},{-10,13},{-17,13},{-24,13},{-31,16},{-38,18},{-45,20},{-52,22}},
	{{3,14},{10,14},{18,14},{25,14},{34,17},{41,19},{49,21},{56,23},{-3,14},{-10,14},{-18,14},{-25,14},{-34,17},{-41,19},{-49,21},{-56,23}},
	{{4,15},{12,15},{21,15},{29,15},{38,18},{46,20},{55,22},{63,24},{-4,15},{-12,15},{-21,15},{-29,15},{-38,18},{-46,20},{-55,22},{-63,24}},
	{{4,16},{13,16},{22,16},{31,16},{41,19},{50,21},{59,23},{68,25},{-4,16},{-13,16},{-22,16},{-31,16},{-41,19},{-50,21},{-59,23},{-68,25}},
	{{5,17},{15,17},{25,17},{35,17},{46,20},{56,22},{66,24},{76,26},{-5,17},{-15,17},{-25,17},{-35,17},{-46,20},{-56,22},{-66,24},{-76,26}},
	{{5,18},{16,18},{27,18},{38,18},{50,21},{61,23},{72,25},{83,27},{-5,18},{-16,18},{-27,18},{-38,18},{-50,21},{-61,23},{-72,25},{-83,27}},
	{{6,19},{18,19},{31,19},{43,19},{56,22},{68,24},{81,26},{93,28},{-6,19},{-18,19},{-31,19},{-43,19},{-56,22},{-68,24},{-81,26},{-93,28}},
	{{6,20},{19,20},{33,20},{46,20},{61,23},{74,25},{88,27},{101,29},{-6,20},{-19,20},{-33,20},{-46,20},{-61,23},{-74,25},{-88,27},{-101,29}},
	{{7,21},{22,21},{37,21},{52,21},{67,24},{82,26},{97,28},{112,30},{-7,21},{-22,21},{-37,21},{-52,21},{-67,24},{-82,26},{-97,28},{-112,30}},
	{{8,22},{24,22},{41,22},{57,22},{74,25},{90,27},{107,29},{123,31},{-8,22},{-24,22},{-41,22},{-57,22},{-74,25},{-90,27},{-107,29},{-123,31}},
	{{9,23},{27,23},{45,23},{63,23},{82,26},{100,28},{118,30},{136,32},{-9,23},{-27,23},{-45,23},{-63,23},{-82,26},{-100,28},{-118,30},{-136,32}},
	{{10,24},{30,24},{50,24},{70,24},{90,27},{110,29},{130,31},{150,33},{-10,24},{-30,24},{-50,24},{-70,24},{-90,27},{-110,29},{-130,31},{-150,33}},
	{{11,25},{33,25},{55,25},{77,25},{99,28},{121,30},{143,32},{165,34},{-11,25},{-33,25},{-55,25},{-77,25},{-99,28},{-121,30},{-143,32},{-165,34}},
	{{12,26},{36,26},{60,26},{84,26},{109,29},{133,31},{157,33},{181,35},{-12,26},{-36,26},{-60,26},{-84,26},{-109,29},{-133,31},{-157,33},{-181,35}},
	{{13,27},{39,27},{66,27},{92,27},{120,30},{146,32},{173,34},{199,36},{-13,27},{-39,27},{-66,27},{-92,27},{-120,30},{-146,32},{-173,34},{-199,36}},
	{{14,28},{43,28},{73,28},{102,28},{132,31},{161,33},{191,35},{220,37},{-14,28},{-43,28},{-73,28},{-102,28},{-132,31},{-161,33},{-191,35},{-220,37}},
	{{16,29},{48,29},{81,29},{113,29},{146,32},{178,34},{211,36},{243,38},{-16,29},{-48,29},{-81,29},{-113,29},{-146,32},{-178,34},{-211,36},{-243,38}},
	{{17,30},{52,30},{88,30},{123,30},{160,33},{195,35},{231,37},{266,39},{-17,30},{-52,30},{-88,30},{-123,30},{-160,33},{-195,35},{-231,37},{-266,39}},
	{{19,31},{58,31},{97,31},{136,31},{176,34},{215,36},{254,38},{293,40},{-19,31},{-58,31},{-97,31},{-136,31},{-176,34},{-215,36},{-254,38},{-293,40}},
	{{21,32},{64,32},{107,32},{150,32},{194,35},{237,37},{280,39},{323,41},{-21,32},{-64,32},{-107,32},{-150,32},{-194,35},{-237,37},{-280,39},{-323,41}},
	{{23,33},{70,33},{118,33},{165,33},{213,36},{260,38},{308,40},{355,42},{-23,33},{-70,33},{-118,33},{-165,33},{-213,36},{-260,38},{-308,40},{-355,42}},
	{{26,34},{78,34},{130,34},{182,34},{235,37},{287,39},{339,41},{391,43},{-26,34},{-78,34},{-130,34},{-182,34},{-235,37},{-287,39},{-339,41},{-391,43}},
	{{28,35},{85,35},{143,35},{200,35},{258,38},{315,40},{373,42},{430,44},{-28,35},{-85,35},{-143,35},{-200,35},{-258,38},{-315,40},{-373,42},{-430,44}},
	{{31,36},{94,36},{157,36},{220,36},{284,39},{347,41},{410,43},{473,45},{-31,36},{-94,36},{-157,36},{-220,36},{-284,39},{-347,41},{-410,43},{-473,45}},
	{{34,37},{103,37},{173,37},{242,37},{313,40},{382,42},{452,44},{521,46},{-34,37},{-103,37},{-173,37},{-242,37},{-313,40},{-382,42},{-452,44},{-521,46}},
	{{38,38},{114,38},{191,38},{267,38},{345,41},{421,43},{498,45},{574,47},{-38,38},{-114,38},{-191,38},{-267,38},{-345,41},{-421,43},{-498,45},{-574,47}},
	{{42,39},{126,39},{210,39},{294,39},{379,42},{463,44},{547,46},{631,48},{-42,39},{-126,39},{-210,39},{-294,39},{-379,42},{-463,44},{-547,46},{-631,48}},
	{{46,40},{138,40},{231,40},{323,40},{417,43},{509,45},{602,47},{694,49},{-46,40},{-138,40},{-231,40},{-323,40},{-417,43},{-509,45},{-602,47},{-694,49}},
	{{51,41},{153,41},{255,41},{357,41},{459,44},{561,46},{663,48},{765,50},{-51,41},{-153,41},{-255,41},{-357,41},{-459,44},{-561,46},{-663,48},{-765,50}},
	{{56,42},{168,42},{280,42},{392,42},{505,45},{617,47},{729,49},{841,51},{-56,42},{-168,42},{-280,42},{-392,42},{-505,45},{-617,47},{-729,49},{-841,51}},
	{{61,43},{184,43},{308,43},{431,43},{555,46},{678,48},{802,50},{925,52},{-61,43},{-184,43},{-308,43},{-431,43},{-555,46},{-678,48},{-802,50},{-925,52}},
	{{68,44},{204,44},{340,44},{476,44},{612,47},{748,49},{884,51},{1020,53},{-68,44},{-204,44},{-340,44},{-476,44},{-612,47},{-748,49},{-884,51},{-1020,53}},
	{{74,45},{223,45},{373,45},{522,45},{672,48},{821,50},{971,52},{1120,54},{-74,45},{-223,45},{-373,45},{-522,45},{-672,48},{-821,50},{-971,52},{-1120,54}},
	{{82,46},{246,46},{411,46},{575,46},{740,49},{904,51},{1069,53},{1233,55},{-82,46},{-246,46},{-411,46},{-575,46},{-740,49},{-904,51},{-1069,53},{-1233,55}},
	{{90,47},{271,47},{452,47},{633,47},{814,50},{995,52},{1176,54},{1357,56},{-90,47},{-271,47},{-452,47},{-633,47},{-814,50},{-995,52},{-1176,54},{-1357,56}},
	{{99,48},{298,48},{497,48},{696,48},{895,51},{1094,53},{1293,55},{1492,57},{-99,48},{-298,48},{-497,48},{-696,48},{-895,51},{-1094,53},{-1293,55},{-1492,57}},
	{{109,49},{328,49},{547,49},{766,49},{985,52},{1204,54},{1423,56},{1642,58},{-109,49},{-328,49},{-547,49},{-766,49},{-985,52},{-1204,54},{-1423,56},{-1642,58}},
	{{120,50},{360,50},{601,50},{841,50},{1083,53},{1323,55},{1564,57},{1804,59},{-120,50},{-360,50},{-601,50},{-841,50},{-1083,53},{-1323,55},{-1564,57},{-1804,59}},
	{{132,51},{397,51},{662,51},{927,51},{1192,54},{1457,56},{1722,58},{1987,60},{-132,51},{-397,51},{-662,51},{-927,51},{-1192,54},{-1457,56},{-1722,58},{-1987,60}},
	{{145,52},{436,52},{728,52},{1019,52},{1311,55},{1602,57},{1894,59},{2185,61},{-145,52},{-436,52},{-728,52},{-1019,52},{-1311,55},{-1602,57},{-1894,59},{-2185,61}},
	{{160,53},{480,53},{801,53},{1121,53},{1442,56},{1762,58},{2083,60},{2403,62},{-160,53},{-480,53},{-801,53},{-1121,53},{-1442,56},{-1762,58},{-2083,60},{-2403,62}},
	{{176,54},{528,54},{881,54},{1233,54},{1587,57},{1939,59},{2292,61},{2644,63},{-176,54},{-528,54},{-881,54},{-1233,54},{-1587,57},{-1939,59},{-2292,61},{-2644,63}},
	{{194,55},{582,55},{970,55},{1358,55},{1746,58},{2134,60},{2522,62},{2910,64},{-194,55},{-582,55},{-970,55},{-1358,55},{-1746,58},{-2134,60},{-2522,62},{-2910,64}},
	{{213,56},{639,56},{1066,56},{1492,56},{1920,59},{2346,61},{2773,63},{3199,65},{-213,56},{-639,56},{-1066,56},{-1492,56},{-1920,59},{-2346,61},{-2773,63},{-3199,65}},
	{{234,57},{703,57},{1173,57},{1642,57},{2112,60},{2581,62},{3051,64},{3520,66},{-234,57},{-703,57},{-1173,57},{-1642,57},{-2112,60},{-2581,62},{-3051,64},{-3520,66}},
	{{258,58},{774,58},{1291,58},{1807,58},{2324,61},{2840,63},{3357,65},{3873,67},{-258,58},{-774,58},{-1291,58},{-1807,58},{-2324,61},{-2840,63},{-3357,65},{-3873,67}},
	{{284,59},{852,59},{1420,59},{1988,59},{2556,62},{3124,64},{3692,66},{4260,68},{-284,59},{-852,59},{-1420,59},{-1988,59},{-2556,62},{-3124,64},{-3692,66},{-4260,68}},
	{{312,60},{936,60},{1561,60},{2185,60},{2811,63},{3435,65},{4060,67},{4684,69},{-312,60},{-936,60},{-1561,60},{-2185,60},{-2811,63},{-3435,65},{-4060,67},{-4684,69}},
	{{343,61},{1030,61},{1717,61},{2404,61},{3092,64},{3779,66},{4466,68},{5153,70},{-343,61},{-1030,61},{-1717,61},{-2404,61},{-3092,64},{-3779,66},{-4466,68},{-5153,70}},
	{{378,62},{1134,62},{1890,62},{2646,62},{3402,65},{4158,67},{4914,69},{5670,71},{-378,62},{-1134,62},{-1890,62},{-2646,62},{-3402,65},{-4158,67},{-4914,69},{-5670,71}},
	{{415,63},{1246,63},{2078,63},{2909,63},{3742,66},{4573,68},{5405,70},{6236,72},{-415,63},{-1246,63},{-2078,63},{-2909,63},{-3742,66},{-4573,68},{-5405,70},{-6236,72}},
	{{457,64},{1372,64},{2287,64},{3202,64},{4117,67},{5032,69},{5947,71},{6862,73},{-457,64},{-1372,64},{-2287,64},{-3202,64},{-4117,67},{-5032,69},{-5947,71},{-6862,73}},
	{{503,65},{1509,65},{2516,65},{3522,65},{4529,68},{5535,70},{6542,72},{7548,74},{-503,65},{-1509,65},{-2516,65},{-3522,65},{-4529,68},{-5535,70},{-6542,72},{-7548,74}},
	{{553,66},{1660,66},{2767,66},{3874,66},{4981,69},{6088,71},{7195,73},{8302,75},{-553,66},{-1660,66},{-2767,66},{-3874,66},{-4981,69},{-6088,71},{-7195,73},{-8302,75}},
	{{608,67},{1825,67},{3043,67},{4260,67},{5479,70},{6696,72},{7914,74},{9131,76},{-608,67},{-1825,67},{-3043,67},{-4260,67},{-5479,70},{-6696,72},{-7914,74},{-9131,76}},
	{{669,68},{2008,68},{3348,68},{4687,68},{6027,71},{7366,73},{8706,75},{10045,77},{-669,68},{-2008,68},{-3348,68},{-4687,68},{-6027,71},{-7366,73},{-8706,75},{-10045,77}},
	{{736,69},{2209,69},{3683,69},{5156,69},{6630,72},{8103,74},{9577,76},{11050,78},{-736,69},{-2209,69},{-3683,69},{-5156,69},{-6630,72},{-8103,74},{-9577,76},{-11050,78}},
	{{810,70},{2431,70},{4052,70},{5673,70},{7294,73},{8915,75},{10536,77},{12157,79},{-810,70},{-2431,70},{-4052,70},{-5673,70},{-7294,73},{-8915,75},{-10536,77},{-12157,79}},
	{{891,71},{2674,71},{4457,71},{6240,71},{8023,74},{9806,76},{11589,78},{13372,80},{-891,71},{-2674,71},{-4457,71},{-6240,71},{-8023,74},{-9806,76},{-11589,78},{-13372,80}},
	{{980,72},{2941,72},{4902,72},{6863,72},{8825,75},{10786,77},{12747,79},{14708,81},{-980,72},{-2941,72},{-4902,72},{-6863,72},{-8825,75},{-10786,77},{-12747,79},{-14708,81}},
	{{1078,73},{3235,73},{5393,73},{7550,73},{9708,76},{11865,78},{14023,80},{16180,82},{-1078,73},{-3235,73},{-5393,73},{-7550,73},{-9708,76},{-11865,78},{-14023,80},{-16180,82}},
	{{1186,74},{3559,74},{5932,74},{8305,74},{10679,77},{13052,79},{15425,81},{17798,83},{-1186,74},{-3559,74},{-5932,74},{-8305,74},{-10679,77},{-13052,79},{-15425,81},{-17798,83}},
	{{1305,75},{3915,75},{6526,75},{9136,75},{11747,78},{14357,80},{16968,82},{19578,84},{-1305,75},{-3915,75},{-6526,75},{-9136,75},{-11747,78},{-14357,80},{-16968,82},{-19578,84}},
	{{1435,76},{4306,76},{7178,76},{10049,76},{12922,79},{15793,81},{18665,83},{21536,85},{-1435,76},{-4306,76},{-7178,76},{-10049,76},{-12922,79},{-15793,81},{-18665,83},{-21536,85}},
	{{1579,77},{4737,77},{7896,77},{11054,77},{14214,80},{17372,82},{20531,84},{23689,86},{-1579,77},{-4737,77},{-7896,77},{-11054,77},{-14214,80},{-17372,82},{-20531,84},{-23689,86}},
	{{1737,78},{5211,78},{8686,78},{12160,78},{15636,81},{19110,83},{22585,85},{26059,87},{-1737,78},{-5211,78},{-8686,78},{-12160,78},{-15636,81},{-19110,83},{-22585,85},{-26059,87}},
	{{1911,79},{5733,79},{9555,79},{13377,79},{17200,82},{21022,84},{24844,86},{28666,88},{-1911,79},{-5733,79},{-9555,79},{-13377,79},{-17200,82},{-21022,84},{-24844,86},{-28666,88}},
	{{2102,80},{6306,80},{10511,80},{14715,80},{18920,83},{23124,85},{27329,87},{31533,88},{-2102,80},{-6306,80},{-10511,80},{-14715,80},{-18920,83},{-23124,85},{-27329,87},{-31533,88}},
	{{2312,81},{6937,81},{11562,81},{16187,81},{20812,84},{25437,86},{30062,88},{34687,88},{-2312,81},{-6937,81},{-11562,81},{-16187,81},{-20812,84},{-25437,86},{-30062,88},{-34687,88}},
	{{2543,82},{7630,82},{12718,82},{17805,82},{22893,85},{27980,87},{33068,88},{38155,88},{-2543,82},{-7630,82},{-12718,82},{-17805,82},{-22893,85},{-27980,87},{-33068,88},{-38155,88}},
	{{2798,83},{8394,83},{13990,83},{19586,83},{25183,86},{30779,88},{36375,88},{41971,88},{-2798,83},{-8394,83},{-13990,83},{-19586,83},{-25183,86},{-30779,88},{-36375,88},{-41971,88}},
	{{3077,84},{9232,84},{15388,84},{21543,84},{27700,87},{33855,88},{40011,88},{46166,88},{-3077,84},{-9232,84},{-15388,84},{-21543,84},{-27700,87},{-33855,88},{-40011,88},{-46166,88}},
	{{3385,85},{10156,85},{16928,85},{23699,85},{30471,88},{37242,88},{44014,88},{50785,88},{-3385,85},{-10156,85},{-16928,85},{-23699,85},{-30471,88},{-37242,88},{-44014,88},{-50785,88}},
	{{3724,86},{11172,86},{18621,86},{26069,86},{33518,88},{40966,88},{48415,88},{55863,88},{-3724,86},{-11172,86},{-18621,86},{-26069,86},{-33518,88},{-40966,88},{-48415,88},{-55863,88}},
	{{4095,87},{12286,87},{20478,87},{28669,87},{36862,88},{45053,88},{53245,88},{61436,88},{-4095,87},{-12286,87},{-20478,87},{-28669,87},{-36862,88},{-45053,88},{-53245,88},{-61436,88}}
};

#else
static const int ima_index_table[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

static const int ima_step_table[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

#endif

static int ima_expand(int nibble, int *predictor, int *index)
{
#ifdef HALO_WIIU
	const struct ima_decode_entry *entry = &ima_decode_table[*index][nibble];

	*predictor += entry->difference;
	if (*predictor > 32767) *predictor = 32767;
	if (*predictor < -32768) *predictor = -32768;
	*index = entry->next_index;
	return *predictor;
#else
	int step = ima_step_table[*index];
	int difference = step >> 3;

	if (nibble & 1) difference += step >> 2;
	if (nibble & 2) difference += step >> 1;
	if (nibble & 4) difference += step;
	if (nibble & 8) difference = -difference;
	*predictor += difference;
	if (*predictor > 32767) *predictor = 32767;
	if (*predictor < -32768) *predictor = -32768;
	*index += ima_index_table[nibble];
	if (*index < 0) *index = 0;
	if (*index > 88) *index = 88;
	return *predictor;
#endif
}

/* Xbox ADPCM: per block, a 4-byte header per channel (first sample, step
index), then 4-byte groups of eight nibbles, low nibble first, alternating
between channels; 64 samples per channel. The header's sample is the block's
first, and the nibbles code the 63 after it: the 64th nibble only pads the
block (the maps' sounds always have 0 there), and decoding it in place of the
header's sample put a wrong sample in every 64, a buzz at 344 Hz in 22 kHz
sounds. */
static short *decode_adpcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long block_bytes = XBOX_ADPCM_BLOCK_BYTES * channels;
	unsigned long blocks = size / block_bytes;
	short *samples = packet_allocate((blocks ? blocks : 1) * XBOX_ADPCM_BLOCK_SAMPLES * channels * sizeof(short));
	unsigned long block, channel;

	if (!samples)
	{
		*frame_count = 0;
		return NULL;
	}
	for (block = 0; block < blocks; block++)
	{
		const unsigned char *data = source + block * block_bytes;
		short *output = samples + block * XBOX_ADPCM_BLOCK_SAMPLES * channels;

		for (channel = 0; channel < channels; channel++)
		{
			const unsigned char *header = data + channel * 4;
			int predictor = (short)(header[0] | (header[1] << 8));
			int index = header[2] > 88 ? 88 : header[2];
			unsigned long group, byte;

			output[channel] = (short)predictor;
			for (group = 0; group < 8; group++)
			{
				const unsigned char *nibbles = data + 4 * channels + (group * channels + channel) * 4;

				for (byte = 0; byte < 4; byte++)
				{
					/* nibble n codes sample n + 1 */
					unsigned long sample = group * 8 + byte * 2 + 1;

					output[sample * channels + channel] = (short)ima_expand(nibbles[byte] & 0xf, &predictor, &index);
					if (sample + 1 < XBOX_ADPCM_BLOCK_SAMPLES)
						output[(sample + 1) * channels + channel] = (short)ima_expand(nibbles[byte] >> 4, &predictor, &index);
				}
			}
		}
	}
	*frame_count = blocks * XBOX_ADPCM_BLOCK_SAMPLES;
	return samples;
}

static short *decode_pcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long frames = size / (2 * channels);
	short *samples = packet_allocate((frames ? frames : 1) * channels * sizeof(short));

	if (samples)
		memcpy(samples, source, frames * channels * sizeof(short));
	*frame_count = samples ? frames : 0;
	return samples;
}

/* ---------- 3D */

static float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* DirectSound's inverse distance law, held beyond the maximum distance. The
game sets a voice's maximum distance before its minimum, so the maximum can
be under the minimum for a moment: it is held at the minimum */
static float distance_attenuation(const struct sdl_stream *stream, float distance, float rolloff_factor)
{
	float minimum = stream->minimum_distance;
	float maximum = stream->maximum_distance > minimum ? stream->maximum_distance : minimum;

	if (minimum <= 0.0f || distance <= minimum)
		return 1.0f;
	if (distance > maximum)
		distance = maximum;
	return minimum / (minimum + rolloff_factor * (distance - minimum));
}

/* ---------- Doppler

The game gives each 3D voice its velocity (units a second, relative to the
listener: sound_manager.c update_channels) and the Xbox's DirectSound shifted
its pitch by them, as a sound passing by is heard: a Banshee, a Ghost or a
plasma bolt rising as it comes and falling as it goes. An Xbox's shift is
the first order one: the frequency times 1 + (D v_listener - D v_source) / c,
c the speed of sound, D the Doppler factor, the velocities' parts along the
line from the listener to the source in metres a second (the distance
factor's metres a unit), the source's away from the listener, the
listener's towards the source. Recorded on an Xbox, a source passing at 20
units a second was shifted 1.178 coming and 0.822 going, and at 40, 1.356
and 0.644: the same either way, and twice as far at twice the speed, where
(c + D v_listener) / (c + D v_source) gives 1.216 and 0.849, then 1.551 and
0.738. Each part is held within the speed of sound, and the shift to an
octave either way, so that a projectile passing close by, or a velocity
that jumps (a teleport), takes no voice up out of hearing. A voice's shift
is the one its position and velocities give at each mix, moved to over
PARAMETER_RAMP_FRAMES as a change of its pitch is (doppler_heard): an
Xbox's moved with each of the game's updates and settled within 2 ms.
Recorded on an Xbox, a source passing at 40 units a second, its position set
every 34 ms, stepped its pitch at each set; a glide over 80 ms (as the mixer
had) was 166 cents off it, and 37 cents at 20 units a second. */

static float doppler_ratio(const struct sdl_stream *stream)
{
	float offset[3], distance, source_speed, listener_speed = 0.0f, ratio, limit = DOPPLER_SPEED_OF_SOUND;
	int axis;

	if (stereo_doppler_enabled && !stream->has_3d && stream->stereo_positioned)
	{
		ratio = 1.0f + stream->stereo_approach * listener.distance_factor * listener.doppler_factor / DOPPLER_SPEED_OF_SOUND;
		return ratio < 0.5f ? 0.5f : ratio > 2.0f ? 2.0f : ratio;
	}
	if (!stream->has_3d || stream->mode == DS3DMODE_DISABLE)
		return 1.0f;
	for (axis = 0; axis < 3; axis++)
	{
		offset[axis] = stream->position[axis] -
			(stream->mode == DS3DMODE_HEADRELATIVE ? 0.0f : listener.position[axis]);
	}
	distance = sqrtf(dot3(offset, offset));
	if (distance < 1.0e-4f)
		return 1.0f;
	source_speed = listener.doppler_factor * listener.distance_factor * dot3(stream->velocity, offset) / distance;
	/* (head-relative: the voice's velocity is the listener's already) */
	if (stream->mode != DS3DMODE_HEADRELATIVE)
		listener_speed = listener.doppler_factor * listener.distance_factor * dot3(listener.velocity, offset) / distance;
	source_speed = source_speed < -limit ? -limit : source_speed > limit ? limit : source_speed;
	listener_speed = listener_speed < -limit ? -limit : listener_speed > limit ? limit : listener_speed;
	ratio = 1.0f + (listener_speed - source_speed) / DOPPLER_SPEED_OF_SOUND;
	if (ratio > DOPPLER_MAXIMUM_RATIO)
		ratio = DOPPLER_MAXIMUM_RATIO;
	if (ratio < 1.0f / DOPPLER_MAXIMUM_RATIO)
		ratio = 1.0f / DOPPLER_MAXIMUM_RATIO;
	return ratio;
}

/* the shift a voice plays in a mix: its doppler_ratio, kept for the
voices' report (the step ramps to it, mix_voice) */
static float doppler_heard(struct sdl_stream *stream)
{
	stream->current_doppler = doppler_ratio(stream);
	return stream->current_doppler;
}

#include "dsound_xbox_hrtf.inl"

/* On 5.1 (AC-3) the Xbox played a 3D voice from the front pair of speakers
and the back pair, never the center or the LFE: the same pair of ears' sound
on both (the HRTF's, or the pan's), a sound behind heard as the direction
mirrored ahead (azimuth 180 - azimuth), and its power split between the
pairs by its direction, DirectSound's azimuth and elevation (hrtf_angles):
the back pair's share 0.5 + (1 - |elevation| / 90)(|azimuth| / 90 - 1),
held to 0 to 1. Ahead it is all in the front pair, behind all in the back,
to the side half in each, above and below towards half. Recorded on an Xbox
(its AC-3 decoded), a sweep at each 30 degrees around and at 45 and 135
degrees 40 above and below: each pair's two speakers alike in time and level
ahead and behind, both pairs the same time and level apart between their
speakers to the side (660 us, the HRTF's), the back pair's share 0, 0, 0.21,
0.50, 0.79, 1, 1 from ahead round to behind, 0.21 and 0.79 at 45 and 135
degrees, up or down; the center and the LFE silent. So here; on stereo the
front pair alone, all round (spatialize). pair: the front pair's gain and
the back pair's */
static void surround_split(float right, float up, float ahead, float *pair)
{
	float azimuth = 0.0f, elevation = 0.0f, share;

	if (output_channels != 6)
	{
		pair[0] = 1.0f;
		pair[1] = 0.0f;
		return;
	}
	if (right * right + up * up + ahead * ahead > 1.0e-8f)
		hrtf_angles(right, up, ahead, &azimuth, &elevation);
	share = 0.5f + (1.0f - fabsf(elevation) / 90.0f) * (fabsf(azimuth) / 90.0f - 1.0f);
	share = share < 0.0f ? 0.0f : share > 1.0f ? 1.0f : share;
	pair[0] = sqrtf(1.0f - share);
	pair[1] = sqrtf(share);
}

/* the gains of a 3D voice to the left and right (the pan's), the gains of
the front and back pairs of speakers (surround_split) and the distance
rolloff's attenuation (distance_gain); for a mono voice through the HRTF
(hrtf_on: chosen, or crossfading from it), its gain to each ear and its way
there (hrtf_out) */
static void spatialize(const struct sdl_stream *stream, float *left, float *right, float *pair,
	float *distance_gain, BOOL *hrtf_on, struct hrtf_parameters *hrtf_out)
{
	float offset[3], right_axis[3], distance, attenuation, pan, side, ahead, up;
	int axis;

	if (stream->mode == DS3DMODE_HEADRELATIVE)
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis];
		side = offset[0];
		up = offset[1];
		ahead = offset[2];
	}
	else
	{
		for (axis = 0; axis < 3; axis++)
			offset[axis] = stream->position[axis] - listener.position[axis];
		/* left-handed: right = top x front */
		right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
		right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
		right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
		side = dot3(offset, right_axis);
		up = dot3(offset, listener.top);
		ahead = dot3(offset, listener.front);
	}
	/* in the game's units, those of the minimum and maximum distances: the
	distance factor turns units into meters for Doppler, and scaling by it
	here put every 3D sound 3 times as far away, up to 10 dB quieter */
	distance = sqrtf(dot3(offset, offset));
	attenuation = distance_attenuation(stream, distance, listener.rolloff_factor);
	*distance_gain = attenuation;

	/* equal power pan; close sources and sources straight ahead or behind
	stay centred, and neither ear drops below a quarter */
	{
		float horizontal = sqrtf(side * side + ahead * ahead);
		float angle;

		pan = horizontal > 1.0e-4f ? side / horizontal : 0.0f;
		if (distance < stream->minimum_distance && stream->minimum_distance > 0.0f)
			pan *= distance / stream->minimum_distance;
		pan *= 0.75f;
		angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		*left = cosf(angle) * 1.41421356f * 0.70710678f;
		*right = sinf(angle) * 1.41421356f * 0.70710678f;
	}
	/* and through the HRTF, each ear at the pan's gain in the middle (-3
	dB), the head's filters doing the rest */
	surround_split(side, up, ahead, pair);
	*hrtf_on = stream->channels == 1 && (hrtf_enabled || stream->hrtf_mix > 0.0f);
	if (*hrtf_on)
	{
		/* (on 5.1 a sound behind as the direction mirrored ahead) */
		hrtf_place(side, up, output_channels == 6 ? fabsf(ahead) : ahead, distance,
			output_channels == 6 && ahead < 0.0f, hrtf_out);
		hrtf_out->gain = 0.70710678f * attenuation * gain_from_millibels(stream->direct);
		hrtf_out->pair[0] = pair[0];
		hrtf_out->pair[1] = pair[1];
	}
	*left *= attenuation * gain_from_millibels(stream->direct);
	*right *= attenuation * gain_from_millibels(stream->direct);
}

/* the voice's gains to the left and right, and to the reverb (only 3D voices
send to it, as their I3DL2 mix bin did on the Xbox), with the coefficients of
the low passes of its direct path (3D voices: obstruction and occlusion
muffle it) and of the send; a 3D voice's gains of the front and back pairs
of speakers (pair: surround_split; others 1 and 0); a 3D voice through the
HRTF (hrtf_on) has its way to each ear too (hrtf_out) */
#ifdef RESAMPLER_GAIN_CACHE_TEST
static unsigned long voice_gain_recalculations;
#endif

#ifdef VOICE_GAIN_CACHE
static void voice_gains_uncached(const struct sdl_stream *stream, float *left, float *right, float *pair, float *room,
	float *direct_lowpass, float *room_lowpass, BOOL *hrtf_on, struct hrtf_parameters *hrtf_out)
#else
static void voice_gains(const struct sdl_stream *stream, float *left, float *right, float *pair, float *room,
	float *direct_lowpass, float *room_lowpass, BOOL *hrtf_on, struct hrtf_parameters *hrtf_out)
#endif
{
	float level_3d = -1.0f, level_direct = -1.0f;

#ifdef RESAMPLER_GAIN_CACHE_TEST
	voice_gain_recalculations++;
#endif
	pair[0] = 1.0f;
	pair[1] = 0.0f;
	*room = 0.0f;
	*direct_lowpass = 0.0f;
	*room_lowpass = 0.0f;
	*hrtf_on = FALSE;
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		float distance_gain, cosine = frequency_cosine(environment.flHFReference);

		spatialize(stream, left, right, pair, &distance_gain, hrtf_on, hrtf_out);
		level_3d = VOICE_LEVEL_3D * distance_gain * stream->volume;
		level_direct = level_3d * gain_from_millibels(stream->direct);
		if (stream->direct_hf < stream->direct)
			*direct_lowpass = lowpass_coefficient(gain_from_millibels(stream->direct_hf - stream->direct), cosine);
		if (reverb_enabled)
		{
			LONG level = environment.lRoom + stream->room;
			LONG high_level = environment.lRoom + environment.lRoomHF + stream->room_hf;

			/* VOICE_LEVEL_3D times the room level and the direct path's
			distance rolloff, whatever the room rolloff: recorded on an
			Xbox (issue 792's eighth run), the cave's reverb of a noise
			burst 0.5 to 20 m away (minimum distance 1 m) is 6.0 dB over
			its room level and rolloff to within 0.1 dB, at room levels of
			-20, -10 and 0 dB and the source's own -10 alike, and a room
			rolloff factor of 1 leaves it as it was. (#790's cap at the
			rolloff, fitted at 2 and 10 m, was 4 dB loud from 3 m and 6
			dB quiet within 1 m) */
			*room = VOICE_LEVEL_3D * gain_from_millibels(level) * distance_gain;
			if (*room > 0.0f && high_level < level)
				*room_lowpass = lowpass_coefficient(gain_from_millibels(high_level - level), cosine);
		}
	}
	else if (stream->channels == 2 && stream->stereo_positioned)
	{
		/* the equal power pan of a 3D voice, at the gains of a 2D one when
		centred (the game fades it with distance), its direct path muffled
		and its room send made as a 3D voice's are (SetI3DL2Source) */
		float angle = (stream->stereo_pan + 1.0f) * 0.25f * 3.14159265f;
		float direct = gain_from_millibels(stream->direct);
		float cosine = frequency_cosine(environment.flHFReference);

		*left = cosf(angle) * 1.41421356f * stream->mix_left * direct;
		*right = sinf(angle) * 1.41421356f * stream->mix_right * direct;
		if (stream->direct_hf < stream->direct)
			*direct_lowpass = lowpass_coefficient(gain_from_millibels(stream->direct_hf - stream->direct), cosine);
		if (reverb_enabled)
		{
			LONG level = environment.lRoom + stream->room;
			LONG high_level = environment.lRoom + environment.lRoomHF + stream->room_hf;

			/* the room's rolloff with distance, a 3D voice's; the volume
			holds the game's fade with it, which a 3D voice's room send does
			not take, so it is taken back out (no further than a twentieth:
			past that the send fades out with the sound) */
			*room = gain_from_millibels(level) *
				distance_attenuation(stream, stream->stereo_distance,
					environment.flRoomRolloffFactor + stream->room_rolloff_factor) /
				(stream->stereo_distance_fade > 0.05f ? stream->stereo_distance_fade : 0.05f);
			if (*room > 0.0f && high_level < level)
				*room_lowpass = lowpass_coefficient(gain_from_millibels(high_level - level), cosine);
		}
	}
	else
	{
		*left = stream->mix_left;
		*right = stream->mix_right;
	}
	/* (the Xbox's quietest levels before audio.volume, which it did not
	have: voice_level) */
	if (level_3d < 0.0f)
	{
		*left = voice_level(*left * stream->volume) * master_volume;
		*right = voice_level(*right * stream->volume) * master_volume;
		*room = voice_level(*room * stream->volume) * master_volume;
	}
	else
	{
		/* a 3D voice's are those of its volume and distance rolloff
		together, at VOICE_LEVEL_3D times, its other gains after them:
		recorded on an Xbox (issue 714's seventh run), a voice 10 m away
		(-20 dB of rolloff, -23 with the pan's) stepped down from -20 dB
		played as a 2D voice 14 dB quieter would, its direct path and its
		reverb alike: exact to -30 dB, -40 at -42.8, -42 at -52.1, silent
		from -44 (its whole level's would be silent from -34). Its direct
		path's are of its direct level too (obstruction and occlusion
		with it), its reverb's not: recorded again (issue 792's eighth
		run), voices 0.5 to 40 m away, ahead, to the right and at 45
		degrees, went silent where this says to within a dB, each ear at
		the same step (the pan after it), and a voice 10 m away with a
		direct level of -10 dB, or obstructed by -10 dB, 10 dB sooner,
		its reverb where the voice's without it went. The HRTF's gain
		too: without it every mono 3D voice on headphones played at full
		volume, whatever audio.volume, audio.effects_volume and the
		game's own gain said, and only the 2D voices followed them */
		float scale = level_direct > 0.0f ?
			voice_level(level_direct) / level_direct * stream->volume * master_volume : 0.0f;
		float room_scale = level_3d > 0.0f ? voice_level(level_3d) / level_3d * stream->volume * master_volume : 0.0f;

		*left *= scale;
		*right *= scale;
		*room *= room_scale;
		if (*hrtf_on)
			hrtf_out->gain *= scale;
	}
}

#ifdef VOICE_GAIN_CACHE
static void voice_gain_inputs_read(const struct sdl_stream *stream, struct voice_gain_inputs *key)
{
	/* Initialize padding too (the host tests can have 64-bit longs). */
	memset(key, 0, sizeof(*key));
	key->real[0] = stream->volume;
	key->real[1] = stream->mix_left;
	key->real[2] = stream->mix_right;
	key->real[3] = stream->stereo_pan;
	key->real[4] = stream->stereo_distance;
	key->real[5] = stream->stereo_distance_fade;
	key->real[6] = stream->position[0];
	key->real[7] = stream->position[1];
	key->real[8] = stream->position[2];
	key->real[9] = stream->minimum_distance;
	key->real[10] = stream->maximum_distance;
	key->real[11] = stream->room_rolloff_factor;
	key->real[12] = master_volume;
	key->real[13] = listener.position[0];
	key->real[14] = listener.position[1];
	key->real[15] = listener.position[2];
	key->real[16] = listener.front[0];
	key->real[17] = listener.front[1];
	key->real[18] = listener.front[2];
	key->real[19] = listener.top[0];
	key->real[20] = listener.top[1];
	key->real[21] = listener.top[2];
	key->real[22] = listener.rolloff_factor;
	key->real[23] = environment.flHFReference;
	key->real[24] = environment.flRoomRolloffFactor;
	key->integer[0] = stream->channels;
	key->integer[1] = stream->has_3d;
	key->integer[2] = stream->mode;
	key->integer[3] = stream->stereo_positioned;
	key->integer[4] = stream->direct;
	key->integer[5] = stream->direct_hf;
	key->integer[6] = stream->room;
	key->integer[7] = stream->room_hf;
	key->integer[8] = reverb_enabled;
	key->integer[9] = environment.lRoom;
	key->integer[10] = environment.lRoomHF;

}

/* Called with mixer_lock held, as are all setters of the key's inputs. */
static void voice_gains(struct sdl_stream *stream, float *left, float *right, float *pair, float *room,
	float *direct_lowpass, float *room_lowpass, BOOL *hrtf_on, struct hrtf_parameters *hrtf_out)
{
	struct voice_gain_inputs key;

	/* a mono voice that the HRTF takes (audio.hrtf, off by default on the Wii U) or is leaving has its way to each ear to work out too, which the
	cache does not hold: the cache is not used then (and what it holds is dropped) */
	if (stream->channels == 1 && (hrtf_enabled || stream->hrtf_mix > 0.0f))
	{
		stream->gain_cache_valid = FALSE;
		voice_gains_uncached(stream, left, right, pair, room, direct_lowpass, room_lowpass, hrtf_on, hrtf_out);
		return;
	}
	voice_gain_inputs_read(stream, &key);
	if (!stream->gain_cache_valid || memcmp(&key, &stream->gain_cache_inputs, sizeof(key)))
	{
		voice_gains_uncached(stream, &stream->gain_cache_target[0], &stream->gain_cache_target[1],
			&stream->gain_cache_target[5], &stream->gain_cache_target[2], &stream->gain_cache_target[3],
			&stream->gain_cache_target[4], hrtf_on, hrtf_out);
		memcpy(&stream->gain_cache_inputs, &key, sizeof(key));
		stream->gain_cache_valid = TRUE;
	}
	*left = stream->gain_cache_target[0];
	*right = stream->gain_cache_target[1];
	*room = stream->gain_cache_target[2];
	*direct_lowpass = stream->gain_cache_target[3];
	*room_lowpass = stream->gain_cache_target[4];
	pair[0] = stream->gain_cache_target[5];
	pair[1] = stream->gain_cache_target[6];
	*hrtf_on = FALSE;
}
#endif

/* the 5.1 speakers around the listener, clockwise from the center, and
their directions in radians (ITU-R BS.775: fronts at 30 degrees, backs at
110) */
#define SURROUND_RING_SPEAKERS 5
static const unsigned long surround_ring_channel[SURROUND_RING_SPEAKERS] = { 2, 1, 5, 4, 0 };
static const float surround_ring_angle[SURROUND_RING_SPEAKERS + 1] =
{
	0.0f, 0.52359878f, 1.91986218f, 4.36332313f, 5.75958653f, 6.28318531f,
};

/* the speaker gains of a sound from a direction, side (right) and ahead:
vector base amplitude panning (Pulkki's VBAP, in the plane) between the two
speakers either side of it, blended (in power) towards all five equally by
spread, 0 to 1. Their power sums to 1, as the stereo pan's does.

VBAP takes the gains g1 and g2 whose sum g1 l1 + g2 l2 of the speakers'
directions l1 and l2 points the sound's way p, then scales them to a power
of 1: g1 = sin(a2 - a) / sin(a2 - a1) and g2 = sin(a - a1) / sin(a2 - a1),
a the sound's angle, a1 and a2 the speakers', the common divisor gone with
the scaling. So a sound at a speaker plays from it alone, one between two
from both, as the tangent law of stereo panning puts a phantom image (the
listener between them hears it where it is), and no pair is wider than 180
degrees (the backs' 140 is the widest). */
static void surround_pan(float side, float ahead, float spread, float *gains)
{
	float angle = atan2f(side, ahead);
	unsigned long segment, speaker;

	for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		gains[speaker] = 0.0f;
	if (angle < 0.0f)
		angle += 6.28318531f;
	for (segment = 0; segment < SURROUND_RING_SPEAKERS - 1; segment++)
	{
		if (angle < surround_ring_angle[segment + 1])
			break;
	}
	{
		float first = sinf(surround_ring_angle[segment + 1] - angle);
		float second = sinf(angle - surround_ring_angle[segment]);
		float length;

		first = first > 0.0f ? first : 0.0f;
		second = second > 0.0f ? second : 0.0f;
		length = sqrtf(first * first + second * second);
		if (length < 1.0e-6f)
		{
			first = 1.0f;
			length = 1.0f;
		}
		gains[surround_ring_channel[segment]] = first / length;
		gains[surround_ring_channel[(segment + 1) % SURROUND_RING_SPEAKERS]] = second / length;
	}
	for (speaker = 0; speaker < SURROUND_RING_SPEAKERS; speaker++)
	{
		float *gain = &gains[surround_ring_channel[speaker]];

		*gain = sqrtf((1.0f - spread) * *gain * *gain + spread / SURROUND_RING_SPEAKERS);
	}
}

/* on 5.1, a voice's gains from each source channel to each speaker (the
direct path's; its room send and low passes are voice_gains'):
	- a 3D voice's left and right gains (voice_gains: the pan's, with its
	  volume) on the front pair and the back pair, at their gains (pair,
	  surround_split), a mono voice's both from its one channel, a stereo
	  voice's from its left and right; through the HRTF, its way to each
	  ear (hrtf_process) instead, crossfaded with these as the choice
	  changes;
	- a stereo voice positioned in the world has its channels' middle (the
	  first row) panned towards it in the front half, and their difference
	  (the second) kept on the front pair as wide as the stereo mix keeps it;
	- a 2D voice goes by its mix bins. */
static void voice_surround_gains(const struct sdl_stream *stream, float left, float right, const float *pair,
	float gains[2][MAXIMUM_OUTPUT_CHANNELS])
{
	unsigned long speaker;

	memset(gains, 0, 2 * sizeof(gains[0]));
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		unsigned long second = stream->channels == 2 ? 1 : 0;

		gains[0][0] = left * pair[0];
		gains[0][4] = left * pair[1];
		gains[second][1] = right * pair[0];
		gains[second][5] = right * pair[1];
		return;
	}
	if (stream->channels == 2 && stream->stereo_positioned)
	{
		float pan = stream->stereo_pan < -1.0f ? -1.0f : stream->stereo_pan > 1.0f ? 1.0f : stream->stereo_pan;
		float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		float direct = gain_from_millibels(stream->direct);
		float far_gain = (cosf(angle) < sinf(angle) ? cosf(angle) : sinf(angle)) * 1.41421356f;

		/* the middle at the stereo pan's level (1 on each side when centred,
		a power of 2) */
		surround_pan(sinf(pan * 1.57079633f), cosf(pan * 1.57079633f), 0.1f, gains[0]);
		for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
			gains[0][speaker] *= 1.41421356f * stream->mix_left * direct;
		gains[1][0] = far_gain * stream->mix_left * direct;
		gains[1][1] = -far_gain * stream->mix_right * direct;
	}
	else
	{
		memcpy(gains, stream->surround_bins, 2 * sizeof(gains[0]));
		if (stream->channels == 1)
			memset(gains[1], 0, sizeof(gains[1]));
	}
	/* (voice_level, as voice_gains) */
	for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
	{
		gains[0][speaker] = voice_level(gains[0][speaker] * stream->volume) * master_volume;
		gains[1][speaker] = voice_level(gains[1][speaker] * stream->volume) * master_volume;
	}
}

/* ---------- resampling

Each voice is resampled to the output rate by band-limited interpolation (J.
O. Smith's): an output sample is the source frames around its moment, each
weighted by a windowed sinc low pass centred there. The low pass keeps
RESAMPLER_CUTOFF of the source's band (a 22 kHz voice is 0.5 dB down at 10
kHz) and takes the images of it out (80 dB down), but for those of its last
few hundred hertz, which lie beside them, about 20 dB down. Linear
interpolation, which the mixer did before, left the images only 8 to 20 dB
down, a gritty haze above 11 kHz over every 22 kHz voice. A voice
played faster than the output rate takes its frames (a step over 1) gets the
low pass narrowed to match, up to RESAMPLER_MAXIMUM_STRETCH times, so it
does not alias. The frames come from the voice's packets in turn, so the low
pass reads straight across a packet's end into the next. */

/* the low pass's one side, RESAMPLER_TABLE_STEPS values a source frame */
static float resampler_table[RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS + 2];
/* the same low pass as the weights of a voice's taps 1 - RESAMPLER_ZERO_CROSSINGS
to RESAMPLER_ZERO_CROSSINGS, at each of RESAMPLER_TABLE_STEPS phases between two
source frames (and one row more, for the last's blend): a voice at the output
rate or slower blends two rows by its phase, which is what the table gave tap
by tap, a third of the work. resampler_deltas holds each row's step to the
next, so the blend is a multiply and an add a tap */
#define RESAMPLER_TAPS (2 * RESAMPLER_ZERO_CROSSINGS)
#if defined(HALO_WIIU) && RESAMPLER_TAPS != 48
#error The qualified Wii U audio kernels require exactly 48 resampler taps
#endif
static float resampler_phases[RESAMPLER_TABLE_STEPS + 1][RESAMPLER_TAPS] RESAMPLER_ALIGNED;
static float resampler_deltas[RESAMPLER_TABLE_STEPS][RESAMPLER_TAPS] RESAMPLER_ALIGNED;
/* the Xbox's resampler, as a voice's reverb send hears it: a sinc low pass at
the source's own Nyquist rate, Hann windowed to SEND_ZERO_CROSSINGS frames
each side (six taps), and never narrowed for a voice played faster than the
output rate, so that it lets through about half of the source's top few
kilohertz's images (-6 dB at the source's Nyquist rate) and aliases what a
voice pitched up has above the output's. The reverb takes every other frame
of the send unfiltered (reverb_process), which folds the send's top octave
onto its bottom one: so a 44.1 kHz voice's images at 22 to 24 kHz are the
reverb's low end. Recorded on an Xbox (OpenH1 issue 714, its sixth run:
clicks and noise in the cave at 22.05, 44.1 and 48 kHz, at pitches 0.75 to
1.5), a 44.1 kHz click's reverberation is 3.6 dB louder below 1 kHz at pitch
1 than through the mixer's resampler, 6 dB pitched up 1.5 times, and a 48
kHz one's 1 dB at 0.75; this low pass gives each within 0.5 dB (a width of
three frames each side fits best, of two to eight, and a cutoff at the
source's Nyquist rate, of 0.9 to 1.1 of it). At a phase of 0 it is the
frames themselves, so a voice at the output's rate sends its frames as they
are, as the Xbox's did. The direct path has a longer one (direct_phases). A row
a phase, and its step to the next, as resampler_phases and resampler_deltas */
static float send_phases[RESAMPLER_TABLE_STEPS + 1][SEND_TAPS] RESAMPLER_ALIGNED;
static float send_deltas[RESAMPLER_TABLE_STEPS][SEND_TAPS] RESAMPLER_ALIGNED;

#if defined(HALO_WIIU) || defined(RESAMPLER_CACHE_TEST)
#define RESAMPLER_COEFFICIENT_CACHE
#define RESAMPLER_CACHE_WAYS 4
/* The Wii U has no SIMD for blending the same weights at every voice's
output sample. Keep four exact blends per phase row; a miss computes the
same coefficients, and replacement only changes how much work is done.
All access is from the mixer while it holds mixer_lock. */
struct resampler_coefficient_entry
{
	unsigned int blend_bits;
	BOOL valid;
	float weights[RESAMPLER_TAPS] RESAMPLER_ALIGNED;
};
typedef char resampler_float_key_size[sizeof(float) == sizeof(unsigned int) ? 1 : -1];
static struct resampler_coefficient_entry resampler_coefficient_cache[RESAMPLER_TABLE_STEPS][RESAMPLER_CACHE_WAYS];
static unsigned char resampler_cache_victim[RESAMPLER_TABLE_STEPS];
#ifdef RESAMPLER_CACHE_TEST
static unsigned long resampler_cache_hits, resampler_cache_misses;
#endif

/* These exact rates revisit a small set of phases. Arbitrary pitches keep
the original blending path instead of filling the cache with one-use keys. */
static BOOL resampler_cache_frequency(unsigned long frequency)
{
	switch (frequency)
	{
	case 8000: case 11025: case 12000: case 16000: case 22050:
	case 24000: case 32000: case 44100: case 48000:
		return TRUE;
	default:
		return FALSE;
	}
}

/* Keep the uncommon fill call out of the sample loop's register lifetime. */
static __attribute__((noinline)) const float *resampler_coefficients_fill(unsigned long row, unsigned int bits,
	float blend, unsigned replace)
{
	unsigned tap;
	struct resampler_coefficient_entry *entry;

#ifdef RESAMPLER_CACHE_TEST
	resampler_cache_misses++;
#endif
	if (replace == RESAMPLER_CACHE_WAYS)
	{
		replace = resampler_cache_victim[row];
		resampler_cache_victim[row] = (replace + 1) % RESAMPLER_CACHE_WAYS;
	}
	entry = &resampler_coefficient_cache[row][replace];
	for (tap = 0; tap < RESAMPLER_TAPS; tap++)
		entry->weights[tap] = resampler_phases[row][tap] + resampler_deltas[row][tap] * blend;
	entry->blend_bits = bits;
	entry->valid = TRUE;
	return entry->weights;
}

static __inline__ __attribute__((always_inline)) const float *resampler_coefficients(unsigned long row, float blend)
{
	unsigned int bits;
	unsigned way, replace = RESAMPLER_CACHE_WAYS;
	struct resampler_coefficient_entry *entry;

	memcpy(&bits, &blend, sizeof(bits));
	for (way = 0; way < RESAMPLER_CACHE_WAYS; way++)
	{
		entry = &resampler_coefficient_cache[row][way];
		if (entry->valid && entry->blend_bits == bits)
		{
#ifdef RESAMPLER_CACHE_TEST
			resampler_cache_hits++;
#endif
			return entry->weights;
		}
		if (!entry->valid && replace == RESAMPLER_CACHE_WAYS)
			replace = way;
	}
	return resampler_coefficients_fill(row, bits, blend, replace);
}
#endif

/* the Xbox's resampler on a voice's direct path, audio.resampler's "xbox"
(the default): a low pass at the source's Nyquist rate, never narrowed, over
four source frames each side, as recorded on an Xbox (OpenH1 issue 792, its
eighth run: two trains of sixteen one-sample clicks at 22.05 kHz, pitched 1
and 0.9, each click on another phase, the clicks' frames laid on one time
line by the rate the Xbox played them at). In each source frame from the
center it is a polynomial of the distance into the frame, as here: these
play the recordings back to within -70 dB, with corners at the whole frames,
where the best smooth low pass's is -35 dB. With it the Xbox's 22.05 kHz
voices are played as the Xbox played them: the passband (-2.2 dB at 0.4 of
the source's rate, -6.9 at 0.5; the Kaiser windowed sinc fitted before, -1.9
and -6.0), the images a 4 kHz tone leaves at 0.82, 1.18 and 1.82 of the
source's rate (-63, -71 and -79 dB: the sinc's -70, -99, -101) and the low
pass's lobe from 1.3 to 1.6 of it, at -33 dB, which comes down to the
output's 13 to 19 kHz (issue 714's seventh run: a 22.05 kHz click's images
above 16 kHz, 9 to 40 dB over any smooth low pass's). It is the
coefficients, not their phases: tables of 16 to 64 phases, unblended, fit
the clicks worse than one blended. The Xbox's step a frame is rounded too
(22050 Hz played at 22042.97, 17640 at 17636.72, 24000 and 12000 exactly),
by under 0.6 cents, which is not copied. A row a frame from the center, its
coefficients from the constant up */
static const double direct_kernel[DIRECT_ZERO_CROSSINGS][DIRECT_KERNEL_ORDER + 1] =
{
	{ 1.0007795, -0.2495059, -1.3652166, 0.3404684, 0.4084059, -0.1349805 },
	{ -0.0001219, -0.6004896, 0.7474932, 0.0843083, -0.3094547, 0.0781494 },
	{ -0.0001317, 0.1000168, -0.0728184, -0.1081077, 0.1009553, -0.0199766 },
	{ -0.0001926, -0.0076839, -0.0064829, 0.0419929, -0.0412200, 0.0134312 },
};
/* direct_kernel as a table of phases: a row a phase, and its step to the
next, as resampler_phases */
static float direct_phases[RESAMPLER_TABLE_STEPS + 1][DIRECT_TAPS] RESAMPLER_ALIGNED;
static float direct_deltas[RESAMPLER_TABLE_STEPS][DIRECT_TAPS] RESAMPLER_ALIGNED;

/* how a voice's frames are converted to the output's rate: the Xbox's low
pass (direct_phases), the mixer's windowed sinc low pass, or between the two
frames either side (linear: it lets more of the images above the source's
rate through, which some hear as brighter) */
enum resampler_mode
{
	RESAMPLER_XBOX,
	RESAMPLER_SINC,
	RESAMPLER_LINEAR
};
/* audio.resampler */
static enum resampler_mode resampler_mode = RESAMPLER_XBOX;

/* audio.resampler's mode: the Xbox's for any value but "sinc" and "linear" */
static enum resampler_mode resampler_mode_read(void)
{
	const char *setting = config_string("audio.resampler");

	if (setting && !strcmp(setting, "sinc"))
		return RESAMPLER_SINC;
	return setting && !strcmp(setting, "linear") ? RESAMPLER_LINEAR : RESAMPLER_XBOX;
}

/* Configuration can read files. Read it before calling this publisher;
   the mixer sees the settings together while it holds the same lock as its voices. */
static void mixer_settings_publish(float volume, BOOL enabled, BOOL stereo_doppler, BOOL hrtf, int resampler)
{
	mixer_lock_take();
	master_volume = volume;
	reverb_enabled = enabled;
	stereo_doppler_enabled = stereo_doppler;
	hrtf_enabled = hrtf;
	resampler_mode = resampler;
	pthread_mutex_unlock(&mixer_lock);
}

static double bessel_i0(double x)
{
	double sum = 1.0, term = 1.0;
	int k;

	for (k = 1; k < 64 && term > 1.0e-12 * sum; k++)
	{
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
	}
	return sum;
}

static void resampler_initialize(void)
{
	unsigned long index;

	for (index = 0; index <= RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS; index++)
	{
		double distance = (double)index / RESAMPLER_TABLE_STEPS;
		double edge = distance / RESAMPLER_ZERO_CROSSINGS;
		double angle = 3.14159265358979 * RESAMPLER_CUTOFF * distance;
		double sinc = index ? sin(angle) / angle : 1.0;
		double window = bessel_i0(RESAMPLER_KAISER_BETA * sqrt(1.0 - edge * edge)) / bessel_i0(RESAMPLER_KAISER_BETA);

		resampler_table[index] = (float)(RESAMPLER_CUTOFF * sinc * window);
	}
	resampler_table[RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS + 1] = 0.0f;
}

/* the low pass, distance source frames from its centre times
RESAMPLER_TABLE_STEPS */
static float resampler_weight(float distance)
{
	unsigned long index = (unsigned long)distance;
	float fraction;

	if (index >= RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS)
		return 0.0f;
	fraction = distance - (float)index;
	return resampler_table[index] + (resampler_table[index + 1] - resampler_table[index]) * fraction;
}

static void resampler_phases_initialize(void)
{
	unsigned long phase, tap;

	for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < 2 * RESAMPLER_ZERO_CROSSINGS; tap++)
		{
			float distance = fabsf((float)((long)tap + 1 - RESAMPLER_ZERO_CROSSINGS) -
				(float)phase / RESAMPLER_TABLE_STEPS);

			resampler_phases[phase][tap] = resampler_weight(distance * RESAMPLER_TABLE_STEPS);
		}
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < RESAMPLER_TAPS; tap++)
			resampler_deltas[phase][tap] = resampler_phases[phase + 1][tap] - resampler_phases[phase][tap];
	}
#ifdef RESAMPLER_COEFFICIENT_CACHE
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS; phase++)
	{
		unsigned way;

		resampler_cache_victim[phase] = 0;
		for (way = 0; way < RESAMPLER_CACHE_WAYS; way++)
			resampler_coefficient_cache[phase][way].valid = FALSE;
	}
#ifdef RESAMPLER_CACHE_TEST
	resampler_cache_hits = resampler_cache_misses = 0;
#endif
#endif
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS + 1; phase++)
	{
		for (tap = 0; tap < SEND_TAPS; tap++)
		{
			double distance = (double)((long)tap + 1 - SEND_ZERO_CROSSINGS) - (double)phase / RESAMPLER_TABLE_STEPS;
			double angle = 3.14159265358979 * distance;

			/* (on a frame, exactly it, and 0 at the others) */
			if (phase % RESAMPLER_TABLE_STEPS == 0)
				send_phases[phase][tap] = distance == 0.0 ? 1.0f : 0.0f;
			else if (fabs(distance) >= SEND_ZERO_CROSSINGS)
				send_phases[phase][tap] = 0.0f;
			else
			{
				send_phases[phase][tap] = (float)(sin(angle) / angle *
					(0.5 + 0.5 * cos(angle / SEND_ZERO_CROSSINGS)));
			}
		}
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < SEND_TAPS; tap++)
			send_deltas[phase][tap] = send_phases[phase + 1][tap] - send_phases[phase][tap];
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS + 1; phase++)
	{
		for (tap = 0; tap < DIRECT_TAPS; tap++)
		{
			double distance = fabs((double)((long)tap + 1 - DIRECT_ZERO_CROSSINGS) -
				(double)phase / RESAMPLER_TABLE_STEPS);
			unsigned long frame = (unsigned long)distance;

			/* (on a frame, exactly it, and 0 at the others) */
			if (phase % RESAMPLER_TABLE_STEPS == 0)
				direct_phases[phase][tap] = distance == 0.0 ? 1.0f : 0.0f;
			else if (frame >= DIRECT_ZERO_CROSSINGS)
				direct_phases[phase][tap] = 0.0f;
			else
			{
				double into = distance - (double)frame, weight = 0.0;
				int power;

				for (power = DIRECT_KERNEL_ORDER; power >= 0; power--)
					weight = weight * into + direct_kernel[frame][power];
				direct_phases[phase][tap] = (float)weight;
			}
		}
	}
	for (phase = 0; phase < RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < DIRECT_TAPS; tap++)
			direct_deltas[phase][tap] = direct_phases[phase + 1][tap] - direct_phases[phase][tap];
	}
}

/* The low pass over a voice's frames, the sum of each frame times its weight:
four taps at a time where the compiler has vectors (SSE, NEON), summed in four
lanes, then the lanes together. Only the order of the additions differs from
tap by tap, a difference of the order of float's rounding (-115 dB or less);
RESAMPLER_SCALAR adds them tap by tap (tools/harness/tests/test_mixer.py). */
#if (defined(__GNUC__) || defined(__clang__)) && !defined(RESAMPLER_SCALAR)
typedef float resampler_vector __attribute__((vector_size(16)));
typedef float resampler_unaligned_vector __attribute__((vector_size(16), aligned(4)));
/* (each row of the phase tables whole vectors, aligned) */
typedef char resampler_taps_in_fours[RESAMPLER_TAPS % 4 ? -1 : 1];
#define RESAMPLER_LANES 4
#else
#define RESAMPLER_LANES 1
#endif

/* frames[0, RESAMPLER_TAPS) weighted by the phase table's row blended blend of
the way to the next */
static float resampler_dot(const float *frames, const float *weights, const float *deltas, float blend)
{
#if RESAMPLER_LANES == 4
	resampler_vector sum = { 0.0f, 0.0f, 0.0f, 0.0f }, blends = { blend, blend, blend, blend };
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;

		sum += *(const resampler_unaligned_vector *)(frames + tap) * weight;
	}
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
#else
	float sum = 0.0f;
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap++)
		sum += frames[tap] * (weights[tap] + deltas[tap] * blend);
	return sum;
#endif
}

/* frames[0, DIRECT_TAPS) weighted by a row of direct_phases, as resampler_dot */
static float direct_dot(const float *frames, const float *weights, const float *deltas, float blend)
{
#if RESAMPLER_LANES == 4
	resampler_vector sum = { 0.0f, 0.0f, 0.0f, 0.0f }, blends = { blend, blend, blend, blend };
	int tap;

	for (tap = 0; tap < DIRECT_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;

		sum += *(const resampler_unaligned_vector *)(frames + tap) * weight;
	}
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
#else
	float sum = 0.0f;
	int tap;

	for (tap = 0; tap < DIRECT_TAPS; tap++)
		sum += frames[tap] * (weights[tap] + deltas[tap] * blend);
	return sum;
#endif
}

/* resampler_dot of two channels' frames, with the same weights */
static void resampler_dot2(const float *left_frames, const float *right_frames, const float *weights,
	const float *deltas, float blend, float *left, float *right)
{
#if RESAMPLER_LANES == 4
	resampler_vector left_sum = { 0.0f, 0.0f, 0.0f, 0.0f }, right_sum = left_sum;
	resampler_vector blends = { blend, blend, blend, blend };
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;

		left_sum += *(const resampler_unaligned_vector *)(left_frames + tap) * weight;
		right_sum += *(const resampler_unaligned_vector *)(right_frames + tap) * weight;
	}
	*left = (left_sum[0] + left_sum[1]) + (left_sum[2] + left_sum[3]);
	*right = (right_sum[0] + right_sum[1]) + (right_sum[2] + right_sum[3]);
#else
	*left = resampler_dot(left_frames, weights, deltas, blend);
	*right = resampler_dot(right_frames, weights, deltas, blend);
#endif
}

#ifdef RESAMPLER_COEFFICIENT_CACHE
/* Same accumulation order as resampler_dot and resampler_dot2; only the
coefficient blending has moved to the bounded cache above. */
static float resampler_dot_prepared(const float *frames, const float *weights)
{
#if RESAMPLER_LANES == 4
	resampler_vector sum = { 0.0f, 0.0f, 0.0f, 0.0f };
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap += 4)
		sum += *(const resampler_unaligned_vector *)(frames + tap) * *(const resampler_vector *)(weights + tap);
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
#else
	float sum = 0.0f;
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap++)
		sum += frames[tap] * weights[tap];
	return sum;
#endif
}

static void resampler_dot2_prepared(const float *left_frames, const float *right_frames, const float *weights,
	float *left, float *right)
{
#if RESAMPLER_LANES == 4
	resampler_vector left_sum = { 0.0f, 0.0f, 0.0f, 0.0f }, right_sum = left_sum;
	int tap;

	for (tap = 0; tap < RESAMPLER_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap);

		left_sum += *(const resampler_unaligned_vector *)(left_frames + tap) * weight;
		right_sum += *(const resampler_unaligned_vector *)(right_frames + tap) * weight;
	}
	*left = (left_sum[0] + left_sum[1]) + (left_sum[2] + left_sum[3]);
	*right = (right_sum[0] + right_sum[1]) + (right_sum[2] + right_sum[3]);
#else
	*left = resampler_dot_prepared(left_frames, weights);
	*right = resampler_dot_prepared(right_frames, weights);
#endif
}
#endif

/* the voice's frame at its phase, its channels' middle, through the Xbox's
resampler (send_phases), for the reverb's send: as resampler_dot, over
SEND_TAPS */
static float send_frame(const struct sdl_stream *stream)
{
	double position = stream->phase * RESAMPLER_TABLE_STEPS;
	unsigned long row = (unsigned long)position;
	float blend = (float)(position - (double)row);
	unsigned long first = (stream->center + 1 - SEND_ZERO_CROSSINGS) % RESAMPLER_HISTORY;
	const float *left = stream->history[0] + first, *right = stream->history[1] + first;
	const float *weights = send_phases[row], *deltas = send_deltas[row];
	int tap;
#if RESAMPLER_LANES == 4
	resampler_vector sum = { 0.0f, 0.0f, 0.0f, 0.0f }, blends = { blend, blend, blend, blend };

	for (tap = 0; tap < SEND_TAPS; tap += 4)
	{
		resampler_vector weight = *(const resampler_vector *)(weights + tap) +
			*(const resampler_vector *)(deltas + tap) * blends;
		resampler_vector frames = *(const resampler_unaligned_vector *)(left + tap);

		if (stream->channels == 2)
			frames = (frames + *(const resampler_unaligned_vector *)(right + tap)) * 0.5f;
		sum += frames * weight;
	}
	return (sum[0] + sum[1]) + (sum[2] + sum[3]);
#else
	float sum = 0.0f;

	for (tap = 0; tap < SEND_TAPS; tap++)
	{
		float frame = stream->channels == 2 ? 0.5f * (left[tap] + right[tap]) : left[tap];

		sum += frame * (weights[tap] + deltas[tap] * blend);
	}
	return sum;
#endif
}

/* a voice starting (over): silence before its first frame, which the output
starts at */
static void resampler_reset(struct sdl_stream *stream)
{
	memset(stream->history, 0, sizeof(stream->history));
	stream->history_count = RESAMPLER_ZERO_CROSSINGS * RESAMPLER_MAXIMUM_STRETCH;
	stream->center = stream->history_count;
	stream->phase = 0.0;
	stream->silence = 0;
}

/* how much the low pass is narrowed for a voice taking frames step at a time:
for one faster than the output rate, to keep what it takes above the output's
band out (up to RESAMPLER_MAXIMUM_STRETCH times) */
static float resampler_scale(double step)
{
	return step > 1.0 ? (float)(1.0 / (step < RESAMPLER_MAXIMUM_STRETCH ? step : RESAMPLER_MAXIMUM_STRETCH)) : 1.0f;
}

static float packet_sample(const struct voice_packet *packet, unsigned long frame, unsigned long channel,
	unsigned long channels)
{
	return packet->samples[frame * channels + channel] * (1.0f / 32768.0f);
}

/* The voice's next frame, finishing packets it passes. A queued packet still
being decoded is different from the end of the stream: it must not be padded
with silence in the resampler's history. */
static BOOL take_frame(struct sdl_stream *stream, float *frame, BOOL *waiting)
{
	*waiting = FALSE;
	for (;;)
	{
		struct voice_packet *packet = NULL;
		unsigned long position;

		for (position = 0; position < stream->packet_count; position++)
		{
			struct voice_packet *candidate = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

			if (!candidate->finished)
			{
				packet = candidate;
				break;
			}
		}
		if (!packet)
			return FALSE;
		if (!packet->ready)
		{
			*waiting = TRUE;
			return FALSE;
		}
		if (stream->cursor < packet->frames)
		{
			frame[0] = packet_sample(packet, stream->cursor, 0, stream->channels);
			frame[1] = packet_sample(packet, stream->cursor, stream->channels - 1, stream->channels);
			stream->cursor++;
			return TRUE;
		}
		stream->cursor = 0;
		packet->finished = TRUE;
	}
}

/* whether the voice has a frame to take; the packets with none left are
marked finished on the way, as take_frame marks them */
static BOOL voice_has_frames(struct sdl_stream *stream)
{
	unsigned long position;

	for (position = 0; position < stream->packet_count; position++)
	{
		struct voice_packet *packet = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

		if (packet->finished)
			continue;
		if (!packet->ready)
			return FALSE;
		if (stream->cursor < packet->frames)
			return TRUE;
		stream->cursor = 0;
		packet->finished = TRUE;
	}
	return FALSE;
}

/* ---------- mixing */

/* mixes one voice into output (frames of output_channels floats), and into
the reverb's send (frames of mono) */
static void mix_voice(struct sdl_stream *stream, float *output, float *send, unsigned long frames)
{
	double step, target_step, ramp_step;
	float target_left, target_right, target_room, left, right, room, ramp_left, ramp_right, ramp_room, scale;
	float target_direct_lowpass, target_room_lowpass, direct_lowpass, room_lowpass;
	float ramp_direct_lowpass, ramp_room_lowpass, share;
	float direct_state[2], room_state;
	long width;
	unsigned long frame, ramp_frames;
	BOOL waiting = FALSE;
#ifdef HALO_WIIU
	/* port: only the qualified audio worker uses the Wii U's paired dots;
	   the filter, phase and C fallback retain their original arithmetic. */
	const BOOL paired_dots = wiiu_resampler_ps_ready();
#endif
#ifdef RESAMPLER_COEFFICIENT_CACHE
	BOOL prepared_coefficients;
#endif
	/* (a stereo voice panned towards its sound: voice_gains) */
	BOOL positioned = stream->channels == 2 && stream->stereo_positioned;
	/* (through the HRTF: whether it may take the voice, and whether its
	filters run in this mix; how much of the voice they play, at the start,
	the end and a frame's step; its parameters, and the direct path the
	HRTF takes, hrtf_process) */
	BOOL hrtf_on, hrtf_running;
	float hrtf_mix, hrtf_mix_end, ramp_hrtf_mix, hrtf_mix_start;
	struct hrtf_parameters target_hrtf;
	float hrtf_direct[MIX_CHUNK_FRAMES];
	/* on 5.1, the speakers' gains (voice_surround_gains) */
	BOOL surround = output_channels == 6, surround_heard = FALSE;
	float target_surround[2][MAXIMUM_OUTPUT_CHANNELS], surround_gains[2][MAXIMUM_OUTPUT_CHANNELS];
	float ramp_surround[2][MAXIMUM_OUTPUT_CHANNELS], pair[2];
	unsigned long speaker;

	if (stream->paused || stream->flushing || !stream->packet_count || !stream->sample_rate)
		return;
	/* (at most MIX_CHUNK_FRAMES at a time: the HRTF's direct path) */
	if (frames > MIX_CHUNK_FRAMES)
	{
		mix_voice(stream, output, send, MIX_CHUNK_FRAMES);
		mix_voice(stream, output + MIX_CHUNK_FRAMES * output_channels, send + MIX_CHUNK_FRAMES,
			frames - MIX_CHUNK_FRAMES);
		return;
	}
	/* a voice whose next packet is not decoded yet waits for it (DSOUND_DECODE_THREAD) */
	{
		unsigned long position;

		for (position = 0; position < stream->packet_count; position++)
		{
			const struct voice_packet *packet = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

			if (packet->finished)
				continue;
			if (!packet->ready)
			{
#ifdef DSOUND_DECODE_THREAD
				__atomic_add_fetch(&decoder_voices_waited, 1, __ATOMIC_RELAXED);
#endif
				return;
			}
			break;
		}
	}
	target_step = (double)(stream->frequency ? stream->frequency : stream->sample_rate) / OUTPUT_RATE *
		doppler_heard(stream);
	if (target_step * OUTPUT_RATE > VOICE_FREQUENCY_MAXIMUM)
		target_step = VOICE_FREQUENCY_MAXIMUM / OUTPUT_RATE;
	/* (not "<": a step that is not a number is held too, or the loops below that count it would not end) */
	if (!(target_step * OUTPUT_RATE >= VOICE_FREQUENCY_MINIMUM))
		target_step = VOICE_FREQUENCY_MINIMUM / OUTPUT_RATE;
	/* (the step ramps from the last mix's to it, as the gains do) */
	step = stream->gains_valid ? stream->current_step : target_step;
	scale = resampler_scale(step > target_step ? step : target_step);
#ifdef RESAMPLER_COEFFICIENT_CACHE
	prepared_coefficients = resampler_cache_frequency(stream->frequency ? stream->frequency : stream->sample_rate);
#endif
	width = (long)ceilf(RESAMPLER_ZERO_CROSSINGS / scale);
	/* a voice that ran dry and stopped (below) starts over once it has
	frames again: stream_process starts over only a stream with no packets,
	and the next can come before the finished ones are completed */
	if (stream->silence > (unsigned long)(2 * width))
	{
		if (!voice_has_frames(stream))
			return;
		resampler_reset(stream);
		stream->gains_valid = FALSE;
		step = target_step;
		scale = resampler_scale(step);
		width = (long)ceilf(RESAMPLER_ZERO_CROSSINGS / scale);
	}
	voice_gains(stream, &target_left, &target_right, pair, &target_room, &target_direct_lowpass,
		&target_room_lowpass, &hrtf_on, &target_hrtf);
	/* how much of the voice the HRTF plays: towards all of it while it is
	chosen (hrtf_enabled), none otherwise, a frame's share of the crossfade
	each frame (hrtf_mix_step), however many frames the mixes have; a voice
	starting is at once where it is going */
	hrtf_mix = hrtf_on ? stream->hrtf_mix : 0.0f;
	hrtf_mix_end = hrtf_on && hrtf_enabled ? 1.0f : 0.0f;
	if (!stream->gains_valid)
		hrtf_mix = hrtf_mix_end;
	ramp_hrtf_mix = hrtf_mix_end > hrtf_mix ? 1.0f / HRTF_FADE_FRAMES :
		hrtf_mix_end < hrtf_mix ? -1.0f / HRTF_FADE_FRAMES : 0.0f;
	hrtf_running = hrtf_on && (hrtf_mix > 0.0f || hrtf_mix_end > 0.0f);
	/* (its filters start from silence, as the crossfade starts from none
	of them) */
	if (hrtf_running && (!stream->gains_valid || !stream->hrtf_on))
	{
		stream->hrtf_current = target_hrtf;
		memset(stream->hrtf_frames, 0, sizeof(stream->hrtf_frames));
		memset(stream->hrtf_bands, 0, sizeof(stream->hrtf_bands));
	}
	stream->hrtf_on = hrtf_running;
	if (surround)
		voice_surround_gains(stream, target_left, target_right, pair, target_surround);
	/* a voice starting is at once at its gains, step and way through the
	HRTF; a change to any of them, from the last mix's, ramps from where
	they are to the new ones over PARAMETER_RAMP_FRAMES, then holds them: a
	ramp the mix's frames end goes on in the next mix, and one changed
	before its end ramps on from where it was, so that a change takes the
	same frames however many the mixes have */
	if (!stream->gains_valid)
	{
		if (surround)
			memcpy(stream->current_surround, target_surround, sizeof(target_surround));
		stream->current_left = target_left;
		stream->current_right = target_right;
		stream->current_room = target_room;
		stream->current_step = target_step;
		stream->current_direct_lowpass = target_direct_lowpass;
		stream->current_room_lowpass = target_room_lowpass;
		stream->direct_lowpass[0] = stream->direct_lowpass[1] = 0.0f;
		stream->room_lowpass = 0.0f;
		stream->ramp_frames = 0;
		stream->gains_valid = TRUE;
	}
	else if (target_left != stream->target_left || target_right != stream->target_right ||
		target_room != stream->target_room || target_step != stream->target_step ||
		target_direct_lowpass != stream->target_direct_lowpass || target_room_lowpass != stream->target_room_lowpass ||
		(surround && memcmp(target_surround, stream->target_surround, sizeof(target_surround))) ||
		(hrtf_running && memcmp(&target_hrtf, &stream->hrtf_target, sizeof(target_hrtf))))
	{
		stream->ramp_frames = PARAMETER_RAMP_FRAMES;
	}
	stream->target_left = target_left;
	stream->target_right = target_right;
	stream->target_room = target_room;
	stream->target_step = target_step;
	stream->target_direct_lowpass = target_direct_lowpass;
	stream->target_room_lowpass = target_room_lowpass;
	if (surround)
		memcpy(stream->target_surround, target_surround, sizeof(target_surround));
	if (hrtf_running)
		stream->hrtf_target = target_hrtf;
	ramp_frames = stream->ramp_frames;
	share = ramp_frames ? 1.0f / (float)ramp_frames : 0.0f;
	step = stream->current_step;
	ramp_step = ramp_frames ? (target_step - step) / (double)ramp_frames : 0.0;
	left = stream->current_left;
	right = stream->current_right;
	room = stream->current_room;
	direct_lowpass = stream->current_direct_lowpass;
	room_lowpass = stream->current_room_lowpass;
	/* the low passes' states, here while the mix writes the output (which
	the compiler cannot tell from them) */
	direct_state[0] = stream->direct_lowpass[0];
	direct_state[1] = stream->direct_lowpass[1];
	room_state = stream->room_lowpass;
	ramp_left = (target_left - left) * share;
	ramp_right = (target_right - right) * share;
	ramp_room = (target_room - room) * share;
	ramp_direct_lowpass = (target_direct_lowpass - direct_lowpass) * share;
	ramp_room_lowpass = (target_room_lowpass - room_lowpass) * share;
	hrtf_mix_start = hrtf_mix;
	if (surround)
	{
		for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		{
			surround_gains[0][speaker] = stream->current_surround[0][speaker];
			surround_gains[1][speaker] = stream->current_surround[1][speaker];
			ramp_surround[0][speaker] = (target_surround[0][speaker] - surround_gains[0][speaker]) * share;
			ramp_surround[1][speaker] = (target_surround[1][speaker] - surround_gains[1][speaker]) * share;
			/* (a 2D voice may be heard from the center, the backs or the
			LFE alone, its left and right gains 0) */
			if (surround_gains[0][speaker] || surround_gains[1][speaker] || target_surround[0][speaker] ||
				target_surround[1][speaker])
			{
				surround_heard = TRUE;
			}
		}
	}

	for (frame = 0; frame < frames; frame++)
	{
		float sample_left = 0.0f, sample_right = 0.0f;
		long tap;

		/* the frames ahead the low pass reaches; after the last packet,
		silence, until what was taken has played out */
		while ((long)(stream->history_count - stream->center) <= width)
		{
			unsigned long slot = stream->history_count % RESAMPLER_HISTORY;
			float taken[2];

			if (take_frame(stream, taken, &waiting))
			{
				stream->silence = 0;
			}
			else if (waiting)
			{
#ifdef DSOUND_DECODE_THREAD
				__atomic_add_fetch(&decoder_voices_waited, 1, __ATOMIC_RELAXED);
#endif
				break;
			}
			else
			{
				taken[0] = taken[1] = 0.0f;
				stream->silence++;
			}
			stream->history[0][slot] = stream->history[0][slot + RESAMPLER_HISTORY] = taken[0];
			stream->history[1][slot] = stream->history[1][slot + RESAMPLER_HISTORY] = taken[1];
			stream->history_count++;
		}
		if (waiting || stream->silence > (unsigned long)(2 * width))
			break;

		if (!left && !right && !room && !ramp_left && !ramp_right && !ramp_room && !surround_heard)
		{
			/* a voice turned all the way down (out of earshot), and not
			sending to the reverb, only moves on */
		}
		else if (resampler_mode == RESAMPLER_LINEAR)
		{
			/* (the frame the output is at and the next, side by side
			however the ring wraps) */
			const float *left_frames = stream->history[0] + stream->center % RESAMPLER_HISTORY;
			const float *right_frames = stream->history[1] + stream->center % RESAMPLER_HISTORY;
			float fraction = (float)stream->phase;

			sample_left = left_frames[0] + (left_frames[1] - left_frames[0]) * fraction;
			sample_right = right_frames[0] + (right_frames[1] - right_frames[0]) * fraction;
		}
		else if (resampler_mode == RESAMPLER_XBOX)
		{
			/* the Xbox's low pass (direct_phases), never narrowed */
			double position = stream->phase * RESAMPLER_TABLE_STEPS;
			unsigned long row = (unsigned long)position;
			float blend = (float)(position - (double)row);
			unsigned long first = (stream->center + 1 - DIRECT_ZERO_CROSSINGS) % RESAMPLER_HISTORY;

			sample_left = direct_dot(stream->history[0] + first, direct_phases[row], direct_deltas[row], blend);
			sample_right = stream->channels == 1 ? sample_left :
				direct_dot(stream->history[1] + first, direct_phases[row], direct_deltas[row], blend);
		}
		else if (scale == 1.0f)
		{
			double position = stream->phase * RESAMPLER_TABLE_STEPS;
			unsigned long row = (unsigned long)position;
			float blend = (float)(position - (double)row);
			unsigned long first = (stream->center + 1 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY;
#ifdef RESAMPLER_COEFFICIENT_CACHE
			if (prepared_coefficients)
			{
				const float *prepared = resampler_coefficients(row, blend);

#ifdef HALO_WIIU
				if (paired_dots)
				{
					if (stream->channels == 1)
					{
						wiiu_resampler_ps_dot48(stream->history[0] + first, prepared, &sample_left);
						sample_right = sample_left;
					}
					else
					{
						float paired[2];

						wiiu_resampler_ps_dot48_stereo(stream->history[0] + first, stream->history[1] + first, prepared, paired);
						sample_left = paired[0];
						sample_right = paired[1];
					}
				}
				else
#endif
				if (stream->channels == 1)
					sample_left = sample_right = resampler_dot_prepared(stream->history[0] + first, prepared);
				else
					resampler_dot2_prepared(stream->history[0] + first, stream->history[1] + first, prepared, &sample_left, &sample_right);
			}
			else
#endif
			{
				/* a mono voice's two channels are the same: take_frame */
#ifdef HALO_WIIU
				if (paired_dots)
				{
					if (stream->channels == 1)
					{
						wiiu_resampler_ps_blended48(stream->history[0] + first, resampler_phases[row], resampler_deltas[row], &blend, &sample_left);
						sample_right = sample_left;
					}
					else
					{
						float paired[2];

						wiiu_resampler_ps_blended48_stereo(stream->history[0] + first, stream->history[1] + first,
							resampler_phases[row], resampler_deltas[row], &blend, paired);
						sample_left = paired[0];
						sample_right = paired[1];
					}
				}
				else
#endif
				if (stream->channels == 1)
					sample_left = sample_right = resampler_dot(stream->history[0] + first, resampler_phases[row],
						resampler_deltas[row], blend);
				else
					resampler_dot2(stream->history[0] + first, stream->history[1] + first, resampler_phases[row],
						resampler_deltas[row], blend, &sample_left, &sample_right);
			}
		}
		else
		{
			unsigned long first = (stream->center + 1 - (unsigned long)width) % RESAMPLER_HISTORY;
			const float *left_frames = stream->history[0] + first, *right_frames = stream->history[1] + first;

			for (tap = 1 - width; tap <= width; tap++)
			{
				float weight = scale * resampler_weight(fabsf((float)tap - (float)stream->phase) * scale * RESAMPLER_TABLE_STEPS);

				sample_left += left_frames[tap + width - 1] * weight;
				if (stream->channels == 2)
					sample_right += right_frames[tap + width - 1] * weight;
			}
			if (stream->channels == 1)
				sample_right = sample_left;
		}
		/* the room send, with its own low pass, through the Xbox's resampler
		(send_phases), not the mixer's: the reverb folds the send's top
		octave onto its bottom one, and the Xbox's let through images and
		aliases there that the mixer's takes out (recorded, the cave's
		reverberation of a click was 4 dB louder below 500 Hz at 48 kHz, 3.6
		dB at 44.1 kHz) */
		if (room || ramp_room)
		{
			float mono = send_frame(stream);

			room_state = flush_denormal(mono + room_lowpass * (room_state - mono));
			send[frame] += room_state * room;
		}
		/* the direct path, muffled, or as it is; a mono voice's mix bins or pan
		split it across the speakers */
		if (direct_lowpass || ramp_direct_lowpass)
		{
			sample_left = flush_denormal(sample_left + direct_lowpass * (direct_state[0] - sample_left));
			sample_right = flush_denormal(sample_right + direct_lowpass * (direct_state[1] - sample_right));
		}
		direct_state[0] = sample_left;
		direct_state[1] = sample_right;
		if (surround)
		{
			/* the first row's gains take the left (a mono voice's only)
			channel, or a positioned voice's middle; the second the right, or
			its difference; through the HRTF (hrtf_process, after), the pan's
			share while the choice changes */
			float *speakers = output + frame * MAXIMUM_OUTPUT_CHANNELS;
			float panned = hrtf_running ? 1.0f - hrtf_mix : 1.0f;
			float first = (positioned ? 0.5f * (sample_left + sample_right) : sample_left) * panned;
			float second = (positioned ? 0.5f * (sample_left - sample_right) :
				stream->channels == 2 ? sample_right : 0.0f) * panned;

			if (hrtf_running)
			{
				hrtf_direct[frame] = sample_left;
				hrtf_mix = hrtf_mix_step(hrtf_mix, ramp_hrtf_mix, hrtf_mix_end);
			}
			for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
			{
				speakers[speaker] += first * surround_gains[0][speaker] + second * surround_gains[1][speaker];
				surround_gains[0][speaker] += ramp_surround[0][speaker];
				surround_gains[1][speaker] += ramp_surround[1][speaker];
			}
		}
		else if (hrtf_running)
		{
			/* through the HRTF (hrtf_process, after), crossfaded with the
			pan while the choice changes */
			float panned = 1.0f - hrtf_mix;

			hrtf_direct[frame] = sample_left;
			if (panned > 0.0f)
			{
				output[frame * 2] += sample_left * left * panned;
				output[frame * 2 + 1] += sample_left * right * panned;
			}
			hrtf_mix = hrtf_mix_step(hrtf_mix, ramp_hrtf_mix, hrtf_mix_end);
		}
		else if (stream->channels == 1)
		{
			output[frame * 2] += sample_left * left;
			output[frame * 2 + 1] += sample_left * right;
		}
		else if (positioned)
		{
			/* the channels' middle panned, and their difference kept as
			wide as the far ear's gain: the sound comes from where it is,
			still stereo */
			float middle = 0.5f * (sample_left + sample_right);
			float side = 0.5f * (sample_left - sample_right);
			float far_gain = left < right ? left : right;

			output[frame * 2] += middle * left + side * far_gain;
			output[frame * 2 + 1] += middle * right - side * far_gain;
		}
		else
		{
			output[frame * 2] += sample_left * left;
			output[frame * 2 + 1] += sample_right * right;
		}
		left += ramp_left;
		right += ramp_right;
		room += ramp_room;
		direct_lowpass += ramp_direct_lowpass;
		room_lowpass += ramp_room_lowpass;
		stream->phase += step;
		step += ramp_step;
		/* (a phase that is not a number or is absurd would count for ever) */
		if (!(stream->phase < 65536.0))
			stream->phase = 0.0;
		while (stream->phase >= 1.0)
		{
			stream->phase -= 1.0;
			stream->center++;
		}
		/* (at the ramp's end, exactly the new ones, held) */
		if (frame + 1 == ramp_frames)
		{
			left = target_left;
			right = target_right;
			room = target_room;
			direct_lowpass = target_direct_lowpass;
			room_lowpass = target_room_lowpass;
			step = target_step;
			ramp_left = ramp_right = ramp_room = ramp_direct_lowpass = ramp_room_lowpass = 0.0f;
			ramp_step = 0.0;
			if (surround)
			{
				memcpy(surround_gains, target_surround, sizeof(surround_gains));
				memset(ramp_surround, 0, sizeof(ramp_surround));
			}
		}
	}
	stream->direct_lowpass[0] = direct_state[0];
	stream->direct_lowpass[1] = direct_state[1];
	stream->room_lowpass = room_state;
	/* (the frames mixed: all of them, or those before the voice ran dry) */
	if (hrtf_running)
	{
		hrtf_process(stream, hrtf_direct, output, frame, ramp_frames, &target_hrtf, hrtf_mix_start, ramp_hrtf_mix,
			hrtf_mix_end);
	}
	stream->hrtf_mix = hrtf_running ? hrtf_mix : hrtf_mix_end;
	/* where the ramp got to, and the frames of it left */
	stream->current_left = left;
	stream->current_right = right;
	stream->current_room = room;
	stream->current_step = step;
	stream->current_direct_lowpass = direct_lowpass;
	stream->current_room_lowpass = room_lowpass;
	if (surround)
		memcpy(stream->current_surround, surround_gains, sizeof(surround_gains));
	stream->ramp_frames = ramp_frames > frame ? ramp_frames - frame : 0;
}

/* The game cuts a voice off (Flush) to play the next sound on its channel:
each shot of a weapon fired faster than its sound lasts takes the last's
channel (sound classes allow one weapon_fire sound an object). Stopped dead,
a waveform drops from wherever it was to nothing, a click, and a rifle's
shots crackled. The packets must go back to the game at once (it refills the
channel, and may reuse their memory), so what the voice would play next is
mixed now, CUT_FADE_FRAMES of it at its gains, faded out along half a cosine,
and played over the next mixes beside the new sound (mix_cut). What is left
of a cut still playing is kept under it. The fade is the voice's own output
only while that is its frames at its gains through its low passes (whose
state mix_voice carries on into it): processing of a voice that holds what
it played (an HRTF's filters, a delay) must take the fade through itself
too, or the cut jumps from the one to the other and clicks again. */
static void voice_cut(struct sdl_stream *stream)
{
	static float output[CUT_FADE_FRAMES * MAXIMUM_OUTPUT_CHANNELS], send[CUT_FADE_FRAMES];
	unsigned long frame, channel, left = CUT_FADE_FRAMES - stream->cut_played;

	if (stream->paused || !stream->packet_count || !stream->gains_valid)
		return;
	memset(output, 0, sizeof(output));
	memset(send, 0, sizeof(send));
	mix_voice(stream, output, send, CUT_FADE_FRAMES);
	for (frame = 0; frame < CUT_FADE_FRAMES; frame++)
	{
		float fade = 0.5f + 0.5f * (float)cos(3.14159265358979 * (double)(frame + 1) / CUT_FADE_FRAMES);

		/* (what is left of the earlier cut is read only where it is: past
		it is whatever it was, a NaN perhaps, which no factor of 0 hides) */
		for (channel = 0; channel < output_channels; channel++)
		{
			stream->cut[frame * output_channels + channel] = output[frame * output_channels + channel] * fade +
				(frame < left ? stream->cut[(stream->cut_played + frame) * output_channels + channel] : 0.0f);
		}
		stream->cut_send[frame] = send[frame] * fade +
			(frame < left ? stream->cut_send[stream->cut_played + frame] : 0.0f);
	}
	stream->cut_played = 0;
}

/* a cut voice's fade out (voice_cut), into the mix */
static void mix_cut(struct sdl_stream *stream, float *output, float *send, unsigned long frames)
{
	unsigned long frame, channel;

	for (frame = 0; frame < frames && stream->cut_played < CUT_FADE_FRAMES; frame++, stream->cut_played++)
	{
		for (channel = 0; channel < output_channels; channel++)
			output[frame * output_channels + channel] += stream->cut[stream->cut_played * output_channels + channel];
		send[frame] += stream->cut_send[stream->cut_played];
	}
}

#include "dsound_xbox_reverb.inl"

#include "dsound_xbox_lfe.inl"

/* ---------- limiter

The game sets its mix bins' headroom to 0 (sound_dsound_xbox.c), so the voices
sum at their full level, as on the Xbox, and a pile of loud ones goes over
full scale. Clipping each sample, or bending it near full scale, distorts the
sound: dialogue over gunfire crackled. Instead the whole mix is turned down
for as long as it would go over, both channels alike. The output is delayed
LIMITER_LOOKAHEAD - 1 frames (1.3 ms), so that the gain comes down smoothly
before each peak: the smallest gain the frames ahead need, averaged over the
last LIMITER_LOOKAHEAD frames, is never more than a peak needs when it plays.
The gain comes back up over LIMITER_RELEASE_SECONDS. */

#define LIMITER_CEILING 1.0f /* the measured Xbox output reaches full scale */
#define LIMITER_LOOKAHEAD 64
#define LIMITER_RELEASE_SECONDS 0.1f

static struct
{
	/* the frames the output is delayed by, and the gain each needs */
	float delay[LIMITER_LOOKAHEAD][MAXIMUM_OUTPUT_CHANNELS];
	float needed[LIMITER_LOOKAHEAD];
	/* how many of them are under 1: none while nothing is too loud, when the
	smallest is 1 without looking */
	unsigned long limiting;
	/* the gain held down to what the frames ahead need, coming back up */
	float held;
	/* its last LIMITER_LOOKAHEAD values, and their sum */
	float history[LIMITER_LOOKAHEAD];
	double history_sum;
	unsigned long position;
	BOOL initialized;
} limiter;

static void limit(float *output, unsigned long frames)
{
	float release = 1.0f - expf(-1.0f / (LIMITER_RELEASE_SECONDS * OUTPUT_RATE));
	unsigned long frame, index, channel;

	if (!limiter.initialized)
	{
		for (index = 0; index < LIMITER_LOOKAHEAD; index++)
		{
			limiter.needed[index] = 1.0f;
			limiter.history[index] = 1.0f;
		}
		limiter.held = 1.0f;
		limiter.history_sum = LIMITER_LOOKAHEAD;
		limiter.initialized = TRUE;
	}
	for (frame = 0; frame < frames; frame++)
	{
		float *sample = output + frame * output_channels;
		unsigned long position = limiter.position;
		unsigned long oldest = (position + 1) % LIMITER_LOOKAHEAD;
		float peak = 0.0f, lowest, gain;

		/* the new frame takes the slot of the oldest, which has played */
		for (channel = 0; channel < output_channels; channel++)
		{
			if (fabsf(sample[channel]) > peak)
				peak = fabsf(sample[channel]);
			limiter.delay[position][channel] = sample[channel];
		}
		limiter.limiting -= limiter.needed[position] < 1.0f;
		limiter.needed[position] = peak > LIMITER_CEILING ? LIMITER_CEILING / peak : 1.0f;
		limiter.limiting += limiter.needed[position] < 1.0f;
		lowest = 1.0f;
		if (limiter.limiting)
		{
			lowest = limiter.needed[0];
			for (index = 1; index < LIMITER_LOOKAHEAD; index++)
			{
				if (limiter.needed[index] < lowest)
					lowest = limiter.needed[index];
			}
		}
		if (lowest < limiter.held)
			limiter.held = lowest;
		else
			limiter.held += (lowest - limiter.held) * release;
		limiter.history_sum += limiter.held - limiter.history[position];
		limiter.history[position] = limiter.held;
		gain = (float)(limiter.history_sum / LIMITER_LOOKAHEAD);
		/* the frame LIMITER_LOOKAHEAD - 1 frames old plays */
		for (channel = 0; channel < output_channels; channel++)
			sample[channel] = limiter.delay[oldest][channel] * gain;
		limiter.position = oldest;
	}
}

/* The buffers' front pair, under the output limiter. Interpolate the two
samples around the cursor, wrapping at the loop boundary. Unlike stream
packets these samples stay alive until SetBufferData or Release. */
static void mix_buffers(float *output, unsigned long frames)
{
	struct sdl_buffer *buffer;

	for (buffer = buffers; buffer; buffer = buffer->next)
	{
		unsigned long frame, end;
		double step = buffer->rate / OUTPUT_RATE;
		float target = buffer->volume * master_volume;

		if (!buffer->playing || !buffer->samples || !buffer->frames)
			continue;
		end = buffer->looping ? buffer->loop_end : buffer->frames;
		for (frame = 0; frame < frames; frame++)
		{
			unsigned long index = (unsigned long)buffer->position, next = index + 1, channel;
			float fraction = (float)(buffer->position - index);

			if (next >= end)
				next = buffer->looping ? buffer->loop_start : index;
			/* About 10 ms to settle after starting or changing volume. */
			buffer->current_volume = flush_denormal(buffer->current_volume +
				(target - buffer->current_volume) * (1.0f / 120.0f));
			for (channel = 0; channel < 2; channel++)
			{
				unsigned long source_channel = buffer->channels == 1 ? 0 : channel;
				float a = buffer->samples[index * buffer->channels + source_channel];
				float b = buffer->samples[next * buffer->channels + source_channel];

				output[frame * output_channels + channel] +=
					(a + (b - a) * fraction) * buffer->current_volume * (1.0f / 32768.0f);
			}
			buffer->position += step;
			if (buffer->position >= end)
			{
				if (!buffer->looping)
				{
					buffer->playing = FALSE;
					buffer->position = 0;
					buffer->current_volume = 0;
					break;
				}
				buffer->position = buffer->loop_start +
					fmod(buffer->position - buffer->loop_start, end - buffer->loop_start);
			}
		}
	}
}

static void mix(float *output, unsigned long frames)
{
	struct sdl_stream *stream;
	DSI3DL2LISTENER properties;
	BOOL enabled, changed = FALSE;
	unsigned long serial = 0;
#ifdef HALO_WIIU
	const BOOL profile_mix = wiiu_audio_profile_is_enabled();
	struct wiiu_audio_mix_pressure pressure;
	uint64_t profile_lock_started = 0, profile_voice_started = 0, profile_stage = 0;
#endif

	memset(output, 0, frames * output_channels * sizeof(float));
	memset(reverb.send, 0, frames * sizeof(float));
#ifdef HALO_WIIU
	if (profile_mix)
	{
		memset(&pressure, 0, sizeof(pressure));
		profile_voice_started = profile_lock_started = OSGetSystemTime();
	}
#endif
	pthread_mutex_lock(&mixer_lock);
	enabled = reverb_enabled;
#ifdef HALO_WIIU
	if (profile_mix)
		pressure.lock_wait += OSGetSystemTime() - profile_lock_started;
#endif
#ifdef DSOUND_MIXER_YIELDS
	{
		struct sdl_stream *next;

		for (stream = streams; stream; stream = next)
		{
#ifdef HALO_WIIU
			if (profile_mix)
			{
				pressure.streams++;
				if (!stream->paused && stream->packet_count)
					pressure.queued++;
			}
#endif
			mix_voice(stream, output, reverb.send, frames);
			mix_cut(stream, output, reverb.send, frames);
			next = stream->next;
			/* (the game thread waits: the lock is let go until it has had it) */
			if (next && __atomic_load_n(&mixer_lock_wanted, __ATOMIC_ACQUIRE))
			{
				mixer_walk_next = next;
#ifdef HALO_WIIU
				if (profile_mix)
					profile_lock_started = OSGetSystemTime();
#endif
				pthread_mutex_unlock(&mixer_lock);
				{
					/* (a few yields, then short sleeps: a yield lets only a thread of this one's priority or higher run, and the game thread
					that wants the lock is lower than the audio thread, so a thread on this core that does not get the core would be waited
					for for ever) */
					unsigned spins = 0;

					while (__atomic_load_n(&mixer_lock_wanted, __ATOMIC_ACQUIRE))
					{
#ifdef HALO_WIIU
						if (++spins > 16)
							OSSleepTicks(OSMicrosecondsToTicks(100));
						else
#endif
							sched_yield();
					}
				}
				pthread_mutex_lock(&mixer_lock);
#ifdef HALO_WIIU
				if (profile_mix)
					pressure.lock_wait += OSGetSystemTime() - profile_lock_started;
#endif
				next = mixer_walk_next;
			}
		}
	}
#else
	for (stream = streams; stream; stream = stream->next)
	{
		mix_voice(stream, output, reverb.send, frames);
		mix_cut(stream, output, reverb.send, frames);
	}
#endif
	mix_buffers(output, frames);
	/* a new environment, once the fade to the last is over */
	if (reverb.serial != environment_serial && !reverb.fade)
	{
		properties = environment;
		serial = environment_serial;
		changed = TRUE;
	}
	pthread_mutex_unlock(&mixer_lock);
#ifdef HALO_WIIU
	if (profile_mix)
	{
		profile_stage = OSGetSystemTime();
		pressure.stage[WIIU_MIXER_VOICES] = profile_stage - profile_voice_started;
	}
#endif
	if (changed)
		reverb_update(&properties, serial);
	/* the reverb runs while something reverberates, and stops once it is
	turned off and faded out, or quiet */
	if (!reverb.silent || (enabled && reverb_sent(reverb.send, frames)))
	{
		reverb_process(reverb.send, output, frames, enabled ? 1.0f : 0.0f);
		if ((!enabled && reverb.level <= 0.0f) || reverb.quiet > REVERB_QUIET_FRAMES)
			reverb_clear();
	}
	/* OpenH1's Xbox captures put the final game mix at half the voice sum. */
	{
		unsigned long sample;
		for (sample = 0; sample < frames * output_channels; sample++)
			output[sample] *= 0.5f;
	}
	lfe_lowpass(output, frames);
#ifdef HALO_WIIU
	if (profile_mix)
	{
		const uint64_t now = OSGetSystemTime();

		pressure.stage[WIIU_MIXER_REVERB] = now - profile_stage;
		profile_stage = now;
	}
#endif
	/* the players' voices (voice_audio.c), dry, under the limiter; on 5.1
	from the front pair */
	if (output_channels == 6)
	{
		float voices[MIX_CHUNK_FRAMES * OUTPUT_CHANNELS];
		unsigned long frame;

		memset(voices, 0, frames * OUTPUT_CHANNELS * sizeof(float));
		voice_audio_mix(voices, frames);
		for (frame = 0; frame < frames; frame++)
		{
			output[frame * 6] += voices[frame * 2];
			output[frame * 6 + 1] += voices[frame * 2 + 1];
		}
	}
	else
	{
		voice_audio_mix(output, frames);
	}
#ifdef HALO_WIIU
	if (profile_mix)
	{
		const uint64_t now = OSGetSystemTime();

		pressure.stage[WIIU_MIXER_CHAT] = now - profile_stage;
		profile_stage = now;
	}
#endif
	limit(output, frames);
#ifdef HALO_WIIU
	if (profile_mix)
	{
		pressure.stage[WIIU_MIXER_LIMITER] = OSGetSystemTime() - profile_stage;
		wiiu_audio_profile_mix_pressure(&pressure);
	}
#endif
}

/* ---------- output */

#ifndef HALO_WIIU
static SDL_AudioStream *audio_stream;
#endif
static BOOL audio_started = FALSE;
/* the device it plays on (audio.output_device), and when it was looked at */
static char audio_device_name[PLATFORM_AUDIO_DEVICE_NAME_SIZE];
static unsigned long audio_device_read_at = (unsigned long)-1;

/* audio.output_device ("default": the system's; none on Android) */
static const char *audio_device_setting(void)
{
	const char *name = config_string("audio.output_device");

	return name && name[0] ? name : "default";
}

#ifndef HALO_WIIU
static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	float buffer[MIX_CHUNK_FRAMES * MAXIMUM_OUTPUT_CHANNELS];

	(void)userdata;
	(void)total_amount;
	while (additional_amount > 0)
	{
		unsigned long frames = (unsigned long)additional_amount / (output_channels * sizeof(float));

		if (frames > MIX_CHUNK_FRAMES)
			frames = MIX_CHUNK_FRAMES;
		if (!frames)
			frames = 1;
		mix(buffer, frames);
		SDL_PutAudioStreamData(stream, buffer, (int)(frames * output_channels * sizeof(float)));
		additional_amount -= (int)(frames * output_channels * sizeof(float));
	}
}

#endif

/* without a device, drain voices in real time */
static void *silent_clock_thread(void *parameter)
{
	float buffer[480 * MAXIMUM_OUTPUT_CHANNELS];
	struct timespec next;

	(void)parameter;
	clock_gettime(CLOCK_MONOTONIC, &next);
	for (;;)
	{
		mix(buffer, 480);
#ifdef HALO_WIIU
		/* (newlib has no clock_nanosleep) */
		{
			struct timespec wait = { 0, 10000000L };

			nanosleep(&wait, NULL);
		}
#else
		next.tv_nsec += 10000000L;
		if (next.tv_nsec >= 1000000000L)
		{
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
#endif
	}
	return NULL;
}

#ifdef DSOUND_DECODE_THREAD
static void decoder_start(void);
#endif

#ifdef HALO_WIIU
/* wiiu_audio.c's (the console's ABI): two AX voices over a ring, filled by calling mix */
int wiiu_audio_open(void (*mixer)(float *output, unsigned long frames));

static void audio_start(void)
{
	if (audio_started)
		return;
	audio_started = TRUE;
	{
		float volume = (float)config_real("audio.volume");
		BOOL enabled = config_boolean("audio.reverb");

		mixer_settings_publish(volume, enabled, stereo_doppler_enabled, hrtf_enabled, resampler_mode);
	}
	resampler_mode = resampler_mode_read();
	resampler_initialize();
	resampler_phases_initialize();
	mixer_lock_take();
	reverb_initialize();
	pthread_mutex_unlock(&mixer_lock);
#ifdef DSOUND_DECODE_THREAD
	decoder_start();
#endif

	if (config_boolean("audio.enabled") && wiiu_audio_open(mix))
		return;
	{
		pthread_t thread;

		platform_log("audio: no output; sound is silent");
		pthread_create(&thread, NULL, silent_clock_thread, NULL);
		pthread_detach(thread);
	}
}
#else
/* audio.channels: "stereo", "5.1", or "auto" for 5.1 when the device has
at least six channels. Android and the browser play stereo */
static unsigned long choose_output_channels(SDL_AudioDeviceID device)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB) || defined(HALO_ARM64_GUEST)
	(void)device;
	return 2;
#else
	const char *setting = config_string("audio.channels");
	SDL_AudioSpec format;

	if (!strcmp(setting, "5.1"))
		return 6;
	if (strcmp(setting, "auto"))
	{
		if (strcmp(setting, "stereo"))
			platform_log("audio.channels \"%s\" is not \"auto\", \"stereo\" or \"5.1\"; using stereo", setting);
		return 2;
	}
	if ((SDL_GetAudioDeviceFormat(device, &format, NULL) ||
		SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, NULL)) && format.channels >= 6)
		return 6;
	return 2;
#endif
}

static void audio_start(void)
{
	SDL_AudioSpec spec;

	if (audio_started)
		return;
	audio_started = TRUE;
	master_volume = (float)config_real("audio.volume");
	reverb_enabled = config_boolean("audio.reverb");
	stereo_doppler_enabled = config_boolean("audio.stereo_doppler");
	hrtf_enabled = config_boolean("audio.hrtf");
	resampler_mode = resampler_mode_read();
	hrtf_initialize();
	resampler_initialize();
	resampler_phases_initialize();
	reverb_initialize();

	if (config_boolean("audio.enabled") && platform_sdl_initialize())
	{
		snprintf(audio_device_name, sizeof(audio_device_name), "%s", audio_device_setting());
		output_channels = choose_output_channels(platform_audio_device(FALSE, audio_device_name));
		platform_log("sound: %s", output_channels == 6 ? "5.1 surround" : "stereo");
		spec.format = SDL_AUDIO_F32;
		spec.channels = (int)output_channels;
		spec.freq = OUTPUT_RATE;
#ifdef HALO_ARM64_GUEST
		/* frames per callback: on Android each callback is handed to a thread
		that can run the guest (host_sdl.c): 512 left it too little time and
		the menus' music broke up, which 1024 does not (about 21 ms at 48 kHz,
		11 ms more than 512) */
		SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
#elif !defined(HALO_WEB)
		/* (a browser's, SDL's own: with room for the page's scheduling, which
		512 frames underran) */
		{
			/* audio.buffer_frames: the device's buffer (port_config.c) */
			long buffer_frames = config_integer("audio.buffer_frames");
			char frames[16];

			if (buffer_frames < 64)
				buffer_frames = 64;
			if (buffer_frames > 8192)
				buffer_frames = 8192;
			snprintf(frames, sizeof(frames), "%ld", buffer_frames);
			SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames);
		}
#endif
		audio_device_read_at = config_changes();
		audio_stream = SDL_OpenAudioDeviceStream(platform_audio_device(FALSE, audio_device_name), &spec,
			audio_callback, NULL);
		if (!audio_stream && strcmp(audio_device_name, "default"))
			audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
		if (audio_stream)
		{
			SDL_ResumeAudioStreamDevice(audio_stream);
			return;
		}
		platform_log("cannot open an audio device (%s); sound is silent", SDL_GetError());
		output_channels = 2;
	}
	{
		pthread_t thread;

		pthread_create(&thread, NULL, silent_clock_thread, NULL);
		pthread_detach(thread);
	}
}

#endif

/* (the event thread, each frame: sdl_platform.c) audio.output_device
changed (Settings > Audio): the sound goes on on the new device, else the
system's default */
#ifdef HALO_WIIU
void dsound_sdl_output_device_check(void)
{
}
#else
void dsound_sdl_output_device_check(void)
{
	SDL_AudioSpec spec;
	SDL_AudioStream *stream;

	if (!audio_stream || audio_device_read_at == config_changes())
		return;
	audio_device_read_at = config_changes();
	if (!strcmp(audio_device_name, audio_device_setting()))
		return;
	snprintf(audio_device_name, sizeof(audio_device_name), "%s", audio_device_setting());
	spec.format = SDL_AUDIO_F32;
	/* (the game's mix bins are set for the channels the sound started with;
	SDL turns them into the new device's) */
	spec.channels = (int)output_channels;
	spec.freq = OUTPUT_RATE;
	SDL_DestroyAudioStream(audio_stream);
	stream = SDL_OpenAudioDeviceStream(platform_audio_device(FALSE, audio_device_name), &spec, audio_callback, NULL);
	if (!stream)
		stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
	audio_stream = stream;
	if (audio_stream)
	{
		SDL_ResumeAudioStreamDevice(audio_stream);
		platform_log("audio: playing on %s", audio_device_name);
	}
	else
	{
		pthread_t thread;

		/* (none at all: the voices drained in real time, as at the start) */
		platform_log("audio: cannot open an audio device (%s); sound is silent", SDL_GetError());
		pthread_create(&thread, NULL, silent_clock_thread, NULL);
		pthread_detach(thread);
	}
}

#endif

/* ---------- completion */

static void packet_release(struct voice_packet *entry)
{
	packet_deallocate(entry->samples);
	entry->samples = NULL;
}

/* completes the head packet; called with the lock held, which the game's
callback runs without */
static void stream_complete_head(struct sdl_stream *stream, DWORD status, DWORD completed_size)
{
	struct voice_packet *entry = &stream->packets[stream->packet_head];
	XMEDIAPACKET packet = entry->packet;

	/* (one not played to its end, flushed: the cursor was its place, and the
	mixer may take the next while the lock is let go below) */
	if (!entry->finished)
		stream->cursor = 0;
	packet_release(entry);
	entry->finished = FALSE;
	stream->packet_head = (stream->packet_head + 1) % MAXIMUM_STREAM_PACKETS;
	stream->packet_count--;
	if (packet.pdwCompletedSize)
		*packet.pdwCompletedSize = completed_size;
	if (packet.pdwStatus)
		*packet.pdwStatus = status;
	if (stream->callback)
	{
		pthread_mutex_unlock(&mixer_lock);
		stream->callback(stream->context, packet.pContext, status);
		mixer_lock_take();
	}
	else if (packet.hCompletionEvent)
	{
		SetEvent(packet.hCompletionEvent);
	}
}

static void streams_complete_finished(void)
{
	struct sdl_stream *stream;

	mixer_lock_take();
	for (stream = streams; stream; stream = stream->next)
	{
		while (stream->packet_count && stream->packets[stream->packet_head].finished)
			stream_complete_head(stream, XMEDIAPACKET_STATUS_SUCCESS, stream->packets[stream->packet_head].packet.dwMaxSize);
	}
	pthread_mutex_unlock(&mixer_lock);
}

/* ---------- stream interface */

static struct sdl_stream *stream_from_interface(void *stream)
{
	return (struct sdl_stream *)stream;
}

static ULONG STDMETHODCALLTYPE stream_add_reference(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);
	ULONG count;

	mixer_lock_take();
	count = ++stream->reference_count;
	pthread_mutex_unlock(&mixer_lock);
	return count;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object);

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);
	struct sdl_stream **link;
	ULONG count;

	mixer_lock_take();
	count = --stream->reference_count;
	pthread_mutex_unlock(&mixer_lock);
	if (count)
		return count;

	stream_flush(object);
	mixer_lock_take();
#ifdef DSOUND_MIXER_YIELDS
	/* (the mixer, having let the lock go, goes on with this voice next: it goes on with the one after) */
	if (mixer_walk_next == stream)
		mixer_walk_next = stream->next;
#endif
	for (link = &streams; *link; link = &(*link)->next)
	{
		if (*link == stream)
		{
			*link = stream->next;
			break;
		}
	}
	pthread_mutex_unlock(&mixer_lock);
	free(stream);
	return 0;
}

static HRESULT STDMETHODCALLTYPE stream_get_info(IDirectSoundStream *object, LPXMEDIAINFO information)
{
	struct sdl_stream *stream = stream_from_interface(object);

	memset(information, 0, sizeof(*information));
	information->dwFlags = XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC;
	information->dwInputSize = stream->adpcm ? XBOX_ADPCM_BLOCK_BYTES * stream->channels : 2 * stream->channels;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_status(IDirectSoundStream *object, LPDWORD status)
{
	struct sdl_stream *stream = stream_from_interface(object);

	mixer_lock_take();
	*status = stream->packet_count < MAXIMUM_STREAM_PACKETS ? XMO_STATUSF_ACCEPT_INPUT_DATA : 0;
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

/* the serial of the last packet submitted (under the lock) */
static unsigned long packet_serial;

#ifdef DSOUND_DECODE_THREAD
/* a packet's samples to its entry, found by its serial (it may have been
completed, flushed or its stream released meanwhile: then they are freed) */
static void packet_samples_give(struct sdl_stream *stream, unsigned long serial, short *samples, unsigned long frames)
{
	struct sdl_stream *walk;

	mixer_lock_take();
	for (walk = streams; walk && walk != stream; walk = walk->next)
		;
	if (walk)
	{
		unsigned long position;

		for (position = 0; position < walk->packet_count; position++)
		{
			struct voice_packet *entry = &walk->packets[(walk->packet_head + position) % MAXIMUM_STREAM_PACKETS];

			if (entry->serial == serial && !entry->ready)
			{
				entry->samples = samples;
				entry->frames = samples ? frames : 0;
				entry->ready = TRUE;
				samples = NULL;
				break;
			}
		}
	}
	pthread_mutex_unlock(&mixer_lock);
	packet_deallocate(samples);
}

/* ---------- the decoder (the Wii U: beside the GX2 thread, wiiu_shell.c) */

#define DECODER_JOBS 256

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t posted;
	BOOL started;
	BOOL profile_enabled; /* immutable after the worker is started */
	unsigned long first, count;
	struct decoder_job
	{
		struct sdl_stream *stream;
		unsigned long serial;
		unsigned char *source; /* a copy of the packet's data, the job's (the game's may be reused once the packet is flushed) */
		unsigned long size, channels;
		BOOL adpcm;
#ifdef HALO_WIIU
		uint64_t posted_ticks;
#endif
	} jobs[DECODER_JOBS];
	/* for the log: the most jobs waiting at once */
	unsigned long most_waiting;
} decoder = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };

static short *packet_decode(BOOL adpcm, const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frames)
{
	return adpcm ? decode_adpcm(source, size, channels, frames) : decode_pcm(source, size, channels, frames);
}

static void *decoder_thread(void *parameter)
{
	struct wiiu_decoder_profile profile = { 0 };
	uint64_t thresholds[3];

	(void)parameter;
	thresholds[0] = OSMillisecondsToTicks(2);
	thresholds[1] = OSMillisecondsToTicks(10);
	thresholds[2] = OSMillisecondsToTicks(16);
	{
		void wiiu_thread_beside_gx2(void);

		wiiu_thread_beside_gx2();
	}
	for (;;)
	{
		struct decoder_job job;
		short *samples;
		unsigned long frames = 0;
		uint64_t started_ticks = 0, decoded_ticks = 0, published_ticks = 0;

		pthread_mutex_lock(&decoder.lock);
		while (!decoder.count)
			pthread_cond_wait(&decoder.posted, &decoder.lock);
		job = decoder.jobs[decoder.first];
		decoder.first = (decoder.first + 1) % DECODER_JOBS;
		decoder.count--;
		pthread_mutex_unlock(&decoder.lock);
		if (decoder.profile_enabled)
			started_ticks = OSGetSystemTime();
		{
			/* (the detailed overlay's AUDIO: the decoder's time, a monotonic count of ticks; two reads of the time base a packet) */
			const uint32_t perf_started = (uint32_t)OSGetSystemTime();

			samples = packet_decode(job.adpcm, job.source, job.size, job.channels, &frames);
			wiiu_decoder_perf_ticks_value += (uint32_t)OSGetSystemTime() - perf_started;
		}
		if (decoder.profile_enabled)
			decoded_ticks = OSGetSystemTime();
		packet_deallocate(job.source);
		packet_samples_give(job.stream, job.serial, samples, frames);
		if (decoder.profile_enabled)
		{
			published_ticks = OSGetSystemTime();
			wiiu_decoder_profile_add(&profile, job.posted_ticks, started_ticks, decoded_ticks, published_ticks, thresholds);
		}
		{
			static time_t told;
			const time_t now = time(NULL);

			if (now - told >= 10)
			{
				unsigned long most_waiting, voices_waited;

				told = now;
				pthread_mutex_lock(&decoder.lock);
				most_waiting = decoder.most_waiting;
				decoder.most_waiting = decoder.count;
				pthread_mutex_unlock(&decoder.lock);
				voices_waited = __atomic_exchange_n(&decoder_voices_waited, 0, __ATOMIC_RELAXED);
				platform_log("audio: the decoder had at most %lu packets waiting; voices waited for a packet in %lu mixes",
					most_waiting, voices_waited);
				if (decoder.profile_enabled)
				{
					const uint32_t jobs = profile.jobs;
					const unsigned long fallbacks = __atomic_exchange_n(&decoder_profile_fallbacks, 0, __ATOMIC_RELAXED);

					platform_log("audio profile: %u jobs, queue mean/max %.2f/%.2f ms, decode %.2f/%.2f ms, publish max %.2f ms",
						(unsigned)jobs, jobs ? (double)OSTicksToMicroseconds(profile.total[0] / jobs) / 1000.0 : 0.0,
						(double)OSTicksToMicroseconds(profile.maximum[0]) / 1000.0,
						jobs ? (double)OSTicksToMicroseconds(profile.total[1] / jobs) / 1000.0 : 0.0,
						(double)OSTicksToMicroseconds(profile.maximum[1]) / 1000.0,
						(double)OSTicksToMicroseconds(profile.maximum[2]) / 1000.0);
					platform_log("audio profile: queue >=2/10/16 ms %u/%u/%u, caller fallbacks %lu",
						(unsigned)profile.queue_at_least[0], (unsigned)profile.queue_at_least[1],
						(unsigned)profile.queue_at_least[2], fallbacks);
					{
						struct wiiu_packet_pool_stats memory;

						wiiu_packet_pool_stats(&memory, 1);
						platform_log("audio memory: %u requests, %u pool hits, %u heap allocations (%u failed), %u heap frees, cached %u buffers / %u bytes",
							(unsigned)memory.requests, (unsigned)memory.hits, (unsigned)memory.heap_allocations,
							(unsigned)memory.heap_failures, (unsigned)memory.heap_frees,
							(unsigned)memory.cached_blocks, (unsigned)memory.cached_bytes);
						if (memory.retries || memory.size_rejections)
							platform_log("audio memory: %u allocation retries after reclaiming %u idle buffers; %u overflow rejections (no heap request)",
								(unsigned)memory.retries, (unsigned)memory.reclaimed_blocks, (unsigned)memory.size_rejections);
					}
					memset(&profile, 0, sizeof(profile));
				}
			}
		}
	}
	return NULL;
}

static void decoder_start(void)
{
	pthread_t thread;
	FILE *file = fopen(WIIU_DATA_DIRECTORY "/audio_profile.txt", "r");
	int enabled = 0;

	if (file)
	{
		if (fscanf(file, "%d", &enabled) != 1)
			enabled = 0;
		fclose(file);
	}
	decoder.profile_enabled = enabled == 1;
	if (decoder.profile_enabled)
		platform_log("audio profile: decoder timing enabled (audio_profile.txt)");

	if (pthread_create(&thread, NULL, decoder_thread, NULL) == 0)
	{
		pthread_detach(thread);
		decoder.started = TRUE;
	}
	else
	{
		platform_log("audio: no decoder thread; packets are decoded as they come");
	}
}

/* FALSE if the queue is full or there is no memory for the copy (the caller decodes) */
static BOOL decoder_post(struct sdl_stream *stream, unsigned long serial, const XMEDIAPACKET *input)
{
	BOOL posted = FALSE;
	/* (the data copied now: the job owns what it reads; a quarter of the decoded size for ADPCM) */
	unsigned char *copy = packet_allocate(input->dwMaxSize ? input->dwMaxSize : 1);

	if (!copy)
	{
#ifdef HALO_WIIU
		if (decoder.profile_enabled)
			__atomic_add_fetch(&decoder_profile_fallbacks, 1, __ATOMIC_RELAXED);
#endif
		return FALSE;
	}
	if (input->dwMaxSize)
		memcpy(copy, input->pvBuffer, input->dwMaxSize);
	pthread_mutex_lock(&decoder.lock);
	if (decoder.count < DECODER_JOBS)
	{
		struct decoder_job *job = &decoder.jobs[(decoder.first + decoder.count) % DECODER_JOBS];

		job->stream = stream;
		job->serial = serial;
		job->source = copy;
		job->size = input->dwMaxSize;
		job->channels = stream->channels;
		job->adpcm = stream->adpcm;
#ifdef HALO_WIIU
		job->posted_ticks = decoder.profile_enabled ? OSGetSystemTime() : 0;
#endif
		decoder.count++;
		if (decoder.count > decoder.most_waiting)
			decoder.most_waiting = decoder.count;
		posted = TRUE;
		copy = NULL;
		pthread_cond_signal(&decoder.posted);
	}
	pthread_mutex_unlock(&decoder.lock);
	packet_deallocate(copy);
#ifdef HALO_WIIU
	if (!posted && decoder.profile_enabled)
		__atomic_add_fetch(&decoder_profile_fallbacks, 1, __ATOMIC_RELAXED);
#endif
	return posted;
}
#endif

static HRESULT STDMETHODCALLTYPE stream_process(IDirectSoundStream *object, LPCXMEDIAPACKET input, LPCXMEDIAPACKET output)
{
	struct sdl_stream *stream = stream_from_interface(object);
	struct voice_packet *entry;
	unsigned long frames = 0, serial;
	short *samples = NULL;
	BOOL later = FALSE;

	(void)output;
	if (!input)
		return E_INVALIDARG;
#ifdef DSOUND_DECODE_THREAD
	later = decoder.started;
#endif
	/* decode outside the lock (or on the decoder's thread) */
	if (!later)
		samples = stream->adpcm ?
			decode_adpcm(input->pvBuffer, input->dwMaxSize, stream->channels, &frames) :
			decode_pcm(input->pvBuffer, input->dwMaxSize, stream->channels, &frames);
	mixer_lock_take();
	if (stream->packet_count == MAXIMUM_STREAM_PACKETS)
	{
		pthread_mutex_unlock(&mixer_lock);
		packet_deallocate(samples);
		return E_OUTOFMEMORY;
	}
	entry = &stream->packets[(stream->packet_head + stream->packet_count) % MAXIMUM_STREAM_PACKETS];
	entry->packet = *input;
	entry->samples = samples;
	entry->frames = samples ? frames : 0;
	entry->finished = FALSE;
	entry->ready = !later;
	entry->serial = serial = ++packet_serial;
	if (input->pdwStatus)
		*input->pdwStatus = XMEDIAPACKET_STATUS_PENDING;
	if (input->pdwCompletedSize)
		*input->pdwCompletedSize = 0;
	if (!stream->packet_count)
	{
		/* a stream that ran dry starts over */
		stream->cursor = 0;
		resampler_reset(stream);
		stream->gains_valid = FALSE;
	}
	stream->packet_count++;
	pthread_mutex_unlock(&mixer_lock);
#ifdef DSOUND_DECODE_THREAD
	if (later && !decoder_post(stream, serial, input))
	{
		samples = packet_decode(stream->adpcm, input->pvBuffer, input->dwMaxSize, stream->channels, &frames);
		packet_samples_give(stream, serial, samples, frames);
	}
#else
	(void)serial;
#endif
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_discontinuity(IDirectSoundStream *object)
{
	(void)object;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);

	mixer_lock_take();
	if (stream->flushing)
	{
		pthread_mutex_unlock(&mixer_lock);
		return S_OK;
	}
	voice_cut(stream);
	/* Completion callbacks release the lock. Only the captured fade may
	play then, not the packets still awaiting cancellation. */
	stream->flushing = TRUE;
	while (stream->packet_count)
	{
		struct voice_packet *head = &stream->packets[stream->packet_head];

		/* Cancellation is synchronous. SUCCESS would let the game's
		completion callback refill the stream we are stopping. */
		stream_complete_head(stream, XMEDIAPACKET_STATUS_FLUSHED,
			head->finished ? head->packet.dwMaxSize : 0);
	}
	stream->flushing = FALSE;
	stream->cursor = 0;
	resampler_reset(stream);
	/* (the next sound on the channel says where it is) */
	stream->stereo_positioned = FALSE;
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

/* A stereo voice of a sound in the world (`positioned`) is panned towards
it, `pan` from -1 (left) to 1 (right), and muffled and reverberated as a 3D
voice is (its I3DL2 source, which the game sets as a 3D channel's), its room
send rolling off from `minimum_distance` with `distance`. The game fades its
volume with distance itself, by `distance_fade`. Not positioned, it plays as
the Xbox played every stereo sound, unpanned and dry. It is shifted as a 3D
voice is by `approach`, how fast it comes towards the listener in world units
a second (doppler_ratio). (sound_manager.c,
update_channels: sound_dsound_xbox.c calls this.) */
void dsound_sdl_stream_set_stereo_position(IDirectSoundStream *object, BOOL positioned, float pan,
	float distance, float minimum_distance, float distance_fade, float approach)
{
	struct sdl_stream *stream = stream_from_interface(object);

	mixer_lock_take();
	stream->stereo_positioned = positioned && stream->channels == 2;
	stream->stereo_pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
	stream->stereo_distance = distance > 0.0f ? distance : 0.0f;
	stream->stereo_distance_fade = distance_fade < 0.0f ? 0.0f : (distance_fade > 1.0f ? 1.0f : distance_fade);
	stream->stereo_approach = approach == approach ? approach : 0.0f;
	/* (as the game gives a 3D channel: no maximum) */
	stream->minimum_distance = minimum_distance > 0.0f ? minimum_distance : 0.0f;
	stream->maximum_distance = 3.4e38f;
	pthread_mutex_unlock(&mixer_lock);
}

static IDirectSoundStreamVtbl stream_vtable =
{
	stream_add_reference,
	stream_release,
	stream_get_info,
	stream_get_status,
	stream_process,
	stream_discontinuity,
	stream_flush,
};

/* ---------- the DirectSound object */

struct sdl_direct_sound
{
	ULONG reference_count;
};

static struct sdl_direct_sound direct_sound = { 0 };

HRESULT WINAPI DirectSoundCreate(LPGUID device_id, LPDIRECTSOUND *result, LPUNKNOWN outer)
{
	(void)device_id;
	(void)outer;
	audio_start();
	direct_sound.reference_count++;
	*result = (LPDIRECTSOUND)&direct_sound;
	return DS_OK;
}

ULONG WINAPI IDirectSound_Release(LPDIRECTSOUND sound)
{
	(void)sound;
	return direct_sound.reference_count ? --direct_sound.reference_count : 0;
}

VOID WINAPI DirectSoundDoWork(void)
{
	static unsigned long volume_read_at = (unsigned long)-1;

	/* (audio settings read again when the
	settings change: on the game's thread, not the mixer's, whose lock the
	config's file I/O would hold) */
	if (volume_read_at != config_changes())
	{
		float volume = (float)config_real("audio.volume");
		BOOL room = config_boolean("audio.reverb");
		BOOL stereo_doppler = config_boolean("audio.stereo_doppler");
		BOOL hrtf = config_boolean("audio.hrtf");
		int resampler = resampler_mode_read();

		volume_read_at = config_changes();
		mixer_settings_publish(volume, room, stereo_doppler, hrtf, resampler);
	}
	streams_complete_finished();
}

VOID WINAPI DirectSoundUseFullHRTF(void)
{
	/* The measured full HRTF is already the default on every output. The
	user can override it with audio.hrtf; there is no reduced HRTF bank. */
}

HRESULT WINAPI IDirectSound_GetCaps(LPDIRECTSOUND sound, LPDSCAPS caps)
{
	(void)sound;
	memset(caps, 0, sizeof(*caps));
	caps->dwFree2DBuffers = 64;
	caps->dwFree3DBuffers = 64;
	caps->dwFreeBufferSGEs = 2047;
	caps->dwMemoryAllocated = 0;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_GetSpeakerConfig(LPDIRECTSOUND sound, LPDWORD speaker_config)
{
	(void)sound;
	*speaker_config = output_channels == 6 ? DSSPEAKER_ENABLE_AC3 : DSSPEAKER_STEREO;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_DownloadEffectsImage(LPDIRECTSOUND sound, LPCVOID image, DWORD image_size,
	LPCDSEFFECTIMAGELOC image_location, LPDSEFFECTIMAGEDESC *image_description)
{
	(void)sound;
	/* Halo's environment effect is implemented by dsound_xbox_reverb.inl;
	this backend does not execute uploaded MCPX DSP machine code. */
	(void)image;
	(void)image_size;
	(void)image_location;
	if (image_description)
		*image_description = NULL;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CommitDeferredSettings(LPDIRECTSOUND sound) { (void)sound; return DS_OK; }
HRESULT WINAPI IDirectSound_SetMixBinHeadroom(LPDIRECTSOUND sound, DWORD mix_bin_mask, DWORD headroom) { (void)sound; (void)mix_bin_mask; (void)headroom; return DS_OK; }
HRESULT WINAPI IDirectSound_SetI3DL2Listener(LPDIRECTSOUND sound, LPCDSI3DL2LISTENER listener_properties, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	environment = *listener_properties;
	environment_serial++;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetDistanceFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.distance_factor = factor > 0.0f ? factor : 1.0f;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.rolloff_factor = factor >= 0.0f ? factor : 1.0f;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetPosition(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.position[0] = x;
	listener.position[1] = y;
	listener.position[2] = z;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetVelocity(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.velocity[0] = x;
	listener.velocity[1] = y;
	listener.velocity[2] = z;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

static void normalize3(float *vector)
{
	float length = sqrtf(dot3(vector, vector));

	if (length > 1.0e-6f)
	{
		vector[0] /= length;
		vector[1] /= length;
		vector[2] /= length;
	}
}

HRESULT WINAPI IDirectSound_SetOrientation(LPDIRECTSOUND sound, FLOAT x_front, FLOAT y_front, FLOAT z_front,
	FLOAT x_top, FLOAT y_top, FLOAT z_top, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.front[0] = x_front;
	listener.front[1] = y_front;
	listener.front[2] = z_front;
	listener.top[0] = x_top;
	listener.top[1] = y_top;
	listener.top[2] = z_top;
	normalize3(listener.front);
	normalize3(listener.top);
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundStream(LPDIRECTSOUND sound, LPCDSSTREAMDESC description,
	LPDIRECTSOUNDSTREAM *result, LPUNKNOWN outer)
{
	struct sdl_stream *stream = calloc(1, sizeof(*stream));
	const WAVEFORMATEX *format = description->lpwfxFormat;

	(void)sound;
	(void)outer;
	if (!stream)
		return E_OUTOFMEMORY;
	stream->object.lpVtbl = &stream_vtable;
	stream->reference_count = 1;
	stream->callback = description->lpfnCallback;
	stream->context = description->lpvContext;
	stream->adpcm = format && format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	stream->channels = format && format->nChannels == 2 ? 2 : 1;
	stream->sample_rate = format ? format->nSamplesPerSec : 0;
	stream->frequency = stream->sample_rate;
	/* (no cut playing: voice_cut) */
	stream->cut_played = CUT_FADE_FRAMES;
	stream->volume = 1.0f;
	/* DirectSound's default mix bins: a mono voice to both fronts, a
	stereo voice's channels to the front left and right */
	stream->mix_left = 1.0f;
	stream->mix_right = 1.0f;
	stream->surround_bins[0][0] = 1.0f;
	stream->surround_bins[stream->channels - 1][1] = 1.0f;
	stream->has_3d = (description->dwFlags & DSSTREAMCAPS_CTRL3D) != 0;
	stream->mode = DS3DMODE_NORMAL;
	stream->minimum_distance = DS3D_DEFAULTMINDISTANCE;
	stream->maximum_distance = DS3D_DEFAULTMAXDISTANCE;
	resampler_reset(stream);
	mixer_lock_take();
	stream->next = streams;
	streams = stream;
	pthread_mutex_unlock(&mixer_lock);
	*result = &stream->object;
	return DS_OK;
}

/* January-era DirectSound exports the game declares itself
(sound_dsound_xbox.c); the XDK 3911 headers no longer carry them */

void __stdcall DirectSoundStopStream(LPDIRECTSOUNDSTREAM stream)
{
	stream_flush(stream);
}

unsigned long __stdcall DirectSoundGetStreamVoiceStatus(LPDIRECTSOUNDSTREAM stream)
{
	struct sdl_stream *record = stream_from_interface(stream);
	unsigned long active;

	mixer_lock_take();
	active = record->packet_count != 0;
	pthread_mutex_unlock(&mixer_lock);
	return active;
}

#define STREAM_SETTER(body) \
	struct sdl_stream *record = stream_from_interface(stream); \
	mixer_lock_take(); \
	body; \
	pthread_mutex_unlock(&mixer_lock); \
	return DS_OK;

HRESULT WINAPI IDirectSoundStream_SetFrequency(LPDIRECTSOUNDSTREAM stream, DWORD frequency)
{
	STREAM_SETTER(record->frequency = frequency ? frequency : record->sample_rate)
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM stream, LONG volume)
{
	STREAM_SETTER(record->volume = gain_from_millibels(volume))
}

/* the speaker bins of a mask, for 5.1, in the order of its set bits: a stereo
voice sends its left channel to the first, third, ... and its right to the
second, fourth, ... (the game's stereo voices on AC-3 take front left, front
right, back left, back right). The crosstalk, reverb and effect sends are not
speakers. volumes is NULL for 0 dB. */
static void stream_set_surround_bins(struct sdl_stream *record, DWORD mix_bin_mask, const LONG *volumes, BOOL clear)
{
	unsigned long bit, index = 0;

	if (clear)
		memset(record->surround_bins, 0, sizeof(record->surround_bins));
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) & DSMIXBIN_SPEAKER_MASK)
		{
			record->surround_bins[record->channels == 2 ? index & 1 : 0][bit] =
				volumes ? gain_from_millibels(volumes[index]) : 1.0f;
		}
		index++;
	}
}

HRESULT WINAPI IDirectSoundStream_SetMixBins(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask)
{
	STREAM_SETTER(
		record->mix_left = (mix_bin_mask & DSMIXBIN_FRONT_LEFT) ? 1.0f : 0.0f;
		record->mix_right = (mix_bin_mask & DSMIXBIN_FRONT_RIGHT) ? 1.0f : 0.0f;
		stream_set_surround_bins(record, mix_bin_mask, NULL, TRUE))
}

/* volumes come in the order of the set bits of the mask */
HRESULT WINAPI IDirectSoundStream_SetMixBinVolumes(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask, const LONG *volumes)
{
	struct sdl_stream *record = stream_from_interface(stream);
	unsigned long bit, index = 0;

	mixer_lock_take();
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) == DSMIXBIN_FRONT_LEFT)
			record->mix_left = gain_from_millibels(volumes[index]);
		else if ((1UL << bit) == DSMIXBIN_FRONT_RIGHT)
			record->mix_right = gain_from_millibels(volumes[index]);
		index++;
	}
	stream_set_surround_bins(record, mix_bin_mask, volumes, FALSE);
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundStream_SetMode(LPDIRECTSOUNDSTREAM stream, DWORD mode, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->mode = mode)
}

HRESULT WINAPI IDirectSoundStream_SetPosition(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->position[0] = x; record->position[1] = y; record->position[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetMinDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->minimum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetMaxDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->maximum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetI3DL2Source(LPDIRECTSOUNDSTREAM stream, LPCDSI3DL2BUFFER source, DWORD apply)
{
	LONG direct, direct_hf, room, room_hf;

	(void)apply;
	/* the direct path: at low frequencies obstruction and occlusion take
	their LF ratio of their high frequency levels, which (with the source's
	own) muffle it above the HF reference */
	direct = source->lDirect +
		(LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio) +
		(LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	if (direct > 0)
		direct = 0;
	direct_hf = source->lDirect + source->lDirectHF + source->Obstruction.lHFLevel + source->Occlusion.lHFLevel;
	/* the room send: occlusion lowers and muffles it as it does the direct
	path; obstruction does not lower it, but its muffling (its high
	frequency level over its LF ratio's part) reaches it too. The Xbox
	muffles a voice once, before its mix bins, the reverb's among them: an
	Xbox recording of a voice obstructed by -1200 (LF ratio 0) in a hall is
	the recording of it occluded by -1200, sample for sample (the
	difference 38 dB down, the noise's), the reverb's tail as muffled as
	the direct sound */
	room = source->lRoom + (LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	/* the source's own Room HF does nothing: recorded on an Xbox (the test
	bed's ninth run), the cave's reverb of a voice at a Room HF of -1204
	was as loud at 10 kHz as at 250 Hz, and one at a Room and Room HF of
	-1204 (the game's for most of its 3D sounds) or -2000 was 12 or 20 dB
	down at every frequency, to 0.1 dB; the listener's Room HF (each
	environment's) and occlusion's muffling reach the send as before. Taken
	as the I3DL2 level it is, it muffled the reverberation of nearly every
	3D sound by 12 dB more at the HF reference */
	room_hf = source->lRoom + source->Occlusion.lHFLevel +
		(source->Obstruction.lHFLevel - (LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio));
	{
		STREAM_SETTER(
			record->direct = direct;
			record->direct_hf = direct_hf;
			record->room = room;
			record->room_hf = room_hf;
			record->room_rolloff_factor = source->flRoomRolloffFactor)
	}
}

HRESULT WINAPI IDirectSoundStream_Pause(LPDIRECTSOUNDSTREAM stream, DWORD pause)
{
	STREAM_SETTER(record->paused = pause == DSSTREAMPAUSE_PAUSE)
}

HRESULT WINAPI IDirectSoundStream_SetVelocity(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->velocity[0] = x; record->velocity[1] = y; record->velocity[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetConeAngles(LPDIRECTSOUNDSTREAM stream, DWORD inside, DWORD outside, DWORD apply) { (void)stream; (void)inside; (void)outside; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOrientation(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply) { (void)stream; (void)x; (void)y; (void)z; (void)apply; return DS_OK; }
HRESULT WINAPI IDirectSoundStream_SetConeOutsideVolume(LPDIRECTSOUNDSTREAM stream, LONG volume, DWORD apply) { (void)stream; (void)volume; (void)apply; return DS_OK; }

/* ---------- buffers */

HRESULT WINAPI DirectSoundCreateBuffer(LPCDSBUFFERDESC description, LPDIRECTSOUNDBUFFER *result)
{
	struct sdl_buffer *buffer;
	const WAVEFORMATEX *format;

	if (!result)
		return DSERR_INVALIDCALL;
	*result = NULL;
	if (!description || !description->lpwfxFormat)
		return DSERR_INVALIDCALL;
	format = description->lpwfxFormat;
	if ((format->nChannels != 1 && format->nChannels != 2) || !format->nSamplesPerSec ||
		(format->wFormatTag != WAVE_FORMAT_XBOX_ADPCM &&
			(format->wFormatTag != WAVE_FORMAT_PCM || format->wBitsPerSample != 16)))
		return DSERR_UNSUPPORTED;
	buffer = calloc(1, sizeof(*buffer));
	if (!buffer)
		return E_OUTOFMEMORY;
	buffer->reference_count = 1;
	buffer->channels = format->nChannels;
	buffer->rate = format->nSamplesPerSec;
	buffer->adpcm = format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	buffer->block_bytes = buffer->channels * (buffer->adpcm ? XBOX_ADPCM_BLOCK_BYTES : 2);
	buffer->block_frames = buffer->adpcm ? XBOX_ADPCM_BLOCK_SAMPLES : 1;
	buffer->volume = (float)config_real("audio.effects_volume");
	mixer_lock_take();
	buffer->next = buffers;
	buffers = buffer;
	pthread_mutex_unlock(&mixer_lock);
	*result = (LPDIRECTSOUNDBUFFER)buffer;
	audio_start();
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundBuffer(LPDIRECTSOUND sound, LPCDSBUFFERDESC description,
	LPDIRECTSOUNDBUFFER *result, LPUNKNOWN outer)
{
	(void)sound;
	(void)outer;
	return DirectSoundCreateBuffer(description, result);
}

ULONG WINAPI IDirectSoundBuffer_Release(LPDIRECTSOUNDBUFFER buffer)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer, **link;
	ULONG count;

	if (!record)
		return 0;
	mixer_lock_take();
	count = --record->reference_count;
	if (!count)
	{
		for (link = &buffers; *link && *link != record; link = &(*link)->next)
			;
		if (*link)
			*link = record->next;
		free(record->samples);
		free(record);
	}
	pthread_mutex_unlock(&mixer_lock);
	return count;
}

HRESULT WINAPI IDirectSoundBuffer_SetBufferData(LPDIRECTSOUNDBUFFER buffer, LPVOID data, DWORD size)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	short *samples = NULL;
	unsigned long frames = 0;

	if (!record || (!data && size) || size % record->block_bytes ||
		size / record->block_bytes > (size_t)-1 / (record->block_frames * record->channels * sizeof(short)))
		return DSERR_INVALIDCALL;
	if (size)
	{
		samples = record->adpcm ? decode_adpcm(data, size, record->channels, &frames) :
			decode_pcm(data, size, record->channels, &frames);
		if (!samples)
			return E_OUTOFMEMORY;
	}
	mixer_lock_take();
	free(record->samples);
	record->samples = samples;
	record->frames = record->loop_end = frames;
	record->loop_start = 0;
	record->position = 0;
	record->playing = FALSE;
	record->current_volume = 0;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Play(LPDIRECTSOUNDBUFFER buffer, DWORD reserved1, DWORD reserved2, DWORD flags)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;

	(void)reserved1;
	(void)reserved2;
	if (!record)
		return DSERR_INVALIDCALL;
	mixer_lock_take();
	record->looping = (flags & DSBPLAY_LOOPING) != 0;
	if (record->looping && record->position >= record->loop_end)
		record->position = record->loop_start;
	record->playing = record->frames != 0;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Stop(LPDIRECTSOUNDBUFFER buffer)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;

	if (!record)
		return DSERR_INVALIDCALL;
	mixer_lock_take();
	record->playing = FALSE;
	record->current_volume = 0;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetCurrentPosition(LPDIRECTSOUNDBUFFER buffer, DWORD play_cursor)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	unsigned long frame;

	if (!record || play_cursor % record->block_bytes)
		return DSERR_INVALIDCALL;
	mixer_lock_take();
	if (play_cursor / record->block_bytes > record->frames / record->block_frames)
	{
		pthread_mutex_unlock(&mixer_lock);
		return DSERR_INVALIDCALL;
	}
	frame = play_cursor / record->block_bytes * record->block_frames;
	if (frame >= record->frames && (frame || record->frames))
	{
		pthread_mutex_unlock(&mixer_lock);
		return DSERR_INVALIDCALL;
	}
	record->position = record->looping && frame >= record->loop_end ? record->loop_start : frame;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetLoopRegion(LPDIRECTSOUNDBUFFER buffer, DWORD loop_start, DWORD loop_length)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	unsigned long start, length;

	if (!record || loop_start % record->block_bytes || loop_length % record->block_bytes || !loop_length)
		return DSERR_INVALIDCALL;
	mixer_lock_take();
	if (loop_start / record->block_bytes > record->frames / record->block_frames ||
		loop_length / record->block_bytes > record->frames / record->block_frames)
	{
		pthread_mutex_unlock(&mixer_lock);
		return DSERR_INVALIDCALL;
	}
	start = loop_start / record->block_bytes * record->block_frames;
	length = loop_length / record->block_bytes * record->block_frames;
	if (start >= record->frames || length > record->frames - start)
	{
		pthread_mutex_unlock(&mixer_lock);
		return DSERR_INVALIDCALL;
	}
	record->loop_start = start;
	record->loop_end = start + length;
	if (record->looping && record->position >= record->loop_end)
		record->position = start;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetPitch(LPDIRECTSOUNDBUFFER buffer, LONG pitch)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	double rate;

	if (!record)
		return DSERR_INVALIDCALL;
	/* Xbox pitch is absolute: zero means 48 kHz, with 4096 units per octave
	(xboxdevwiki.net/APU, Pitch calculation). It is not a multiplier of the
	buffer's original sample rate. Bound the exponent before applying the
	same frequency limits as the game's stream voices. */
	if (pitch < -32768) pitch = -32768;
	if (pitch > 32767) pitch = 32767;
	rate = 48000.0 * pow(2.0, (double)pitch / 4096.0);
	if (rate < DSBFREQUENCY_MIN) rate = DSBFREQUENCY_MIN;
	if (rate > DSBFREQUENCY_MAX) rate = DSBFREQUENCY_MAX;
	mixer_lock_take();
	record->rate = rate;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER buffer, LONG volume)
{
	struct sdl_buffer *record = (struct sdl_buffer *)buffer;
	float gain;

	if (!record)
		return DSERR_INVALIDCALL;
	if (volume > 0) volume = 0;
	gain = gain_from_millibels(volume) * (float)config_real("audio.effects_volume");
	mixer_lock_take();
	record->volume = gain;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}
