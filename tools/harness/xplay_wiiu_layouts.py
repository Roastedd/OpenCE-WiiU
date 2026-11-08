#!/usr/bin/env python3
"""The wire structures' sizes and every scalar's offset (tests/xplay_layouts_*.inc) against the declarations as the Wii U's own compiler
(wiiu-clang, powerpc-eabi, in the opence-wiiu image) lays them out, with the flags the Wii U build compiles each unit with.

    docker run --rm -v "$PWD:/src" -w /src opence-wiiu python3 tools/harness/xplay_wiiu_layouts.py

The tests of the wire (test_xplay_*.py) run a Linux PowerPC stand-in under qemu-ppc, which cannot run the Wii U's executable; this is the
part of them the Wii U's toolchain can answer without a console: that the layouts the tests used are the layouts it builds. It restores
the big-endian loop's build.ninja when done. Exit status: the number of units that differ."""

import os
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import xplay_layouts  # noqa: E402


def main():
    subprocess.run(["python3", "configure.py", "--linux-target=wiiu"], check=True, stdout=subprocess.DEVNULL)
    try:
        subprocess.run(["ninja", "build/linux-wiiu/halo_msvc_semantics.h", "build/linux-wiiu/platform_msvc_semantics.h"], check=True,
                       stdout=subprocess.DEVNULL)
        bad = 0
        for name, (source, _) in xplay_layouts.UNITS.items():
            obj = f"build/linux-wiiu/obj/{source[:-2]}.o"
            out = subprocess.run(["ninja", "-t", "commands", obj], capture_output=True, text=True)
            lines = [line for line in out.stdout.splitlines() if "wiiu-clang" in line]
            if out.returncode or not lines:
                print(name, "no compile command:", out.stderr[:200])
                bad += 1
                continue
            words = shlex.split(lines[-1])
            if "&&" in words:
                words = words[:words.index("&&")]
            command, skip = [], False
            for word in words:
                if skip:
                    skip = False
                    continue
                if word in ("-MF", "-o"):
                    skip = True
                    continue
                if word in ("-MMD", "-c") or word == source or word.startswith(("-fprofile", "-flto")):
                    continue
                command.append(word)
            command += ["-fsyntax-only", "-Itools/harness/tests", f'-DXPLAY_MODULE_SOURCE="{os.path.basename(source)}"',
                        f'-DXPLAY_MODULE_INC="xplay_layouts_{name}.inc"', "tools/harness/tests/xplay_layout_check.c"]
            result = subprocess.run(command, capture_output=True, text=True)
            print(name, "ok" if result.returncode == 0 else "DIFFERS\n" + result.stderr[-800:])
            bad += result.returncode != 0
        return bad
    finally:
        subprocess.run(["python3", "configure.py", "--linux-target=ppc", "--lto=off", "--pgo=off"], stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    sys.exit(main())
