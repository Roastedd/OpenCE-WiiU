"""The Wii U's voice chat as the console would run it (port/wiiu/src/wiiu_voice_audio.c, with wiiu_voice_core.c and the real
Opus) over stand-ins for Cafe OS (port/wiiu/tests/voice_mock, port/wiiu/tests/test_voice_audio.c): threads on pthreads and a
clock that moves only when every thread sleeps, and mic.rpl in either of its readings, playing a tone. The microphone's frames
encoded and played back, the packet of the frame before and none stale, HOME (answered, and not), the close, a microphone that
does not open, a speaker forgotten, debug.voice_test, what the host is told, and the probe; and a fault in each that a case
must catch.

    HALO_HARNESS_TARGET='--target=powerpc-linux-gnu -fsigned-char -fuse-ld=lld -no-pie' HALO_HARNESS_RUNNER=qemu-ppc \\
        python3 -m pytest -q tools/harness/tests/test_wiiu_voice_audio.py
    HALO_TEST_VERBOSE=1 shows each case's log
"""

import concurrent.futures
import functools
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, ROOT, mutated, read, runner, timeout_scale  # noqa: E402
from tools import linux_build  # noqa: E402

CASES = ["round-trip", "stale", "home", "home-unanswered", "close", "retry", "position", "position-auto", "forget", "voice-test",
         "hears", "no-thread", "probe", "probe-position"]

# a fault in wiiu_voice_audio.c, and the case that must catch it
NEGATIVE_CONTROLS = {
    # (the voice thread closes the microphone at HOME and never says so: the release callback waits its 100 ms)
    "suspend-unanswered": (("\t\t\t\tmicrophone_failed = 0;\n\t\t\t\t__atomic_store_n(&acknowledged, wanted, __ATOMIC_RELEASE);\n",
                            "\t\t\t\tmicrophone_failed = 0;\n"), "home"),
    # (the microphone not opened again for 10 s when the foreground is back)
    "not-reopened": (("\t\t\t\t/* (open again at once when the foreground is back) */\n\t\t\t\tmicrophone_failed = 0;\n",
                      "\t\t\t\tmicrophone_failed = 1;\n\t\t\t\tmicrophone_failed_ms = now_ms();\n"), "home"),
    # (a packet sent however late it comes back)
    "stale-sent": (("#define VOICE_STALE_MS 60u", "#define VOICE_STALE_MS 60000u"), "stale"),
    # (the close not joining the thread)
    "close-not-joined": (("\tOSJoinThread(&voice_thread, NULL);\n", ""), "close"),
    # (a microphone that would not open tried again every second)
    "retry-soon": (("#define VOICE_RETRY_MS 10000u", "#define VOICE_RETRY_MS 1000u"), "retry"),
    # (a speaker forgotten but still played)
    "forget-ignored": (("\t\tif (entry.kind == PLAY_FORGET)\n\t\t{\n\t\t\tfor (index = 0; index < VOICE_SLOTS; index++)\n"
                        "\t\t\t\tif (slots[index].used && slots[index].speaker == entry.speaker)\n\t\t\t\t\tslot_forget(index);\n"
                        "\t\t\tcontinue;\n\t\t}", "\t\tif (entry.kind == PLAY_FORGET)\n\t\t\tcontinue;"), "forget"),
    # (HOME during the probe not answered: the microphone left to the release callback)
    "probe-home-ignored": (("\tif ((wanted & 3u) == REQUEST_RUN)\n\t\treturn 0;\n\tif (was_open)",
                            "\tif (1)\n\t\treturn 0;\n\tif (was_open)"), "probe"),
}

_work = tempfile.TemporaryDirectory(prefix="halo-voice-audio-")


def _compiler():
    return os.environ.get("CC", "clang")


def _target():
    # (HALO_HARNESS_TARGET replaces -m32, as harness.build has it)
    return os.environ.get("HALO_HARNESS_TARGET", "-m32").split()


@functools.lru_cache(maxsize=None)
def _opus_objects():
    """Opus's library as the builds have it (tools/linux_build.py's sources and defines), compiled once for the target"""
    opus = ROOT / linux_build.OPUS_DIR
    out = Path(_work.name) / "opus"
    out.mkdir(exist_ok=True)
    flags = [*_target(), "-std=gnu11", "-O2", *linux_build.OPUS_DEFINES, "-w",
             *(f"-I{opus / name}" for name in ("include", "celt", "silk", "silk/float"))]

    def compiled(source):
        obj = out / (source.relative_to(ROOT / linux_build.OPUS_DIR).as_posix().replace("/", "_") + ".o")
        result = subprocess.run([_compiler(), *flags, "-c", str(source), "-o", str(obj)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"{source} does not compile:\n{result.stderr}")
        return obj

    # (sources.txt as linux_build.opus_sources reads it, from the repository's root whatever the directory)
    sources = [opus / line.strip() for line in (opus / "sources.txt").read_text().splitlines() if line.strip()]
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        return tuple(pool.map(compiled, sources))


@functools.lru_cache(maxsize=None)
def build(fault=None):
    """test_voice_audio.c with wiiu_voice_audio.c (or a copy with a fault), the core and Opus"""
    work = Path(tempfile.mkdtemp(prefix="build-", dir=_work.name))
    source = ROOT / "port/wiiu/src/wiiu_voice_audio.c"
    if fault:
        copy = work / "wiiu_voice_audio.c"
        copy.write_text(mutated(read("port/wiiu/src/wiiu_voice_audio.c"), *fault))
        source = copy
    executable = work / "test_voice_audio"
    command = [_compiler(), *_target(), "-std=gnu11", "-O2", "-g", "-Wall", "-Werror", "-Wno-unused-function", "-pthread",
               f'-DVOICE_AUDIO_SOURCE="{source}"', "-I", str(ROOT / "port/wiiu/tests/voice_mock"), "-I", str(ROOT / "port/wiiu/src"),
               "-I", str(ROOT / "port/linux/src"), str(ROOT / "port/wiiu/tests/test_voice_audio.c"),
               str(ROOT / "port/wiiu/src/wiiu_voice_core.c"), *map(str, _opus_objects()), "-o", str(executable), "-lm"]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"test_voice_audio.c does not compile:\n{result.stderr}")
    return executable


def run(executable, case):
    """one case, with a folder of its own for the card's files: (exit status, output)"""
    directory = tempfile.mkdtemp(prefix=f"{case}-", dir=_work.name)
    result = subprocess.run([*runner(), str(executable), case], capture_output=True, text=True, timeout=120 * timeout_scale(),
                            env=dict(os.environ, HALO_TEST_DIRECTORY=directory))
    return result.returncode, (result.stdout + result.stderr).strip()


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build(), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build(fault), case)
    assert status == CHECK_FAILED, f"wiiu_voice_audio.c with a fault ({control}) passed '{case}': the test cannot see it\n{output}"
