"""The Wii U build (tools/wiiu_build.py, port/wiiu): the ninja graph's flags, what it compiles and leaves out, the link;
and the scripts it runs (the stubs, the link check, the instruction check, the size report).

    python -m pytest -q tools/test_wiiu_build.py

The graph is generated with a stand-in for the compiler, so this needs no Wii U toolchain; the tests of the scripts that
read objects need the opence-wiiu container's (they are skipped without it).
"""

import io
import re
import shutil
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

from tools import linux_build, ninja_syntax, wiiu_build, wiiu_size_report, wiiu_stubs  # noqa: E402

CONTAINER = shutil.which("wiiu-clang") and shutil.which("powerpc-eabi-objdump") and shutil.which("llvm-nm")
needs_container = pytest.mark.skipif(not CONTAINER, reason="needs the opence-wiiu container's toolchain")


@pytest.fixture(autouse=True)
def in_the_repository(monkeypatch):
    """(configure.py runs from the repository's root, and so do these)"""
    monkeypatch.chdir(ROOT)


def graph(monkeypatch, found=True, release=False):
    """build.ninja's Wii U part, as configure.py writes it, its long lines joined again"""
    monkeypatch.setattr(wiiu_build, "find_wiiu_cc", lambda sln: "wiiu-clang" if found else None)
    out = io.StringIO()
    sln = SimpleNamespace(build_dir=Path("build"), wiiu_cc=None, port_release=release, port_profile=False,
                          compiler_launcher=None)
    wiiu_build.generate_wiiu_build(ninja_syntax.Writer(out), sln)
    return re.sub(r" \$\n +", " ", out.getvalue())


def compile_flags(text, source):
    """the cflags of the unit compiled from source"""
    match = re.search(r"^build [^\n]*: wiiu_cc " + re.escape(source.replace(" ", "$ ")) + r"[ \n].*?\n  cflags = ([^\n]*)",
                      text, re.M | re.S)
    assert match, f"{source} is not compiled"
    return match.group(1)


def is_compiled(text, source):
    return re.search(r"^build [^\n]*: wiiu_cc " + re.escape(source.replace(" ", "$ ")) + r"[ \n]", text, re.M) is not None


def test_without_the_compiler_the_graph_says_so(monkeypatch):
    text = graph(monkeypatch, found=False)
    assert "no Wii U compiler found" in text
    assert "\nbuild linux_wiiu:" not in text


def test_the_game_is_compiled_as_the_big_endian_linux_build_is(monkeypatch):
    text = graph(monkeypatch)
    flags = compile_flags(text, "source/main/main.c").split()
    for flag in ("-fshort-wchar", "-ffp-contract=off", "-femulated-tls", "-msvr4-struct-return", "-fsigned-char",
                 "-fms-extensions", "-fwrapv", "-fno-delete-null-pointer-checks", "-fno-strict-aliasing", "-fcommon",
                 "-mcpu=750", "-std=gnu89", "-D__STRICT_ANSI__", "-fno-builtin-wcslen", "-DDEBUG", "-Dxbox"):
        assert flag in flags, flag
    # none of x86's, Linux's or an optimiser the Wii U's linker cannot read
    for flag in ("-malign-double", "-freg-struct-return", "-m32", "-fstack-protector-strong", "-DHALO_RELEASE"):
        assert flag not in flags, flag
    assert not [flag for flag in flags if flag.startswith(("--target", "-flto", "-fprofile"))]
    assert "-DHALO_RELEASE" in compile_flags(graph(monkeypatch, release=True), "source/main/main.c").split()


def test_the_flags_are_the_big_endian_linux_builds_not_copies_of_them(monkeypatch):
    """every flag the ppc build compiles the game with is the Wii U build's, but the Linux target, which the compiler gives"""
    flags = set(compile_flags(graph(monkeypatch), "source/ai/ai.c").split())
    ppc = SimpleNamespace(linux_target="ppc")
    for flag in linux_build.target_flags(ppc, linux_build.LINUX_ABI_FLAGS):
        if not flag.startswith("--target="):
            assert flag in flags, flag


def test_abi_mk_names_flags_the_build_has(monkeypatch):
    """port/wiiu/abi.mk is the flags the probe is built with: they are the game's too"""
    flags = set(compile_flags(graph(monkeypatch), "source/ai/ai.c").split())
    codegen = wiiu_build.abi_makefile_flags() + wiiu_build.abi_makefile_flags("HALO_WIIU_CODEGEN_CLANG")
    assert "-ffp-contract=off" in codegen and "-femulated-tls" in codegen
    for flag in codegen:
        assert flag in flags or flag in ("-mhard-float",), flag


def test_posix_units_have_the_native_abi(monkeypatch):
    text = graph(monkeypatch)
    flags = compile_flags(text, "port/linux/src/posix_dtls.c").split()
    assert "-fshort-wchar" not in flags and "-femulated-tls" in flags and "-fsigned-char" in flags
    assert "-DMINIUPNP_STATICLIB" in compile_flags(text, "port/linux/src/posix_upnp.c").split()
    # (and not the game's: no MSVC semantics, no XDK headers)
    assert "msvc_semantics" not in " ".join(flags) and "xdk" not in " ".join(flags)
    assert "platform_msvc_semantics.h" in compile_flags(text, "port/linux/src/xnet.c")


def test_what_is_left_out_is_listed_with_its_reason(monkeypatch):
    text = graph(monkeypatch)
    for path, reason in wiiu_build.WIIU_EXCLUDE.items():
        assert (ROOT / path).is_file(), f"{path} is not there: take it off WIIU_EXCLUDE"
        assert reason.strip(), path
        assert not is_compiled(text, path), path
    # everything else of the platform layer and mbedtls is compiled
    for source in sorted((ROOT / "port/linux/src").glob("*.c")) + sorted((ROOT / "port/third_party/mbedtls/library").glob("*.c")):
        path = source.relative_to(ROOT).as_posix()
        assert is_compiled(text, path) == (path not in wiiu_build.WIIU_EXCLUDE), path
    assert "miniupnpc/src" not in text


def test_every_game_source_is_compiled(monkeypatch):
    text = graph(monkeypatch)
    sources = linux_build.game_sources(wiiu_build._load_port_config())
    assert len(sources) > 400
    for source in sources:
        assert is_compiled(text, source.as_posix()), source
    for source in sorted((ROOT / "port/linux/game").glob("*.c")):
        assert is_compiled(text, source.relative_to(ROOT).as_posix()), source


def test_every_object_is_checked_and_the_link_is_the_wii_us(monkeypatch):
    text = graph(monkeypatch)
    assert "wiiu_check_objects.py --quiet $out" in text
    link = re.search(r"^rule wiiu_link\n  command = ([^\n]*)", text, re.M).group(1)
    assert "wut.specs" in text and "-lwut" in text and "-mhard-float" in text
    assert "wiiu_link_check.py" in link and "$wiiu_ld" in link
    assert "WUT_ROOT is not set" in link
    # the stubs come from all the objects but themselves, and are linked in
    stubs = re.search(r"^build build/linux-wiiu/wiiu_stubs\.c [^\n]*: wiiu_stubs ([^\n]*)", text, re.M).group(1)
    assert "wiiu_stubs.o" not in stubs and "build/linux-wiiu/obj/source/main/main.o" in stubs
    elf = re.search(r"^build build/linux-wiiu/halo\.elf [^\n]*: wiiu_link ([^\n]*)", text, re.M).group(1)
    assert "build/linux-wiiu/obj/wiiu_stubs.o" in elf and "build/linux-wiiu/obj/source/main/main.o" in elf
    assert re.search(r"^build linux_wiiu: phony build/linux-wiiu/halo\.rpx build/linux-wiiu/halo\.wuhb build/linux-wiiu/halo\.sizes\.txt",
                     text, re.M)
    assert re.search(r"^build build/linux-wiiu/halo\.wuhb: wiiu_wuhb build/linux-wiiu/halo\.rpx", text, re.M)


def test_the_games_main_is_the_shells_to_run(monkeypatch):
    """shell_xbox.c's main is halo_game_main, and the shell's units have the native ABI"""
    text = graph(monkeypatch)
    assert "-Dmain=halo_game_main" in compile_flags(text, "source/shell/shell_xbox.c").split()
    assert "-Dmain=halo_game_main" not in compile_flags(text, "source/main/main.c")
    for name in ("wiiu_main", "wiiu_shell", "wiiu_shell_log", "wiiu_shell_watch", "wiiu_shell_blackbox", "wiiu_crash", "wiiu_atomic",
                 "wiiu_libc", "wiiu_tls", "wiiu_align_fixup"):
        flags = compile_flags(text, f"port/wiiu/src/{name}.c").split()
        assert "-fshort-wchar" not in flags and "-femulated-tls" in flags, name
    # the Xbox kernel's Win32 shims are compiled, not left to stubs
    assert is_compiled(text, "port/linux/src/xbox_kernel.c")


def test_the_wii_us_own_units_are_built_as_the_platform_layer_is(monkeypatch, tmp_path):
    """port/wiiu/src/wiiu_*.c have the native ABI; others the platform layer's"""
    unit = ROOT / "port/wiiu/src" / "zz_test_unit.c"
    native = ROOT / "port/wiiu/src" / "wiiu_zz_test.c"
    try:
        unit.write_text("int zz_test_unit;\n")
        native.write_text("int wiiu_zz_test;\n")
        text = graph(monkeypatch)
        assert "-fshort-wchar" in compile_flags(text, "port/wiiu/src/zz_test_unit.c").split()
        assert "-fshort-wchar" not in compile_flags(text, "port/wiiu/src/wiiu_zz_test.c").split()
    finally:
        unit.unlink(missing_ok=True)
        native.unlink(missing_ok=True)


def test_configure_accepts_the_target_and_leaves_the_others_alone(tmp_path):
    """--linux-target=wiiu replaces the Linux build in build.ninja; without it nothing of the Wii U is in it"""
    # (a folder of links to the repository's, so that configure.py writes its build.ninja here)
    folder = tmp_path / "repository"
    folder.mkdir()
    for entry in ROOT.iterdir():
        if entry.name not in (".git", "build", "build.ninja", "__pycache__"):
            (folder / entry.name).symlink_to(entry)
    # a stand-in compiler on the path, so that the target is generated
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    (bin_dir / "wiiu-clang").write_text("#!/bin/sh\n")
    (bin_dir / "wiiu-clang").chmod(0o755)
    env = {"PATH": f"{bin_dir}:/usr/bin:/bin:/usr/local/bin"}
    for target, expected in (("x86", False), ("wiiu", True)):
        result = subprocess.run([sys.executable, "configure.py", f"--linux-target={target}", "--lto=off", "--pgo=off"],
                                cwd=folder, capture_output=True, text=True, env=env)
        assert result.returncode == 0, result.stderr
        text = (folder / "build.ninja").read_text()
        assert ("\nbuild linux_wiiu: " in text) == expected
        assert ("wiiu_cc" in text) == expected
        assert ("\nbuild linux: " in text) == (not expected)
        (folder / "build.ninja").unlink()


# --- the scripts it runs --------------------------------------------------------------------------------------------


def test_the_stubs_are_written_as_functions_and_tables(tmp_path):
    source, listing = tmp_path / "stubs.c", tmp_path / "stubs.txt"
    wiiu_stubs.write_stubs({"CloseHandle": "F", "D3D__RenderState": "D", "Sleep": "F"}, source, listing)
    text = source.read_text()
    assert "FUNCTION_STUB(CloseHandle)" in text and "FUNCTION_STUB(Sleep)" in text
    assert "DATA_STUB(D3D__RenderState)" in text and "3 of them (2 functions, 1 tables)" in text
    assert "wiiu_log_text(message)" in text and "SYSLaunchMenu" in text and "OSReport" in text
    assert listing.read_text() == "CloseHandle F\nD3D__RenderState D\nSleep F\n"


def test_the_stubs_are_compared_with_the_baseline(tmp_path, capsys):
    listing, baseline = tmp_path / "stubs.txt", tmp_path / "baseline.txt"
    baseline.write_text("# comment\nA\nB\nC\n")
    listing.write_text("A F\nB D\nC F\n")
    assert wiiu_stubs.compare(listing, baseline) == 0
    listing.write_text("A F\nB D\nC F\nNew F\n")
    assert wiiu_stubs.compare(listing, baseline) == 1 and "NEWLY MISSING New" in capsys.readouterr().out
    listing.write_text("A F\nC F\n")
    assert wiiu_stubs.compare(listing, baseline) == 1 and "NOW DEFINED B" in capsys.readouterr().out


def test_the_committed_baseline_is_sorted_and_not_empty():
    names = [line for line in (ROOT / wiiu_build.STUBBED_SYMBOLS).read_text().splitlines() if not line.startswith("#")]
    assert names and names == sorted(names) and len(set(names)) == len(names)


def test_the_memory_map_is_read_from_the_linker_script():
    script = ROOT / "tools" / "zz_wut.ld"
    try:
        script.write_text("MEMORY {\n   codemem (rwx) : ORIGIN = 0x02000000, LENGTH = 224M\n"
                          "   datamem (rw)  : ORIGIN = 0x10000000, LENGTH = 800M\n   loadmem (rwx) : ORIGIN = 0xC0000000, LENGTH = 128M\n}\n")
        regions = wiiu_size_report.regions(script)
    finally:
        script.unlink(missing_ok=True)
    assert regions == {"codemem": (0x02000000, 224 << 20), "datamem": (0x10000000, 800 << 20),
                       "loadmem": (0xC0000000, 128 << 20)}


@needs_container
def test_a_fused_multiply_add_is_found_and_not_found_without_one(tmp_path):
    source = tmp_path / "f.c"
    source.write_text("double f(double a, double b, double c) { return a * b + c; }\n")
    for contract, found in (("fast", True), ("off", False)):
        obj = tmp_path / f"f_{contract}.o"
        subprocess.run(["wiiu-clang", "-O2", f"-ffp-contract={contract}", "-c", str(source), "-o", str(obj)], check=True)
        result = subprocess.run([sys.executable, str(ROOT / "tools/wiiu_check_objects.py"), "--quiet", str(obj)],
                                capture_output=True, text=True)
        assert (result.returncode == 1) == found, result.stdout
        assert ("fmadd" in result.stdout) == found


@needs_container
def test_the_link_check_finds_an_undefined_weak_reference_and_a_wide_string_call(tmp_path):
    good, weak, wide = tmp_path / "good.c", tmp_path / "weak.c", tmp_path / "wide.c"
    good.write_text("int defined_function(void) { return 1; }\n")
    weak.write_text("#pragma weak never_defined\nextern int never_defined(void);\nint f(void) { return never_defined ? 1 : 0; }\n")
    wide.write_text("extern unsigned long wcslen(const unsigned short *);\nunsigned long g(void) { return wcslen(0); }\n")
    objects = {}
    for name, source in (("good", good), ("weak", weak), ("wide", wide)):
        objects[name] = tmp_path / f"{name}.o"
        subprocess.run(["wiiu-clang", "-c", str(source), "-o", str(objects[name])], check=True)

    def check(*names):
        response = tmp_path / "objects.rsp"
        response.write_text("\n".join(str(objects[name]) for name in names))
        return subprocess.run([sys.executable, str(ROOT / "tools/wiiu_link_check.py"), str(response)], capture_output=True,
                              text=True)

    assert check("good").returncode == 0
    assert "never_defined" in check("good", "weak").stdout and check("good", "weak").returncode == 1
    assert "wcslen" in check("good", "wide").stdout
    # defined by one of the objects, it is the port's own and fine
    defined = tmp_path / "defines.c"
    defined.write_text("unsigned long wcslen(const unsigned short *s) { return 0; }\n")
    objects["defines"] = tmp_path / "defines.o"
    subprocess.run(["wiiu-clang", "-c", str(defined), "-o", str(objects["defines"])], check=True)
    assert check("good", "wide", "defines").returncode == 0


@needs_container
def test_the_stub_generator_finds_what_the_linker_lacks(tmp_path):
    caller = tmp_path / "caller.c"
    caller.write_text("extern void missing_function(void);\nextern unsigned char missing_table[];\n"
                      "unsigned char *pointer = missing_table;\nvoid wiiu_log_text(const char *text) { (void)text; }\nint main(void) { missing_function(); return 0; }\n")
    obj = tmp_path / "caller.o"
    subprocess.run(["wiiu-clang", "-c", str(caller), "-o", str(obj)], check=True)
    response = tmp_path / "objects.rsp"
    response.write_text(str(obj))
    out, listing = tmp_path / "stubs.c", tmp_path / "stubs.txt"
    result = subprocess.run([sys.executable, str(ROOT / "tools/wiiu_stubs.py"), "--link",
                             "powerpc-eabi-gcc -specs=$WUT_ROOT/share/wut.specs -mcpu=750 -meabi -mhard-float",
                             "--libs", "-L$WUT_ROOT/lib -lwut -lm", "--objects", str(response), "--output", str(out), "--list",
                             str(listing)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert listing.read_text() == "missing_function F\nmissing_table D\n"
    # the generated source compiles, and with it the link is complete
    stubs = tmp_path / "stubs.o"
    subprocess.run(["wiiu-clang", "-std=gnu11", "-Wall", "-Werror", "-c", str(out), "-o", str(stubs)], check=True)
    link = subprocess.run("powerpc-eabi-gcc -specs=$WUT_ROOT/share/wut.specs -mcpu=750 -meabi -mhard-float "
                          f"{obj} {stubs} -L$WUT_ROOT/lib -lwut -lm -o {tmp_path / 'x.elf'}", shell=True, capture_output=True, text=True)
    assert link.returncode == 0, link.stderr
