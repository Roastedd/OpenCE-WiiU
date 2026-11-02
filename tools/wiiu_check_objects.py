#!/usr/bin/env python3
"""The instructions the Wii U build's objects must not contain (tools/wiiu_build.py runs this on each one).

Disassembles each object with devkitPPC's objdump and reports any instruction from:

  fused multiply-adds (fmadd, fmsub, fnmadd, fnmsub and their single-precision forms): they round once where x86
      rounds twice, and every machine of a network game must simulate the same floating-point results (the
      compiler's -ffp-contract=off keeps them out; clang is known to ignore it under #pragma optimize("", off),
      which is why source/ai/ai.c has its own pragma);
  fsqrt, fsqrts: Espresso is a 750-class core and does not have them (an illegal instruction);
  paired singles (ps_*, psq_*): not what any of this code means, and not in the Linux builds it is compared with.

Objects allowed to contain them (one that calls fma() on purpose) are named with --allow. Exit status 1 when
anything else has one.

The audio assembly object has an explicit exact-path --audio-ps-object route
to a stricter complete instruction/ELF contract. It does not use --allow or
weaken the PS ban for other objects.

The Broadway kernels' objects (port/wiiu/src/bw, tools/wiiu_build.py's wiiu_bw_asm) are named with --bw-kernel-object PATH, and held to
an allowlist instead: only the instructions BW_ALLOWED lists, which have no fused multiply-add (fmadd*, ps_madd*, ps_msub*,
ps_nmadd*...), no estimate (frsqrte, fres, ps_res, ps_rsqrte), no division or root, no control register or SPR written and only the
quantization registers' user aliases (896 to 903) read. Anything else fails the object. Like the audio route it does not use --allow
and does not weaken the ban for other objects.

    wiiu_check_objects.py [--quiet] [--allow NAME]... [--audio-ps-object PATH] [--bw-kernel-object PATH]... OBJECT_OR_FOLDER...
"""

import os
import re
import struct
import subprocess
import sys
from pathlib import Path

FORBIDDEN = re.compile(r"^(fn?m(add|sub)s?|fsqrts?|psq?_\w+)\.?$")
OBJDUMP = os.environ.get("WIIU_OBJDUMP", "powerpc-eabi-objdump")

# What the Broadway kernels' objects may contain (a branch's static prediction hint, - or +, is not part of its name). The paired-single
# arithmetic here is the exact one: add, subtract and multiply (by a lane or not), the lane sum, merges and moves, and the loads and
# stores; nothing that fuses or estimates. mfspr is allowed only to read 896 to 903 (the quantization registers' user aliases).
BW_ALLOWED = frozenset("""
add addi b beq bge bgt ble blr blt bne bso bns cmplw cmplwi cmpw cmpwi cror extsh fadds fcmpu fmuls fneg fsubs lfs lhz li lis lwz mfspr
mr mulli ps_add ps_merge00 ps_merge01 ps_merge10 ps_merge11 ps_mr ps_mul ps_muls0 ps_muls1 ps_sub ps_sum0 ps_sum1 psq_l psq_st slwi srwi
stfs stw
""".split())


def objects(paths):
    for path in map(Path, paths):
        if not path.exists():
            sys.exit(f"{path}: no such file or folder")
        if path.is_dir():
            found = sorted(path.rglob("*.o"))
            if not found:
                sys.exit(f"{path}: no objects in it")
            yield from found
        else:
            yield path


def forbidden_in(path):
    # Several paired encodings overlap later POWER instructions. Without an
    # explicit CPU, objdump can call ps_add 'vsel' and psq_l 'lq', hiding them.
    listing = subprocess.run([OBJDUMP, "-d", "-M", "broadway", "--no-show-raw-insn", str(path)], capture_output=True, text=True,
                             check=True).stdout
    found = {}
    for line in listing.splitlines():
        # "  1c:\tfmadd   f1,f2,f3,f4"
        match = re.match(r"\s*[0-9a-f]+:\s+(\S+)", line)
        if match and FORBIDDEN.match(match.group(1)):
            found[match.group(1)] = found.get(match.group(1), 0) + 1
    return found


def bw_violations(listing):
    """the instructions of an objdump listing that a Broadway kernel object may not have, as {name: count}"""
    found = {}
    for line in listing.splitlines():
        # "  1c:\tfmadd   f1,f2,f3,f4"
        match = re.match(r"\s*[0-9a-f]+:\s+(\S+)\s*(\S*)", line)
        if not match:
            continue
        name = match.group(1).rstrip("+-")
        if name == "mfspr":
            operands = match.group(2).split(",")
            if len(operands) != 2 or not operands[1].isdigit() or not 896 <= int(operands[1]) <= 903:
                found["mfspr " + match.group(2)] = found.get("mfspr " + match.group(2), 0) + 1
        elif name not in BW_ALLOWED:
            found[name] = found.get(name, 0) + 1
    return found


def check_bw_kernel(path):
    listing = subprocess.run([OBJDUMP, "-d", "-M", "broadway", "--no-show-raw-insn", str(path)], capture_output=True, text=True,
                             check=True).stdout
    if "file format elf32-powerpc" not in listing:
        return {"not a PowerPC ELF object": 1}
    return bw_violations(listing)


def main():
    arguments = sys.argv[1:]
    allowed = set()
    audio_objects = set()
    bw_objects = set()
    quiet = False
    while arguments and arguments[0] in ("--allow", "--quiet", "--audio-ps-object", "--bw-kernel-object"):
        if arguments[0] != "--quiet" and len(arguments) < 2:
            print(f"{arguments[0]} needs an argument", file=sys.stderr)
            return 2
        if arguments[0] == "--quiet":
            quiet = True
            arguments = arguments[1:]
        elif arguments[0] == "--allow":
            allowed.add(arguments[1])
            arguments = arguments[2:]
        elif arguments[0] == "--bw-kernel-object":
            bw_objects.add(Path(arguments[1]).resolve())
            arguments = arguments[2:]
        else:
            audio_objects.add(Path(arguments[1]).resolve())
            arguments = arguments[2:]
    if not arguments:
        print(__doc__)
        return 2
    checked = 0
    bad = 0
    audited_audio = set()
    audited_bw = set()
    for path in objects(arguments):
        checked += 1
        if path.resolve() in bw_objects:
            audited_bw.add(path.resolve())
            found = check_bw_kernel(path)
            if found:
                bad += 1
                print(f"{path}: not allowed in a Broadway kernel: " + ", ".join(f"{name} x{count}" for name, count in sorted(found.items())))
            elif not quiet:
                print(f"{path}: Broadway kernel, only allowed instructions")
            continue
        if path.resolve() in audio_objects:
            audited_audio.add(path.resolve())
            try:
                try:
                    from .wiiu_resampler_ps_audit import audit
                except ImportError:
                    from wiiu_resampler_ps_audit import audit
                result = audit(path, OBJDUMP)
            except (OSError, ValueError, UnicodeDecodeError, struct.error, subprocess.CalledProcessError) as error:
                bad += 1
                print(f"{path}: strict audio PS audit failed: {error}")
            else:
                if not quiet:
                    print(f"{path}: exact audio PS contract ({result})")
            continue
        found = forbidden_in(path)
        if found and path.name not in allowed:
            bad += 1
            print(f"{path}: " + ", ".join(f"{name} x{count}" for name, count in sorted(found.items())))
        elif found and not quiet:
            print(f"{path}: allowed ({', '.join(f'{name} x{count}' for name, count in sorted(found.items()))})")
    if bw_objects - audited_bw:
        bad += 1
        print("A Broadway kernel object was named but not checked")
    if audio_objects - audited_audio:
        bad += 1
        print("Audio PS object contract was not satisfied for every requested exact path")
    if not quiet or bad:
        print(f"{checked} objects checked, {bad} with forbidden instructions")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
