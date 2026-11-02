#!/usr/bin/env python3
"""The Broadway kernels' C reference (port/wiiu/src/wiiu_bw_reference.h) holds word for word the bodies of three of the game's
functions it stands for; the kernels are proven equal to the reference, so the reference must stay equal to the game. Fails if a
body there and the game's differ.

    python3 -I tools/test_bw_reference_sync.py          (also collected by pytest)
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "port/wiiu/src/wiiu_bw_reference.h"

# (the game's file, the start of the function's signature there, the reference's name)
FUNCTIONS = [
    ("source/math/matrix_math.c", "void matrix4x3_multiply(", "halo_bw_ref_matrix4x3_multiply("),
    ("source/math/real_math.c", "boolean fast_vector_intersects_sphere(", "halo_bw_ref_fast_vector_intersects_sphere("),
    ("source/models/model_animations.c", "void quaternion_decompress_8byte(", "halo_bw_ref_quaternion_decompress_8byte("),
]


def without_kernel_code(text: str) -> str:
    """the text without the blocks the kernels' call sites are in (#ifdef HALO_BW_KERNELS to its #endif, which do not nest)"""
    out, inside = [], False
    for line in text.split("\n"):
        if line.strip() == "#ifdef HALO_BW_KERNELS":
            inside = True
        elif inside and line.strip() == "#endif":
            inside = False
        elif not inside:
            out.append(line)
    return "\n".join(out)


def body(text: str, signature: str) -> str:
    """the function's braces and what is between them (it starts at the signature, the braces at the start of a line), without the
    kernels' call sites"""
    start = text.index(signature)
    open_brace = text.index("\n{\n", start)
    close_brace = text.index("\n}\n", open_brace)
    return without_kernel_code(text[open_brace + 1:close_brace + 3])


def mismatches():
    header = HEADER.read_text()
    for path, signature, reference in FUNCTIONS:
        game = body((ROOT / path).read_text(), signature)
        mine = body(header, reference)
        if game != mine:
            yield path, signature


def test_the_reference_is_the_games_code():
    assert list(mismatches()) == []


if __name__ == "__main__":
    bad = list(mismatches())
    for path, signature in bad:
        print(f"{path}: {signature.strip()} differs from the reference copy in {HEADER.name}")
    print("reference copies: " + ("DIFFERENT" if bad else "the same as the game's code"))
    sys.exit(1 if bad else 0)
