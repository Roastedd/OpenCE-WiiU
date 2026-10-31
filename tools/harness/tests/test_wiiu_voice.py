"""The Wii U's voice chat without the console (port/wiiu/src/wiiu_voice_core.c, wiiu_voice.c): the queues between its
threads, a speaker's ring for the mixer, the 32 to 48 kHz resampler, the microphone's frames, both readings of the system's
microphone ring, the stale packets and a speaker's sequence; and a fault in each that a case must catch.

Big-endian as the console is (verify.sh's harness runs):

    HALO_HARNESS_TARGET='--target=powerpc-linux-gnu -fsigned-char -fuse-ld=lld -no-pie' HALO_HARNESS_RUNNER=qemu-ppc \\
        python3 -m pytest -q tools/harness/tests/test_wiiu_voice.py
    python tools/harness/tests/test_wiiu_voice.py metrics     the resampler's response, in figures
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, mutated, read, run  # noqa: E402

CASES = ["constants", "queue-order", "queue-backlog", "queue-race", "queue-threads", "ring", "resampler-dc",
         "resampler-response", "resampler-chunks", "capture", "tone", "mic-consume", "mic-position", "stale", "sequence"]

# a fault in wiiu_voice_core.c, and the case that must catch it
NEGATIVE_CONTROLS = {
    # (a full queue takes one more, over the oldest)
    "queue-overfills": (("\tif (head - tail >= queue->capacity)\n\t\treturn 0;", "\tif (head - tail > queue->capacity)\n\t\treturn 0;"),
                        "queue-order"),
    # (the backlog never trimmed: the producer writes over what the consumer has not taken)
    "backlog-unbounded": (("\twhile (head - tail >= keep)\n", "\twhile (0 && head - tail >= keep)\n"), "queue-backlog"),
    # (a copy taken whatever the producer did meanwhile: a record half written over, and the tail moved back)
    "take-unchecked": (("\t\tif (__atomic_compare_exchange_n(&queue->tail, &tail, tail + 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))\n"
                        "\t\t\treturn 1;\n\t}\n}",
                        "\t\t__atomic_store_n(&queue->tail, tail + 1, __ATOMIC_RELEASE);\n\t\treturn 1;\n\t}\n}"), "queue-race"),
    # (a new speaker's slot played from where the last one's left off)
    "ring-not-flushed": (("\t\tif ((int32_t)(flush_at - ring->read) > 0)\n", "\t\tif (0)\n"), "ring"),
    # (a voice played as soon as a sample comes: no jitter buffer)
    "ring-no-prebuffer": (("if (!ring->playing && count >= prebuffer)", "if (!ring->playing && count >= 1)"), "ring"),
    # (a full ring takes the frame over the samples not yet mixed)
    "ring-overwrites": (("\tif (count > WIIU_VOICE_RING_SAMPLES - (write - read))\n\t\treturn 0;\n",
                         "\tif (count > WIIU_VOICE_RING_SAMPLES + (write - read))\n\t\treturn 0;\n"), "ring"),
    # (each piece resampled as if it were the first: a click at every poll's edge)
    "resampler-forgets": (("\tuint32_t made = 0, index;\n\n\tfor (index = 0; index < count; index++)\n\t{\n\t\tconst float *window;\n",
                           "\tuint32_t made = 0, index;\n\n\twiiu_voice_resampler_reset(resampler);\n"
                           "\tfor (index = 0; index < count; index++)\n\t{\n\t\tconst float *window;\n"), "resampler-chunks"),
    # (an even input's two phases in the wrong order: images, and a ragged response)
    "resampler-phases-swapped": (("\t\t\toutput[made++] = resampler_dot(resampler_phases[0], window);\n"
                                  "\t\t\toutput[made++] = resampler_dot(resampler_phases[2], window);\n",
                                  "\t\t\toutput[made++] = resampler_dot(resampler_phases[2], window);\n"
                                  "\t\t\toutput[made++] = resampler_dot(resampler_phases[0], window);\n"), "resampler-response"),
    # (the phases not made to add up to 1: a constant comes out louder)
    "resampler-unnormalised": (("(float)(taps[phase + 3 * j] / sum);", "(float)(taps[phase + 3 * j] / (sum * 0.0 + 1.0));"),
                               "resampler-dc"),
    # (the backlog kept by the capture's queue ignored: every frame waits)
    "capture-keeps-all": (("wiiu_voice_queue_put_newest(capture->frames, frame, capture->keep)",
                           "!wiiu_voice_queue_put(capture->frames, frame)"), "capture"),
    # (the system told nothing was consumed: its read index never moves, the same samples read again)
    "mic-never-consumed": (("\t\t*consume = waiting * per_sample;\n", "\t\t*consume = 0;\n"), "mic-consume"),
    # (the ring's samples read little-endian)
    "mic-little-endian": (("int value = (ring[2 * at] << 8) | ring[2 * at + 1];", "int value = (ring[2 * at + 1] << 8) | ring[2 * at];"),
                          "mic-consume"),
    # (bufferPos taken as samples whatever its unit: a write position in bytes read at twice the speed)
    "mic-position-units": (("\tconst uint32_t per_sample = 2u / reader->unit_bytes;\n", "\tconst uint32_t per_sample = 1u;\n"),
                           "mic-position"),
    # (what the system wrote before the opening read as this opening's)
    "mic-position-from-zero": (("\t\t\treader->read = at;\n\t\t\treader->started = 1;\n",
                                "\t\t\treader->read = 0;\n\t\t\treader->started = 1;\n"), "mic-position"),
    # (packets kept however old: a talk's tail sent seconds later)
    "stale-kept": (("now_ms - record.queued_ms > maximum_age_ms || ", ""), "stale"),
    # (a late or repeated packet played)
    "sequence-late-played": (("\t\tif (ahead >= 0x8000u)\n\t\t\treturn -1;\n", ""), "sequence"),
}


def generated(fault=None):
    """the core and its header as the console builds them, with voice_audio.h's constants"""
    source = read("port/wiiu/src/wiiu_voice_core.c")
    if fault:
        source = mutated(source, *fault)
    audio = read("port/linux/src/voice_audio.h")
    config = (f"#define VOICE_FRAME_SAMPLES {constant(audio, 'VOICE_FRAME_SAMPLES')}\n"
              f"#define VOICE_MAXIMUM_PACKET {constant(audio, 'VOICE_MAXIMUM_PACKET')}\n")
    return (("wiiu_voice_core.h", read("port/wiiu/src/wiiu_voice_core.h")), ("config.inc", config), ("under_test.inc", source))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("wiiu_voice", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("wiiu_voice", generated(fault)), case)
    assert status == CHECK_FAILED, f"wiiu_voice_core.c with a fault ({control}) passed '{case}': the test cannot see it\n{output}"


if __name__ == "__main__" and sys.argv[1:] == ["metrics"]:
    print(run(build("wiiu_voice", generated()), "metrics")[1])
