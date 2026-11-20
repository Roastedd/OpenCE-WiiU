"""The real mixer of dsound_sdl.c on known signals (mixer.c): its resampler against the same low pass worked out in
double precision, sines through it as the ear hears them, voices out of earshot, mono voices and the linear
resampler, cut fades and synchronous packet cancellation.

    python tools/harness/tests/test_mixer.py metrics   the figures
    python tools/harness/tests/test_mixer.py bench [voices] [out of earshot] [indoor]   a game's worth of voices, timed
    python tools/harness/tests/test_mixer.py render > out.raw   noise through the mixer, to compare with another's
(MIXER_SOURCE=path: of another dsound_sdl.c; MIXER_SCALAR=1: its resampler adding tap by tap)
"""

import functools
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, big_endian, build, function, mutated, read, run, runner, timeout_scale  # noqa: E402
from harness.audio import mixer_source  # noqa: E402

SOURCE = "port/linux/src/dsound_sdl.c"
CASES = ['reference',
 'impulse',
 'sines',
 'aliases',
 'silent-voice',
 'mono',
 'linear',
 'doppler-limits',
 'doppler-stereo',
 'cut',
 'cuts',
 'cut-overlap',
 'flush',
 'cut-lifecycle',
 'hrtf-restart',
         'pending-packet', 'pending-ramp']
GAIN_PUBLISHER = function(read(SOURCE), "mixer_settings_publish")

# a fault in the mixer, and the case that must catch it
NEGATIVE_CONTROLS = {
 'hrtf-history-not-reset': (('memset(stream->hrtf_frames, 0, sizeof(stream->hrtf_frames));', ''), 'hrtf-restart'),
 'taps-off-by-one': (('(stream->center + 1 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY',
                      '(stream->center + 2 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY'),
                     'reference'),
 'phase-not-blended': (('for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase++)',
                        'for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase += 2)'),
                       'impulse'),
 'no-stretch': (('#define RESAMPLER_MAXIMUM_STRETCH 4', '#define RESAMPLER_MAXIMUM_STRETCH 1'), 'aliases'),
 'cut-dead': (('mix_voice(stream, output, send, CUT_FADE_FRAMES);', 'return;'), 'cut'),
 'cut-not-faded': (('float fade = 0.5f + 0.5f * (float)cos(3.14159265358979 * (double)(frame + 1) / CUT_FADE_FRAMES);',
                    'float fade = 1.0f;'),
                   'cut'),
 'cut-past-the-earlier': (('(frame < left ? stream->cut_send[stream->cut_played + frame] : 0.0f)',
                           '(frame < left ? 1.0f : 0.0f) * stream->cut_send[(stream->cut_played + frame) % '
                           'CUT_FADE_FRAMES]'),
                          'cuts'),
 'cut-discard-earlier': (('left = CUT_FADE_FRAMES - stream->cut_played', 'left = 0'), 'cut-overlap'),
 'flush-live-voice': (('stream->paused || stream->flushing', 'stream->paused'), 'flush'),
 'linear-nearest': (('left_frames[0] + (left_frames[1] - left_frames[0]) * fraction', 'left_frames[0]'),
                    'linear'),
 'doppler-stereo-always-on': (('stereo_doppler_enabled && !stream->has_3d', '!stream->has_3d'),
                              'doppler-stereo'),
 'frequency-upper-unbounded': (('if (target_step * OUTPUT_RATE > VOICE_FREQUENCY_MAXIMUM)', 'if (0)'),
                               'doppler-limits'),
 'frequency-lower-unbounded': (('if (!(target_step * OUTPUT_RATE >= VOICE_FREQUENCY_MINIMUM))', 'if (0)'),
                               'doppler-limits')}

# ours: a voice waiting for its decoded packet
NEGATIVE_CONTROLS.update({
    "pending-as-silence": (("*waiting = TRUE;", "*waiting = FALSE;"), "pending-packet"),
    "pending-ramp-jumps": (("stream->current_left = left;", "stream->current_left = target_left;"), "pending-ramp"),
})


def generated(source=None, fault=None, scalar=False, cache=False, gains=False):
    """the mixer: dsound_sdl.c from its constants to the end of the mixing (before the output), its resampler adding
    tap by tap if scalar"""
    text = mixer_source(source)
    code = text[text.index("#define OUTPUT_RATE"):text.index("/* ---------- output")]
    code += "\n" + text[text.index("#define STREAM_SETTER"):text.index("\n\n", text.index("#define STREAM_SETTER"))] + "\n"
    code += "\n" + "\n".join(function(text, name) for name in (
        "packet_release", "stream_complete_head", "stream_from_interface",
        "stream_flush", "stream_release", "stream_process", "IDirectSoundStream_Pause"))
    code += "\n#ifdef UNDER_TEST_I3DL2_SOURCE\n" + function(text, "IDirectSoundStream_SetI3DL2Source") + "\n#endif\n"
    if fault:
        code = mutated(code, *fault)
    prefix, suffix = "#define JOIN_FRAME_STEPF(...) ((void)0)\n#define JOIN_FRAME_STEP(name) ((void)0)\n#define packet_allocate malloc\n#define packet_deallocate free\nstatic unsigned long packet_serial;\n", ""
    if gains:
        # Observe the real publisher's lock acquisition. Configuration reads
        # assert that file-reading work happens before that lock is held.
        prefix += """
static unsigned gain_config_lock_calls;
static int gain_config_lock(pthread_mutex_t *lock)
{
    gain_config_lock_calls++;
    return pthread_mutex_lock(lock);
}
#define pthread_mutex_lock gain_config_lock
"""
        suffix = """
#undef pthread_mutex_lock
static unsigned long gain_config_serial;
static double gain_config_volume = 1.0;
static BOOL gain_config_reverb = TRUE, gain_config_read_locked;
static unsigned gain_config_reads, gain_config_completions;
static unsigned long config_changes(void) { return gain_config_serial; }
static void gain_config_read(void)
{
    gain_config_reads++;
    if (pthread_mutex_trylock(&mixer_lock)) gain_config_read_locked = TRUE;
    else pthread_mutex_unlock(&mixer_lock);
}
static double config_real(const char *name)
{
    (void)name; gain_config_read(); return gain_config_volume;
}
static BOOL config_boolean(const char *name)
{
    gain_config_read(); return !strcmp(name, "audio.reverb") && gain_config_reverb;
}
static void streams_complete_finished(void) { gain_config_completions++; }
""" + function(text, "DirectSoundDoWork").replace("VOID WINAPI DirectSoundDoWork", "static void gain_config_do_work", 1)
    return (("under_test.inc", "#pragma STDC FP_CONTRACT OFF\n" +
             ("#define RESAMPLER_SCALAR\n" if scalar else "") +
             ("#define RESAMPLER_CACHE_TEST\n" if cache else "") +
             ("#define RESAMPLER_GAIN_CACHE_TEST\n" if gains else "") + prefix + code + suffix),)


def build_mixer(generated_code, fixture="mixer"):
    """the mixer's test program (the harness's build: HALO_HARNESS_TARGET, -pthread for the flush worker)"""
    return build(fixture, generated_code)


def rendered(scalar, cache=False, gains=False):
    """mixer.c's render: voices at each rate, then a scene's whole mix, as floats"""
    import array

    result = subprocess.run([*runner(), str(build("mixer", generated(scalar=scalar, cache=cache, gains=gains))), "render"],
                            capture_output=True, timeout=120 * timeout_scale(), check=True)
    samples = array.array("f", result.stdout)
    if big_endian() != (sys.byteorder == "big"):
        samples.byteswap()
    return samples


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build_mixer(generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("case", [*CASES, "cache-keys", "cache-reset", "cache-pitch-gate"])
def test_cached_case(case):
    status, output = run(build("mixer", generated(cache=True)), case)
    assert status == 0, output


def test_cached_coefficients_keep_the_rendered_samples():
    reference, cached = rendered(False), rendered(False, cache=True)
    assert len(reference) == len(cached) > 0
    assert reference == cached, "cached resampler changed an output sample"


@pytest.mark.parametrize("case", [*CASES, "gain-cache", "gain-config"])
def test_gain_cached_case(case):
    status, output = run(build("mixer", generated(cache=True, gains=True)), case)
    assert status == 0, output


def test_gain_cache_keeps_the_rendered_bytes():
    reference, cached = rendered(False, cache=True), rendered(False, cache=True, gains=True)
    assert len(reference) == len(cached) > 0
    assert reference.tobytes() == cached.tobytes(), "gain cache changed rendered float bytes"


def test_gain_cache_keeps_moving_and_config_changes_byte_exact():
    import subprocess

    outputs = [subprocess.run([*runner(), str(build("mixer", generated(cache=True, gains=gains))), "gain-render"],
                              capture_output=True, check=True, timeout=120 * timeout_scale()).stdout
               for gains in (False, True)]
    assert len(outputs[0]) == len(outputs[1]) > 0
    assert outputs[0] == outputs[1], "moving listener/source or config update changed rendered float bytes"


@pytest.mark.parametrize("fault,case", [
    (("key->real[12] = master_volume;", "key->real[12] = 0.0f;"), "gain-cache"),
    (("key->integer[8] = reverb_enabled;", "key->integer[8] = 0;"), "gain-cache"),
    (("key->real[23] = environment.flHFReference;", "key->real[23] = 0.0f;"), "gain-cache"),
    ((GAIN_PUBLISHER, GAIN_PUBLISHER.replace("\tmixer_lock_take();\n", "").replace(
        "\tpthread_mutex_unlock(&mixer_lock);\n", "")), "gain-config"),
])
def test_gain_cache_negative_control(fault, case):
    status, output = run(build("mixer", generated(fault=fault, cache=True, gains=True)), case)
    assert status == CHECK_FAILED, f"gain cache fault escaped {case}: {output}"


@pytest.mark.parametrize("fault,case", [
    (("entry->valid && entry->blend_bits == bits", "entry->valid"), "cache-keys"),
    (("resampler_coefficient_cache[phase][way].valid = FALSE;", "(void)way;"), "cache-reset"),
    (("prepared_coefficients = resampler_cache_frequency(stream->frequency ? stream->frequency : stream->sample_rate);",
      "prepared_coefficients = TRUE;"), "cache-pitch-gate"),
])
def test_cache_negative_control(fault, case):
    status, output = run(build("mixer", generated(fault=fault, cache=True)), case)
    assert status == CHECK_FAILED, f"coefficient cache fault escaped {case}: {output}"


def test_vectors_add_as_taps_do():
    """the resampler four taps at a time is the resampler tap by tap but for the order of its additions: the same
    sound to float's rounding, through the whole mix (reverb and limiter)"""
    vector, scalar = rendered(False), rendered(True)
    assert len(vector) == len(scalar) > 0
    worst = max(abs(a - b) for a, b in zip(vector, scalar))
    assert 0 < worst < 1e-5, f"the vector resampler differs by {worst} (none: no vectors)"


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build_mixer(generated(fault=fault)), case)
    assert status == CHECK_FAILED, f"the mixer with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    # (MIXER_SOURCE: another dsound_sdl.c, to compare; MIXER_SCALAR=1: its resampler tap by tap)
    other = os.environ.get("MIXER_SOURCE")
    executable = build_mixer(generated(Path(other).read_text(encoding="latin-1") if other else None,
                                      scalar=bool(os.environ.get("MIXER_SCALAR"))))
    sys.exit(subprocess.run([str(executable), *(sys.argv[1:] or ["metrics"])]).returncode)
