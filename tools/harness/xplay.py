"""Cross-architecture wire tests (test_xplay_*.py): the game's real network units built twice, once for the little-endian
machine every other player has (32-bit x86) and once for the big-endian one the Wii U is (32-bit PowerPC), run, and their
output compared with the committed golden files in tests/xplay_golden.

Unlike the other harness tests these compile the game's own source files (the port's flags: tools/linux_build.py's) rather
than functions cut out of them, so a driver (tests/xplay_*.c) can #include a unit and reach its static functions, and the
real structures are the ones tested. Whatever the unit calls and the driver does not need is left undefined: the objects
are linked with --unresolved-symbols=ignore-all, so a call that is reached by mistake crashes the driver rather than
failing the build.

A leg is a (compiler, flags, runner, environment) tuple, found on this machine:

    le  x86: native -m32 on an x86-64 Linux with 32-bit libraries; else the i686 cross compiler under qemu-i386
    be  PowerPC: the powerpc-linux-gnu compiler under qemu-ppc

Both are in the opence-ppc-be image (tools/ppc-be/run.sh harness tools/harness/tests/test_xplay_*.py). Where a leg is not
available its tests are skipped, and the other is still held to the golden files.
"""

import functools
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TESTS = Path(__file__).resolve().parent / "tests"
GOLDEN = TESTS / "xplay_golden"

sys.path.insert(0, str(ROOT))

_directory = tempfile.TemporaryDirectory(prefix="halo-xplay-")


class Leg:
    def __init__(self, name, cc, target_flags, runner, environment, big_endian):
        self.name = name
        self.cc = cc
        self.target_flags = target_flags
        self.runner = runner
        self.environment = environment
        self.big_endian = big_endian


def _probe(cc, flags, runner, environment):
    """whether a trivial program compiles and runs on this leg"""
    work = Path(_directory.name) / "probe"
    work.mkdir(exist_ok=True)
    source = work / "probe.c"
    source.write_text("int main(void) { return 0; }\n")
    executable = work / ("probe-" + str(abs(hash((cc, tuple(flags))))))
    try:
        built = subprocess.run([cc, *flags, str(source), "-o", str(executable)], capture_output=True)
        if built.returncode:
            return False
        ran = subprocess.run([*runner, str(executable)], capture_output=True, env={**os.environ, **environment},
                             timeout=60)
        return ran.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


@functools.lru_cache(maxsize=None)
def leg(name):
    """the leg 'le' or 'be' of this machine, or None"""
    if name == "le":
        if platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64"):
            flags = ["--target=i686-linux-gnu", "-m32"]
            if _probe("clang", flags, [], {}):
                return Leg("le", "clang", flags, [], {}, False)
        if shutil.which("qemu-i386"):
            environment = {"QEMU_LD_PREFIX": "/usr/i686-linux-gnu"}
            flags = ["--target=i686-linux-gnu", "-m32", "-fuse-ld=lld", "-no-pie"]
            for cc in ("clang-i686", "clang"):
                if shutil.which(cc) and _probe(cc, flags, ["qemu-i386"], environment):
                    return Leg("le", cc, flags, ["qemu-i386"], environment, False)
        return None
    if name == "be":
        if shutil.which("qemu-ppc"):
            environment = {"QEMU_LD_PREFIX": "/usr/powerpc-linux-gnu"}
            flags = ["--target=powerpc-linux-gnu", "-fsigned-char", "-fuse-ld=lld", "-no-pie"]
            if shutil.which("clang") and _probe("clang", flags, ["qemu-ppc"], environment):
                return Leg("be", "clang", flags, ["qemu-ppc"], environment, True)
        return None
    raise ValueError(name)


def legs():
    """the legs this machine has, by name"""
    return {name: found for name in ("le", "be") if (found := leg(name))}


def _game_flags(target):
    """the flags the Linux build compiles the game's units with (tools/linux_build.py), for the leg's byte order"""
    import tools.linux_build as build

    sln = types.SimpleNamespace(linux_target="ppc" if target.big_endian else "x86", port_release=False, port_portable=True,
                                port_profile=False)
    config = build._load_port_config()
    semantics = _semantics_header()
    abi = build.target_flags(sln, build.LINUX_ABI_FLAGS)
    # (the leg's own target flags replace the build's: the same architecture, run as this machine can)
    abi = [flag for flag in abi if not flag.startswith("--target=") and flag not in ("-m32",)]
    if target.big_endian:
        abi += ["-msvr4-struct-return"] if "-msvr4-struct-return" not in abi else []
    return [*target.target_flags, *abi, *build.GAME_FLAGS, "-include", "port/linux/include/halo_linux_prefix.h",
            "-include", str(semantics), "-Iport/linux/include", "-iquote", config["game_sources"],
            *shlex.split(build.game_defines_and_includes(config)), "-idirafter", "port/include/xdk"]


@functools.lru_cache(maxsize=None)
def _platform_semantics_header():
    """platform_msvc_semantics.h: the same for the platform layer, which includes the SDK declarations alone"""
    path = Path(_directory.name) / "platform_msvc_semantics.h"
    command = [sys.executable, "tools/linux_msvc_semantics.py", "--output", str(path), "--inlines", "port/include/xdk"]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("linux_msvc_semantics.py failed:\n" + result.stderr)
    return path


@functools.lru_cache(maxsize=None)
def _semantics_header():
    """halo_msvc_semantics.h, as the build generates it (the same for both byte orders)"""
    path = Path(_directory.name) / "halo_msvc_semantics.h"
    command = [sys.executable, "tools/linux_msvc_semantics.py", "--output", str(path), "--all-inlines", "--tags", "source",
               "--inlines", "source", "--inlines", "port/include/xdk"]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("linux_msvc_semantics.py failed:\n" + result.stderr)
    return path


def _platform_flags(target):
    """the flags the Linux build compiles the platform layer's units (port/linux/src) with"""
    import tools.linux_build as build

    flags = _game_flags(target)
    # (the same ABI, the platform layer's language and includes in the game's place)
    abi = flags[:flags.index("-std=gnu89")]
    return [*abi, *build.PLATFORM_FLAGS, "-include", "port/linux/include/halo_linux_prefix.h", "-include",
            str(_platform_semantics_header()), "-Iport/linux/src",
            "-Iport/linux/include", "-Iport/third_party/tomlc17", "-Iport/third_party/expat", "-Iport/third_party/kcp",
            "-Iport/third_party/monocypher", "-Iport/third_party/zlib", "-Isource", "-Isource/cseries", "-idirafter",
            "port/include/xdk"]


def _compile(target, source, output, extra=(), first=(), platform=False):
    command = [target.cc, *first, *(_platform_flags(target) if platform else _game_flags(target)), "-O1", *extra, "-c",
               str(source), "-o", str(output)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{source} does not compile for {target.name}:\n{result.stderr[-4000:]}")


@functools.lru_cache(maxsize=None)
def _mutated(mutations):
    """a folder with the files of mutations ((path, before, after), ...) as they would be with one deliberate fault each
    (a negative control: the test must fail), keyed by their names"""
    folder = Path(_directory.name) / f"mutated-{abs(hash(mutations)):x}"
    folder.mkdir(exist_ok=True)
    for path, before, after in mutations:
        target = folder / Path(path).name
        text = target.read_text(encoding="latin-1") if target.exists() else (ROOT / path).read_text(encoding="latin-1")
        if text.count(before) != 1:
            raise LookupError(f"negative control no longer applies to {path} (found {text.count(before)} times): {before!r}")
        target.write_text(text.replace(before, after, 1), encoding="latin-1")
    return folder


@functools.lru_cache(maxsize=None)
def _unit(target_name, source, mutations=(), platform=False):
    """a game source file compiled once for a leg (with its mutation, if it has one in mutations)"""
    target = leg(target_name)
    output = Path(_directory.name) / f"{target_name}-{Path(source).stem}-{abs(hash((source, mutations))):x}.o"
    if any(path == source for path, _, _ in mutations):
        folder = _mutated(mutations)
        _compile(target, folder / Path(source).name, output, first=["-iquote", str(ROOT / Path(source).parent)],
                 platform=platform)
    else:
        _compile(target, ROOT / source, output, platform=platform)
    return output


@functools.lru_cache(maxsize=None)
def build(driver, target_name, units=(), defines=(), mutations=(), platform=False):
    """tests/<driver>.c, which includes the units it tests, and the other game source files in units linked in, for a leg"""
    target = leg(target_name)
    work = Path(_directory.name)
    tag = abs(hash((units, defines, mutations)))
    executable = work / f"{driver}-{target_name}-{tag:x}"
    # (units under port/linux/src and port/third_party are the platform layer's, built as it is)
    objects = [_unit(target_name, unit, mutations, unit.startswith(("port/linux/src/", "port/third_party/")))
               for unit in units]
    driver_object = work / f"{driver}-{target_name}-{tag:x}.o"
    first = ["-iquote", str(_mutated(mutations))] if mutations else []
    _compile(target, TESTS / f"{driver}.c", driver_object, [f"-I{TESTS}", f"-I{HARNESS_INCLUDE}", *defines], first,
             platform=platform)
    link = [target.cc, *target.target_flags, str(driver_object), *map(str, objects), "-o", str(executable), "-lm",
            "--error-limit=0" if False else "-Wl,--error-limit=0"]
    result = subprocess.run(link, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        # (what the units call and the driver does not need, which is never reached: a zeroed object each, so that the link
        # holds together, where the call it is never made from would otherwise be a relocation against nothing)
        missing = sorted(set(re.findall(r"undefined symbol: (\S+)", result.stderr)))
        if not missing:
            raise RuntimeError(f"{driver} does not link for {target_name}:\n{result.stderr[-4000:]}")
        if os.environ.get("XPLAY_DEBUG"):
            print(f"{driver} ({target_name}) leaves undefined: " + " ".join(missing), file=sys.stderr)
        stubs = work / f"{driver}-{target_name}-{tag:x}-stubs.s"
        stubs.write_text("\n".join(f".globl {name}\n.bss\n.balign 16\n.type {name},@object\n.size {name},256\n{name}:\n.zero 256"
                                    for name in missing) + "\n")
        stub_object = work / f"{driver}-{target_name}-{tag:x}-stubs.o"
        built = subprocess.run([target.cc, *target.target_flags, "-c", str(stubs), "-o", str(stub_object)],
                               capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError("the stubs do not assemble:\n" + built.stderr[-2000:])
        result = subprocess.run([*link, str(stub_object)], cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"{driver} does not link for {target_name}:\n{result.stderr[-4000:]}")
    return executable


HARNESS_INCLUDE = ROOT / "tools" / "harness" / "include"


def run(executable, target_name, *arguments):
    """(status, standard output, standard error) of a driver on a leg"""
    target = leg(target_name)
    result = subprocess.run([*target.runner, str(executable), *arguments], capture_output=True, text=True, errors="replace",
                            env={**os.environ, **target.environment}, timeout=300, cwd=ROOT)
    return result.returncode, result.stdout, result.stderr


def golden_path(name):
    return GOLDEN / f"{name}.txt"


def check_golden(name, output, leg_name="le"):
    """output must equal the committed golden file (HALO_XPLAY_UPDATE=1 rewrites it: from the little-endian leg only, and the
    other leg is then held to it)"""
    path = golden_path(name)
    if os.environ.get("HALO_XPLAY_UPDATE") == "1":
        if leg_name == "le":
            path.parent.mkdir(exist_ok=True)
            path.write_text(output)
        if leg_name == "le" or not path.exists():
            return
    expected = path.read_text()
    if output != expected:
        import difflib

        diff = "".join(list(difflib.unified_diff(expected.splitlines(True), output.splitlines(True), "golden", "this run"))[:80])
        raise AssertionError(f"{name}: the bytes differ from {path.name}:\n{diff}")


def check_layouts(target_name, source, inc):
    """compile the layout check for a module on a leg: RuntimeError (with the compiler's words) where the tables and the
    declarations differ"""
    target = leg(target_name)
    output = Path(_directory.name) / f"check-{target_name}-{Path(inc).stem}.o"
    _compile(target, TESTS / "xplay_layout_check.c", output,
             [f"-I{TESTS}", f"-DXPLAY_MODULE_SOURCE=\"{Path(source).name}\"", f"-DXPLAY_MODULE_INC=\"{inc}\""])


def vectors_include():
    """a folder with xplay_vectors.inc, made from the master protocol's test vectors (tests/master_vectors.json): the host-signed
    listings and the master's signed list that tests/xplay_p2p.c reads on each byte order"""
    import json

    folder = Path(_directory.name) / "vectors"
    folder.mkdir(exist_ok=True)
    vectors = json.loads((TESTS / "master_vectors.json").read_text())

    def string(text):
        return json.dumps(text)

    lines = ["struct xplay_vector_listing { const char *hex; int version, flags; unsigned long sequence, time; int players, "
             "max_players, engine, kind, valid; const char *name, *map, *gametype; };",
             "static const struct xplay_vector_listing xplay_vector_listings[] = {"]
    for listing in vectors["listings"]:
        lines.append(f"\t{{ {string(listing['bytes'])}, {listing['network_version']}, {listing['flags']}, {listing['sequence']}UL, "
                     f"{listing['time']}UL, {listing['players']}, {listing['max_players']}, {listing['engine']}, "
                     f"{listing['kind']}, {1 if listing['valid'] else 0}, {string(listing['name'])}, {string(listing['map'])}, "
                     f"{string(listing['gametype'])} }},")
    lines.append("};")
    lines.append(f"#define XPLAY_VECTOR_LISTING_COUNT {len(vectors['listings'])}")
    master = vectors["master"]["public_key"].split(":")[1]
    lines.append(f"static const char xplay_vector_master_key[] = {string(master)};")
    lines.append(f"static const char xplay_vector_list_body[] = {string(vectors['list']['body'])};")
    lines.append(f"static const char xplay_vector_list_signature[] = {string(vectors['list']['signature_header'])};")
    lines.append(f"static const char xplay_vector_list_key_id[] = {string(vectors['list']['signature_key_header'])};")
    lines.append(f"static const unsigned long xplay_vector_list_time = {vectors['list']['signature_time_header']}UL;")
    (folder / "xplay_vectors.inc").write_text("\n".join(lines) + "\n")
    return folder
