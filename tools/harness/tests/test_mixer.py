"""The real mixer of dsound_sdl.c on known signals (mixer.c): its resampler against the same low pass worked out in
double precision, sines through it as the ear hears them, voices out of earshot, mono voices and the linear
resampler.

    python tools/harness/tests/test_mixer.py metrics   the figures
    python tools/harness/tests/test_mixer.py bench [voices] [out of earshot] [indoor]   a game's worth of voices, timed
    python tools/harness/tests/test_mixer.py render > out.raw   noise through the mixer, to compare with another's
(MIXER_SOURCE=path: of another dsound_sdl.c; MIXER_SCALAR=1: its resampler adding tap by tap)
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, big_endian, build, function, mutated, read, run, runner, timeout_scale  # noqa: E402

SOURCE = "port/linux/src/dsound_sdl.c"
CASES = ["reference", "impulse", "sines", "aliases", "silent-voice", "mono", "pending-packet", "pending-ramp", "linear"]
GAIN_PUBLISHER = function(read(SOURCE), "mixer_settings_publish")

# a fault in the mixer, and the case that must catch it
NEGATIVE_CONTROLS = {
    "taps-off-by-one": (("(stream->center + 1 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY",
                         "(stream->center + 2 - RESAMPLER_ZERO_CROSSINGS) % RESAMPLER_HISTORY"), "reference"),
    "phase-not-blended": (("for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase++)",
                           "for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase += 2)"), "impulse"),
    "no-stretch": (("#define RESAMPLER_MAXIMUM_STRETCH 2", "#define RESAMPLER_MAXIMUM_STRETCH 1"), "aliases"),
    "pending-as-silence": (("*waiting = TRUE;", "*waiting = FALSE;"), "pending-packet"),
    "pending-ramp-jumps": (("stream->current_left = waiting ? left : target_left;", "stream->current_left = target_left;"), "pending-ramp"),
    "linear-nearest": (("left_frames[0] + (left_frames[1] - left_frames[0]) * fraction", "left_frames[0]"), "linear"),
}


def generated(source=None, fault=None, scalar=False, cache=False, gains=False):
    """the mixer: dsound_sdl.c from its constants to the end of the mixing (before the output), its resampler adding
    tap by tap if scalar"""
    text = source if source is not None else read(SOURCE)
    code = text[text.index("#define OUTPUT_RATE"):text.index("/* ---------- output")]
    if fault:
        code = mutated(code, *fault)
    prefix, suffix = "#define packet_allocate malloc\n", ""
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
    (void)name; gain_config_read(); return gain_config_reverb;
}
static void streams_complete_finished(void) { gain_config_completions++; }
""" + function(text, "DirectSoundDoWork").replace("VOID WINAPI DirectSoundDoWork", "static void gain_config_do_work", 1)
    return (("under_test.inc", "#pragma STDC FP_CONTRACT OFF\n" +
             ("#define RESAMPLER_SCALAR\n" if scalar else "") +
             ("#define RESAMPLER_CACHE_TEST\n" if cache else "") +
             ("#define RESAMPLER_GAIN_CACHE_TEST\n" if gains else "") + prefix + code + suffix),)


def rendered(scalar, cache=False, gains=False):
    """mixer.c's render: voices at each rate, then a scene's whole mix, as floats"""
    import array
    import subprocess

    result = subprocess.run([*runner(), str(build("mixer", generated(scalar=scalar, cache=cache, gains=gains))), "render"],
                            capture_output=True, timeout=120 * timeout_scale(), check=True)
    samples = array.array("f", result.stdout)
    if big_endian() != (sys.byteorder == "big"):
        samples.byteswap()
    return samples


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("mixer", generated()), case)
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
    status, output = run(build("mixer", generated(fault=fault)), case)
    assert status == CHECK_FAILED, f"the mixer with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    import os
    import subprocess

    # (MIXER_SOURCE: another dsound_sdl.c, to compare; MIXER_SCALAR=1: its resampler tap by tap)
    other = os.environ.get("MIXER_SOURCE")
    executable = build("mixer", generated(Path(other).read_text(encoding="latin-1") if other else None,
                                          scalar=bool(os.environ.get("MIXER_SCALAR"))))
    sys.exit(subprocess.run([str(executable), *(sys.argv[1:] or ["metrics"])]).returncode)
