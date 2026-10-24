"""Ninja rules for the Wii U build (``ninja linux_wiiu``).

The Wii U (port/wiiu/README.md) has a big-endian 32-bit PowerPC CPU, Espresso, a 750-class core. The game is
compiled for it as it is compiled for the big-endian Linux build (``configure.py --linux-target=ppc``), which
runs the game under qemu-ppc and agrees with the x86 build tick by tick: the same flags (this module takes them
from tools/linux_build.py, not copies of them), by clang, but for devkitPPC's EABI, newlib and wut. The compiler is
``wiiu-clang`` (tools/wiiu in the repository this port is developed in: clang for powerpc-unknown-eabi, with
devkitPPC's headers, and objects elf2rpl accepts); the link is devkitPPC's GCC driver with wut's specs and
libraries, and elf2rpl makes the RPX. Run it in the opence-wiiu container.

The build is complete as far as it goes and no further: the platform layer that would let the game start is
not written (port/wiiu/README.md lists it). This graph builds

- the game sources and the port's game units (port/linux/game), as the Linux builds do;
- the platform layer's units that compile for the Wii U, less WIIU_EXCLUDE, with the reason for each (the SDL and
  OpenGL glue, the Linux-only system headers); the Wii U's own units in port/wiiu/src stand in for them as they
  are written;
- the third-party libraries the platform layer uses;
- a generated source (tools/wiiu_stubs.py) that defines every symbol still undefined as a function that names
  itself and stops, so that the link is complete and each real implementation replaces one stub;

and links build/linux-wiiu/halo.elf and halo.rpx, with a report of the sizes against wut's memory map
(tools/wiiu_size_report.py). Every object is checked for the instructions the Wii U port must not have (fused
multiply-adds: tools/wiiu_check_objects.py), and the link for weak references nothing defines and for newlib's
wide-character functions, which assume a 32-bit wchar_t (tools/wiiu_link_check.py).

Not optimised together at the link (no -flto, no profile): devkitPPC's linker does not read LLVM bitcode.
"""

import json
import re
import shutil
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence

from .linux_build import (EXPAT_DIR, EXPAT_SOURCES, GAME_FLAGS, KCP_DIR, LINUX_ABI_FLAGS, MBEDTLS_DIR, MINIUPNPC_DIR,
                          MONOCYPHER_DIR, PLATFORM_FLAGS, POSIX_FLAGS, PPC_TARGET_FLAGS, STB_VORBIS_SOURCE,
                          TOML_DIR, X86_ONLY_FLAGS, X86_TARGET, XDK_INCLUDE, ZLIB_DEFINES, ZLIB_DIR, ZLIB_SOURCES,
                          compile_launcher, configuration_defines, game_defines_and_includes, game_sources,
                          linux_configure_inputs, musl_math_cflags, musl_math_sources, opus_cflags,
                          opus_sources, updater_defines, xdk_headers)
from .ninja_syntax import Writer

LINUX_DIR = Path("port/linux")
PORT_CONFIG = LINUX_DIR / "port.json"
WIIU_DIR = Path("port/wiiu")
ABI_MAKEFILE = WIIU_DIR / "abi.mk"
# what the link still lacks, as the last build found it (tools/wiiu_stubs.py --compare)
STUBBED_SYMBOLS = WIIU_DIR / "stubbed_symbols.txt"
BUILD_NAME = "linux-wiiu"

# Espresso is a 750-class core: no fsqrt, paired singles unused
WIIU_MARCH = "-mcpu=750"
# An RPX has no TLS segment (elf2rpl refuses the relocations): thread-local variables are libgcc's emulated ones, which
# need port/wiiu/src/wiiu_tls.c
WIIU_TLS = "-femulated-tls"
# the big-endian Linux build's target flags less the Linux target (wiiu-clang gives the EABI one):
# the structure return convention the game's union-returning converters need, and a signed char
WIIU_TARGET_FLAGS = [flag for flag in PPC_TARGET_FLAGS if not flag.startswith("--target=")]

# The platform layer's sources, and third-party sources, that do not compile for the Wii U yet, with why. Each is
# either replaced by a Wii U unit (port/wiiu/src, as they are written) or removed from here by making it compile.
# tools/wiiu/survey_gate.py (verify.sh in the repository around OpenCE) fails if a source on this list compiles or
# if another does not, so the list is what the compiler finds.
WIIU_EXCLUDE: Dict[str, str] = {
    # OpenGL (gl.h includes SDL3/SDL_opengl.h): the GX2 renderer replaces it
    "port/linux/src/d3d8_gl.c": "Direct3D 8 on OpenGL; the GX2 renderer replaces it",
    "port/linux/src/gl_functions.c": "OpenGL function loading",
    "port/linux/src/hud_hires.c": "the high-resolution HUD's OpenGL drawing",
    "port/linux/src/menu_files.c": "the menus' OpenGL drawing",
    "port/linux/src/nv2a_psh.c": "NV2A pixel shaders to GLSL",
    "port/linux/src/nv2a_vsh.c": "NV2A vertex shaders to GLSL",
    "port/linux/src/xbox_textures.c": "Xbox textures on OpenGL",
    "port/linux/src/xgpu_post.c": "post-processing on OpenGL",
    "port/linux/src/xgpu_text.c": "text on OpenGL",
    # SDL 3: windows, audio, input
    "port/linux/src/sdl_platform.c": "the window and events on SDL 3",
    "port/linux/src/touch_input.c": "touch controls on SDL 3",
    "port/linux/src/voice_audio.c": "voice chat on SDL 3 audio",
    "port/linux/src/xinput_sdl.c": "XInput on SDL 3 gamepads; VPAD/KPAD replace it",
    "port/linux/src/updater.c": "the self-updater (SDL 3); the Wii U does not update itself",
    # system headers newlib does not have
    "port/linux/src/memory_watch.c": "execinfo.h; page protection the Wii U does not have (plan 6.2: a hash watch)",
    "port/linux/src/xbox_memory.c": "sys/mman.h; the Xbox memory window (plan 6.2: waits for the probe)",
    "port/linux/src/posix_net.c": "ifaddrs.h; sockets over nsysnet",
    "port/linux/src/posix_trace_marker.c": "dlfcn.h; a Linux GPU driver's trace markers",
    "port/linux/src/posix_update.c": "struct rlimit (and the self-updater's downloads)",
    # mbedtls's platform modules (its configuration is Linux's)
    "port/third_party/mbedtls/library/entropy_poll.c": "mbedtls: platform entropy sources only on Unix and Windows",
    "port/third_party/mbedtls/library/net_sockets.c": "mbedtls: the sockets module only on Unix and Windows",
    "port/third_party/mbedtls/library/platform_util.c": "mbedtls: no mbedtls_ms_time for this platform",
    "port/third_party/mbedtls/library/timing.c": "mbedtls: the timing module only on Unix and Windows",
}

# Not compiled at all, as a library: internet play's UPnP (port/third_party/miniupnpc) needs the BSD socket headers
# (AF_INET6, SO_RCVTIMEO, struct sockaddr_in6) that newlib does not have; the Wii U's sockets are nsysnet's. Its callers
# in the platform layer (posix_upnp.c) compile and call stubs.
MINIUPNPC_SKIPPED = "port/third_party/miniupnpc"

# the units of port/wiiu/src that are the C library's side of the platform layer, as posix_*.c are (the host's ABI, no
# 16-bit wchar_t, no XDK headers: wut's and newlib's structures have their real layout)
WIIU_NATIVE_PREFIX = "wiiu_"

# what the link needs and the compile does not say: wut's specs and libraries (the environment has WUT_ROOT, as
# the opence-wiiu container does)
WIIU_LINK_FLAGS = ["-g", "-specs=$$WUT_ROOT/share/wut.specs", WIIU_MARCH, "-meabi", "-mhard-float"]
WIIU_LIBS = ["-L$$WUT_ROOT/lib", "-lwut", "-lm"]


def wiiu_flags(flags: Sequence[str]) -> List[str]:
    """flags (LINUX_ABI_FLAGS, POSIX_FLAGS) for the Wii U: the big-endian Linux build's, for the EABI"""
    kept = [flag for flag in flags if flag not in X86_ONLY_FLAGS]
    return WIIU_TARGET_FLAGS + kept if X86_TARGET in flags else kept


def abi_makefile_flags(variable: str = "HALO_WIIU_CODEGEN") -> List[str]:
    """a list of flags in port/wiiu/abi.mk, the flags the probe is built with (the build must have the same)"""
    text = ABI_MAKEFILE.read_text()
    match = re.search(rf"^{variable}\s*:=(.*?)(?<!\\)$", text, re.M | re.S)
    if not match:
        raise ValueError(f"{ABI_MAKEFILE} has no {variable}")
    return match.group(1).replace("\\\n", " ").split()


def find_wiiu_cc(sln: Any) -> Optional[str]:
    """the Wii U compiler (configure.py --wiiu-cc, or wiiu-clang) if it is there"""
    cc = getattr(sln, "wiiu_cc", None) or "wiiu-clang"
    return cc if shutil.which(cc) else None


def _load_port_config() -> Dict[str, Any]:
    with open(PORT_CONFIG, "r", encoding="utf-8") as f:
        return json.load(f)


def wiiu_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py for the Wii U build."""
    return [*linux_configure_inputs(), Path(__file__), ABI_MAKEFILE, WIIU_DIR / "src"]


def _is_excluded(source: Path) -> bool:
    return source.as_posix() in WIIU_EXCLUDE


def generate_wiiu_build(n: Writer, sln: Any) -> None:
    if not PORT_CONFIG.is_file():
        # a checkout without the port (or a test fixture): nothing to emit
        return
    cc = find_wiiu_cc(sln)
    if cc is None:
        n.comment("The Wii U build (ninja linux_wiiu): no Wii U compiler found (wiiu-clang, or configure.py --wiiu-cc);")
        n.comment("it is in the opence-wiiu container (tools/wiiu, in the repository this port is developed in).")
        n.newline()
        return
    config = _load_port_config()
    build_dir: Path = sln.build_dir / BUILD_NAME
    obj_dir = build_dir / "obj"
    elf = build_dir / "halo.elf"
    prefix_header = LINUX_DIR / "include" / "halo_linux_prefix.h"
    semantics_header = build_dir / "halo_msvc_semantics.h"
    platform_semantics_header = build_dir / "platform_msvc_semantics.h"

    n.comment("Wii U build (ninja linux_wiiu)")
    n.variable("wiiu_cc", cc)
    n.variable("wiiu_ld", "powerpc-eabi-gcc")
    n.variable("wiiu_elf2rpl", "elf2rpl")
    n.rule(
        name="wiiu_msvc_semantics",
        command="$python tools/linux_msvc_semantics.py --output $out $scan",
        description="WIIU MSVC SEMANTICS $out",
        restat=True,
    )
    game_headers = sorted(p for p in Path("source").rglob("*") if p.suffix in (".c", ".h"))
    n.build(
        outputs=semantics_header,
        rule="wiiu_msvc_semantics",
        implicit=[Path("tools/linux_msvc_semantics.py"), *xdk_headers(), *game_headers],
        variables={"scan": f"--all-inlines --tags source --inlines source --inlines {XDK_INCLUDE}"},
    )
    n.build(
        outputs=platform_semantics_header,
        rule="wiiu_msvc_semantics",
        implicit=[Path("tools/linux_msvc_semantics.py"), *xdk_headers()],
        variables={"scan": f"--inlines {XDK_INCLUDE}"},
    )
    # (every object is checked as it is made: a forbidden instruction fails the compile of the file that has it)
    n.rule(
        name="wiiu_cc",
        command=f"{compile_launcher(sln)}$wiiu_cc -MMD -MF $out.d $cflags -c $in -o $out"
                " && $python tools/wiiu_check_objects.py --quiet $out",
        description="WIIU CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="wiiu_stubs",
        command='$python tools/wiiu_stubs.py --link "$wiiu_ld $ldflags" --libs "$libs" --objects $out.rsp'
                " --output $out --list $list",
        description="WIIU STUBS $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.rule(
        name="wiiu_link",
        command='test -n "$$WUT_ROOT" || { echo "WUT_ROOT is not set: the Wii U build runs in the opence-wiiu container" >&2; exit 1; }'
                " && $wiiu_ld $ldflags -Wl,-Map,$out.map @$out.rsp $libs -o $out"
                " && $python tools/wiiu_link_check.py $out.rsp $out.map",
        description="WIIU LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.rule(name="wiiu_rpx", command="$wiiu_elf2rpl $in $out", description="WIIU RPX $out")
    # (what Aroma's menu lists with a name; the same program as the RPX)
    n.rule(
        name="wiiu_wuhb",
        command='wuhbtool $in $out --name "OpenCE (Wii U, shell test)" --short-name "OpenCE" --author "OpenCE Wii U port"',
        description="WIIU WUHB $out",
    )
    n.rule(
        name="wiiu_size",
        command="$python tools/wiiu_size_report.py $in --map $map > $out",
        description="WIIU SIZES $out",
    )

    release = getattr(sln, "port_release", False)
    # (no stack protector: newlib has no guard; no -mcpu twice: the compiler adds it)
    # (HALO_WIIU: the Xbox memory window is at the top of MEM2, not at 0x80000000: halo_port_capacity.h)
    abi = " ".join(wiiu_flags(LINUX_ABI_FLAGS) + [WIIU_MARCH, WIIU_TLS, "-DHALO_WIIU"] + configuration_defines(sln))
    port_include = LINUX_DIR / "include"
    sdk_flags = f"-idirafter {XDK_INCLUDE}"
    objects: List[Path] = []

    def add_object(source: Path, cflags: str) -> Path:
        obj = obj_dir / source.with_suffix(".o")
        objects.append(obj)
        n.build(
            outputs=obj,
            rule="wiiu_cc",
            inputs=source,
            # (the SDK declarations are system headers, which the depfile leaves out)
            implicit=[*xdk_headers(), prefix_header, semantics_header, platform_semantics_header],
            variables={"cflags": cflags},
        )
        return obj

    game_cflags = " ".join([
        abi,
        " ".join(GAME_FLAGS),
        f"-include {prefix_header}",
        f"-include {semantics_header}",
        f"-I{port_include}",
        f"-iquote {Path(config['game_sources'])}",
        game_defines_and_includes(config),
        sdk_flags,
    ])
    for source in game_sources(config):
        flags = game_cflags
        if source.as_posix() == "source/main/main.c":
            flags += " " + updater_defines(release)
        if source.as_posix() == "source/shell/shell_xbox.c":
            # (the Wii U's own main, port/wiiu/src/wiiu_main.c, runs the game's on a thread of its own, as the web build's does)
            flags += " -Dmain=halo_game_main"
        add_object(source, flags)
    for source in sorted(Path(config["game_sources"]).glob("*.c")):
        add_object(source, game_cflags)
    add_object(STB_VORBIS_SOURCE, game_cflags)

    platform_dir = Path(config["platform_sources"])
    platform_cflags = " ".join([
        abi,
        " ".join(PLATFORM_FLAGS),
        # (newlib's math.h defines log2 and friends as macros before halo_math.h defines its own)
        "-Wno-macro-redefined",
        f"-include {prefix_header}",
        f"-include {platform_semantics_header}",
        f"-I{platform_dir}",
        f"-I{WIIU_DIR / 'include'}",
        f"-I{port_include}",
        f"-I{TOML_DIR}",
        f"-I{EXPAT_DIR}",
        f"-I{KCP_DIR}",
        f"-I{MONOCYPHER_DIR}",
        f"-I{ZLIB_DIR}",
        "-Isource -Isource/cseries",
        sdk_flags,
    ])
    posix_flags = wiiu_flags(POSIX_FLAGS) + [WIIU_TLS]
    posix_cflags = " ".join(posix_flags + [WIIU_MARCH, f"-I{platform_dir}"])
    mbedtls_include = f"-I{MBEDTLS_DIR / 'include'}"
    for source in sorted(platform_dir.glob("*.c")):
        if _is_excluded(source):
            continue
        if source.name == "posix_dtls.c":
            add_object(source, f"{posix_cflags} {mbedtls_include}")
        elif source.name == "posix_upnp.c":
            add_object(source, f"{posix_cflags} -I{MINIUPNPC_DIR / 'include'} -DMINIUPNP_STATICLIB")
        elif source.name.startswith("posix_"):
            add_object(source, posix_cflags)
        else:
            add_object(source, platform_cflags)
    # the Wii U's own units, as they are written
    wiiu_src = WIIU_DIR / "src"
    for source in sorted(wiiu_src.glob("*.c")):
        if source.name.startswith(WIIU_NATIVE_PREFIX):
            # (-ffp-contract=off: the objects are checked for fused multiply-adds whether or not they touch the simulation)
            add_object(source, f"{posix_cflags} -ffp-contract=off -I{wiiu_src} -I{platform_dir}")
        else:
            # (the platform layer's flags; the Xbox-facing units that include xgpu.h get a stand-in for SDL's OpenGL header)
            add_object(source, f"{platform_cflags} -I{wiiu_src} -I{WIIU_DIR / 'include'}")
    # the high-res text's fonts (the HUD's and the menus' pictures are not in this build): big-endian words, from tools/embed_assets.py --fonts-only
    fonts_source = build_dir / "generated" / "text_hires_fonts.c"
    n.rule(name="wiiu_embed_fonts", command="$python tools/embed_assets.py --fonts-only $out", description="WIIU EMBED $out")
    n.build(outputs=fonts_source, rule="wiiu_embed_fonts", implicit=[Path("tools/embed_assets.py"), Path("port/assets/fonts/fonts.json")])
    add_object(fonts_source, f"{platform_cflags} -I{platform_dir}")
    for source in sorted((MBEDTLS_DIR / "library").glob("*.c")):
        if _is_excluded(source):
            continue
        add_object(source, " ".join(posix_flags + [WIIU_MARCH, mbedtls_include, f"-I{MBEDTLS_DIR / 'library'}",
                                                   "-fno-builtin-wcslen", "-w"]))
    add_object(TOML_DIR / "tomlc17.c", " ".join([abi, "-std=gnu11", "-w"]))
    for name in EXPAT_SOURCES:
        add_object(EXPAT_DIR / name, " ".join([abi, "-std=gnu11", f"-I{EXPAT_DIR}", "-w"]))
    add_object(KCP_DIR / "ikcp.c", " ".join([abi, "-std=gnu11", "-w"]))
    for source in opus_sources():
        add_object(source, opus_cflags(abi))
    for name in ("monocypher.c", "monocypher-ed25519.c"):
        add_object(MONOCYPHER_DIR / name, " ".join([abi, "-std=gnu11", "-w"]))
    for name in ZLIB_SOURCES:
        add_object(ZLIB_DIR / name, " ".join([abi, "-std=gnu11", *ZLIB_DEFINES, "-w"]))
    for source in musl_math_sources():
        add_object(source, musl_math_cflags(abi))

    # what is still undefined, defined as functions that name themselves and stop
    ldflags = " ".join(WIIU_LINK_FLAGS)
    libs = " ".join(WIIU_LIBS)
    stubs_source = build_dir / "wiiu_stubs.c"
    stubs_list = build_dir / "wiiu_stubs.txt"
    n.build(
        outputs=stubs_source,
        rule="wiiu_stubs",
        inputs=objects,
        implicit=[Path("tools/wiiu_stubs.py")],
        implicit_outputs=[stubs_list],
        variables={"ldflags": ldflags, "libs": libs, "list": str(stubs_list)},
    )
    stubs_object = obj_dir / "wiiu_stubs.o"
    n.build(
        outputs=stubs_object,
        rule="wiiu_cc",
        inputs=stubs_source,
        variables={"cflags": f"{WIIU_MARCH} -std=gnu11 -O1 -g -Wall -Werror"},
    )
    n.build(
        outputs=elf,
        rule="wiiu_link",
        inputs=[*objects, stubs_object],
        implicit=[Path("tools/wiiu_link_check.py")],
        implicit_outputs=[Path(f"{elf}.map")],
        variables={"ldflags": ldflags, "libs": libs},
    )
    rpx = build_dir / "halo.rpx"
    n.build(outputs=rpx, rule="wiiu_rpx", inputs=elf)
    n.build(outputs=build_dir / "halo.wuhb", rule="wiiu_wuhb", inputs=rpx)
    sizes = build_dir / "halo.sizes.txt"
    n.build(outputs=sizes, rule="wiiu_size", inputs=elf, implicit=[Path("tools/wiiu_size_report.py")],
            variables={"map": f"{elf}.map"})
    n.build(outputs="linux_wiiu", rule="phony", inputs=[rpx, build_dir / "halo.wuhb", sizes])
    n.newline()
