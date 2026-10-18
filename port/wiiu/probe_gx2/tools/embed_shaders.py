#!/usr/bin/env python3
"""Embeds the probe's shaders in the program: src/shader_glsl.c (GLSL text) and src/shader_gsh.c (GSH files).

  tools/embed_shaders.py --compiler <glslcompiler>   rewrite both (the GSH come from CafeGLSL's command-line compiler)
  tools/embed_shaders.py --check                      do the files on disk match the sources? (needs no compiler)

The GSH files are what CafeGLSL's compiler writes for a pair; the program loads them with the system's loader (WHBGfx)
and so draws without depending on the runtime compiler. shader_gsh.c records the SHA-256 of the sources each pair was
compiled from, so that --check finds a GSH that is older than its GLSL.
"""
import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
SHADERS = HERE / "shaders"
GENERATED = HERE / "generated"

# GSH pairs: name, vertex shader, pixel shader
PAIRS = [
    ("ui", SHADERS / "ui.vert", SHADERS / "ui.frag"),
    ("attr", SHADERS / "attr.vert", SHADERS / "attr.frag"),
    ("packed", SHADERS / "packed.vert", SHADERS / "attr.frag"),
    ("generated", GENERATED / "vs00.vert", GENERATED / "ps_diffuse.frag"),
]

# GLSL text the runtime compiler is given: (name, file, kind) in the order the report lists them
SOURCES = [
    ("attr_vs", SHADERS / "attr.vert", "vertex"),
    ("attr_ps", SHADERS / "attr.frag", "pixel"),
    ("vs00", GENERATED / "vs00.vert", "vertex"),
    ("vs00_packed", GENERATED / "vs00_packed.vert", "vertex"),
    ("vs09", GENERATED / "vs09.vert", "vertex"),
    ("vs09_lit", GENERATED / "vs09_lit.vert", "vertex"),
    ("vs31", GENERATED / "vs31.vert", "vertex"),
    ("vs31_lit", GENERATED / "vs31_lit.vert", "vertex"),
    ("ps_diffuse", GENERATED / "ps_diffuse.frag", "pixel"),
    ("ps_text", GENERATED / "ps_text.frag", "pixel"),
    ("ps_text_fog_alphatest", GENERATED / "ps_text_fog_alphatest.frag", "pixel"),
    ("ps_text_lit_3d", GENERATED / "ps_text_lit_3d.frag", "pixel"),
]

BANNER = "/* Written by tools/embed_shaders.py from shaders/ and generated/. Do not edit. */\n\n"


def c_string(text):
    out = []
    for line in text.splitlines(keepends=True):
        escaped = line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")
        out.append('\t"%s"' % escaped)
    return "\n".join(out) if out else '\t""'


def glsl_c():
    parts = [BANNER, '#include "shader_data.h"\n\n']
    for name, path, _ in SOURCES:
        parts.append("static const char source_%s[] =\n%s;\n\n" % (name, c_string(path.read_text())))
    parts.append("const glsl_source glsl_sources[GLSL_SOURCE_COUNT] =\n{\n")
    for name, _, kind in SOURCES:
        parts.append('\t{ "%s", %s, source_%s },\n' % (name, "GLSL_VERTEX" if kind == "vertex" else "GLSL_PIXEL", name))
    parts.append("};\n")
    return "".join(parts)


def digest(*paths):
    h = hashlib.sha256()
    for p in paths:
        h.update(p.read_bytes())
    return h.hexdigest()


def gsh_c(compiler):
    parts = [BANNER, '#include "shader_data.h"\n\n']
    for name, vs, ps in PAIRS:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / (name + ".gsh")
            result = subprocess.run([compiler, "-vs", str(vs), "-ps", str(ps), "-o", str(out)],
                                    capture_output=True, text=True, check=True)
            data = out.read_bytes()
            mode = result.stdout.strip()
        parts.append("/* %s: %s + %s, sources sha256 %s, compiled for %s */\n" % (name, vs.name, ps.name, digest(vs, ps), mode))
        parts.append("const unsigned char gsh_%s[] __attribute__((aligned(64))) =\n{" % name)
        for i in range(0, len(data), 16):
            parts.append("\n\t" + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",")
        parts.append("\n};\nconst unsigned long gsh_%s_size = sizeof(gsh_%s);\n\n" % (name, name))
    parts.append("const gsh_entry gsh_files[GSH_COUNT] =\n{\n")
    for name, _, _ in PAIRS:
        parts.append('\t{ "%s", gsh_%s, sizeof(gsh_%s) },\n' % (name, name, name))
    parts.append("};\n")
    return "".join(parts)


def check_gsh():
    text = (HERE / "src" / "shader_gsh.c").read_text()
    bad = 0
    for name, vs, ps in PAIRS:
        if "sources sha256 %s" % digest(vs, ps) not in text:
            print("shader_gsh.c: the %s pair is older than its GLSL" % name)
            bad += 1
    return bad


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    glsl_path = HERE / "src" / "shader_glsl.c"
    if args.check:
        bad = 0 if glsl_path.read_text() == glsl_c() else 1
        if bad:
            print("shader_glsl.c is not what the sources give")
        bad += check_gsh()
        sys.exit(1 if bad else 0)
    glsl_path.write_text(glsl_c())
    if args.compiler:
        (HERE / "src" / "shader_gsh.c").write_text(gsh_c(args.compiler))
    else:
        print("(no --compiler: shader_gsh.c left alone)")


main()
