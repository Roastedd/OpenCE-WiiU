"""Builds the friends layer's drivers (tests/social_*.c) from the real sources of port/linux/src, for the machine, or for big-endian PowerPC under qemu-ppc
(HALO_HARNESS_TARGET and HALO_HARNESS_RUNNER, as tools/harness/__init__.py has them). Unlike the harness's tests that cut functions out of the sources, these compile
the units whole: the engine, its codec and the crypto they use (p2p_crypto.c, Monocypher), against a fake broker (tests/social_fake.h). A negative control is a
source with one deliberate fault: the sources are copied to a folder of their own, the fault made in the copy, and the driver built from that."""

import functools
import hashlib
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TESTS = Path(__file__).resolve().parent / "tests"
HARNESS_INCLUDE = Path(__file__).resolve().parent / "include"

# the engine's units, in the folder they are included from by the drivers
SOURCES = ("social_codec.c", "social_core.c", "social_friends.c", "social_party.c", "social_codec.h", "social_core.h")
EXTRA_SOURCES = ("social_store.c", "social_store.h", "social_join.c", "social_join.h", "social_front.c", "social_front.h", "social_command.c",
                 "social_command.h")
CRYPTO = ("port/linux/src/p2p_crypto.c", "port/third_party/monocypher/monocypher.c", "port/third_party/monocypher/monocypher-ed25519.c",
          "port/linux/game/text_safe.c")

_directory = tempfile.TemporaryDirectory(prefix="halo-social-")


class CheckFailed(Exception):
    pass


def target():
    return os.environ.get("HALO_HARNESS_TARGET", "-m32").split()


def runner():
    return os.environ.get("HALO_HARNESS_RUNNER", "").split()


def big_endian():
    return "powerpc" in os.environ.get("HALO_HARNESS_TARGET", "")


def timeout_scale():
    return 10 if runner() else 1


def sanitized():
    return os.environ.get("SOCIAL_SANITIZE") == "1"


def _flags():
    flags = [*target(), "-std=gnu99", "-O1", "-ffp-contract=off", "-g"]
    if sanitized():
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"]
    return flags


def _include_flags(extra=()):
    return [*extra, f"-I{ROOT / 'port/linux/src'}", f"-I{ROOT / 'port/third_party/monocypher'}", f"-I{HARNESS_INCLUDE}",
            f"-I{TESTS}", "-idirafter", str(ROOT / "port/linux/include"), "-idirafter", str(ROOT / "port/include/xdk")]


def _compile(source, output, extra_include=(), warnings=True):
    command = [os.environ.get("CC", "clang"), *_flags(), *_include_flags(extra_include), "-c", str(source), "-o", str(output)]
    if warnings:
        command[1:1] = ["-Wall", "-Wextra", "-Werror", "-Wno-unused-command-line-argument", "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-sign-compare", "-Wno-unused-variable"]
    else:
        command[1:1] = ["-w", "-Wno-unused-command-line-argument"]
    result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    if result.returncode:
        raise RuntimeError(f"{source} does not compile:\n{result.stderr[-6000:]}")


@functools.lru_cache(maxsize=None)
def crypto_objects():
    """the crypto, text safety and a stand-in for the platform's random bytes, built once"""
    work = Path(_directory.name) / "crypto"
    work.mkdir(exist_ok=True)
    objects = []
    for path in CRYPTO:
        output = work / (Path(path).stem + ".o")
        _compile(ROOT / path, output, warnings=False)
        objects.append(output)
    stand_in = work / "social_stubs.c"
    stand_in.write_text("#include <stdlib.h>\n#include <string.h>\nvoid posix_random_bytes(unsigned char *bytes, int size) { int i; for (i = 0; i < size; i++) "
                        "bytes[i] = (unsigned char)(rand() >> 7); }\n"
                        "void p2p_hex(const unsigned char *b, int n, char *t) { static const char d[] = \"0123456789abcdef\"; int i; for (i = 0; i < n; i++) "
                        "{ t[i * 2] = d[b[i] >> 4]; t[i * 2 + 1] = d[b[i] & 15]; } t[n * 2] = 0; }\n")
    output = work / "social_stubs.o"
    _compile(stand_in, output, warnings=False)
    objects.append(output)
    return tuple(objects)


def sources_folder(mutations=()):
    """a folder with src/ (the engine's sources, with mutations ((file, before, after), ...) made) and game/text_safe.h"""
    key = hashlib.sha256(repr(mutations).encode()).hexdigest()[:16]
    folder = Path(_directory.name) / f"sources-{key}"
    if folder.exists():
        return folder
    (folder / "src").mkdir(parents=True)
    (folder / "game").mkdir()
    for name in SOURCES + EXTRA_SOURCES:
        path = ROOT / "port/linux/src" / name
        if path.exists():
            (folder / "src" / name).write_text(path.read_text(encoding="latin-1"), encoding="latin-1")
    for name in ("text_safe.h", "text_safe.c"):
        (folder / "game" / name).write_text((ROOT / "port/linux/game" / name).read_text(encoding="latin-1"), encoding="latin-1")
    for name, before, after in mutations:
        path = folder / "src" / name
        text = path.read_text(encoding="latin-1")
        if text.count(before) != 1:
            raise LookupError(f"negative control no longer applies to {name} (found {text.count(before)} times): {before!r}")
        path.write_text(text.replace(before, after, 1), encoding="latin-1")
    return folder


@functools.lru_cache(maxsize=None)
def build(driver, mutations=(), defines=()):
    """tests/<driver>.c with the engine's sources (mutated copies if mutations are given) and the crypto, as an executable"""
    folder = sources_folder(mutations)
    tag = hashlib.sha256(repr((driver, mutations, defines)).encode()).hexdigest()[:12]
    executable = Path(_directory.name) / f"{driver}-{tag}"
    object_file = Path(_directory.name) / f"{driver}-{tag}.o"
    _compile(TESTS / f"{driver}.c", object_file, extra_include=[f"-I{folder / 'src'}", *[f"-D{d}" for d in defines]])
    link = [os.environ.get("CC", "clang"), *_flags(), str(object_file), *map(str, crypto_objects()), "-o", str(executable), "-lm", "-lpthread"]
    result = subprocess.run(link, capture_output=True, text=True, cwd=ROOT)
    if result.returncode:
        raise RuntimeError(f"{driver} does not link:\n{result.stderr[-4000:]}")
    return executable


def run(executable, *arguments, timeout=300):
    """(exit status, standard output, standard error)"""
    result = subprocess.run([*runner(), str(executable), *arguments], capture_output=True, text=True, errors="replace",
                            timeout=timeout * timeout_scale(), cwd=ROOT)
    return result.returncode, result.stdout, result.stderr
