"""The real single-pixel texture readers (s3tc.c, bitmaps.c) read a map's pixels as the Xbox's little-endian values on
any host (s3tc_byte_order.c). Without that a big-endian host decodes lightmaps to the wrong colours."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, big_endian, build, enum_with, function, read, run  # noqa: E402

CASES = ["dxt1-colors", "dxt3-alpha", "pixels"]


def port_block(text, start):
    """a '#ifdef __BIG_ENDIAN__ ... #endif' block of macros, from its comment"""
    begin = text.index(start)
    return text[begin:text.index("#endif", begin) + len("#endif")]


def under_test(fault=False):
    header = read("source/bitmaps/s3tc/s3tc.h")
    decoders = read("source/bitmaps/s3tc/s3tc.c")
    bitmaps = read("source/bitmaps/bitmaps.c")
    macros = port_block(header, "/* port: a block is in the Xbox's byte order")
    pixels = port_block(bitmaps, "/* port: a map's pixels are in the Xbox's byte order")
    if fault:
        # (the host's own order, as the first big-endian build read them)
        macros = macros.replace("#ifdef __BIG_ENDIAN__", "#if 0")
        pixels = pixels.replace("#ifdef __BIG_ENDIAN__", "#if 0")
    structures = "".join(text + "\n" for text in (
        enum_with(header, "S3TC_ALPHA"),
        header[header.index("struct s3tc_color\n"):header.index("typedef char s3tc_color_size_assert")]))
    formats = enum_with(bitmaps, "_bitmap_format_r5g6b5")
    code = "".join(function(decoders, name) + "\n" for name in (
        "RGBToColor", "DecodeBlockRGB__single_pixel", "DecodeBlockAlpha4__single_pixel"))
    code += function(bitmaps, "bitmap_format_to_a8r8g8b8") + "\n"
    return (("under_test.inc", f"{structures}\n{formats}\n{macros}\n{pixels}\n{code}"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("s3tc_byte_order", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("case", CASES)
def test_negative_control(case):
    # (the host's order is the Xbox's on a little-endian host: the fault changes nothing there)
    if not big_endian():
        pytest.skip("only a big-endian host reads the bytes in the wrong order")
    status, output = run(build("s3tc_byte_order", under_test(fault=True)), case)
    assert status == CHECK_FAILED, f"reading the pixels in the host's order passed '{case}': the test cannot see it"
