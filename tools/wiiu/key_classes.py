#!/usr/bin/env python3
"""Which shader keys are new shaders: the pixel keys of files grouped by canonical key (shader_key_wire.h's shader_key_canonical), against a key list.

    tools/wiiu/key_classes.py NEEDED.txt [NEEDED2.txt ...] [--have KEYS.txt] [-o NEW.txt] [--classes CLASSES.txt]

Two pixel keys with the same canonical key have the same GLSL (the shader pack generator checks it, and writes the shader of a key under its canonical key's hash
as well), so a game run needs a new shader only for a key whose canonical key the list has none of; any other key it lacks is answered by the canonical key's
shader (port/wiiu/src/gx2_shaders.c). This tool says, for the pixel keys of NEEDED (the lines of the missing-shader capture or the digest harness's --keys):
how many there are, how many classes, and how many of both KEYS (the list shaders.pack is made from) lacks. -o writes the lacked keys (as key_diff.py does), --classes the
canonical keys of the NEEDED classes the list has no class for.

The canonical key is made by the game's own header, compiled here with the C compiler (CC, else cc) and a stand-in for xgpu.h as the fallback test has. Vertex keys have no
classes (a vertex shader is its program and its mask): they are counted and the lacked ones written, as key_diff.py does. Nothing here touches a console.

Exit status: 0 when no class is new, 1 when one is, 2 for a file that cannot be read or a tool that cannot be built.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import key_diff  # noqa: E402

WIRE_BYTES = 256

MAIN = r"""
#include "xgpu.h"
#include "shader_key_wire.h"
#include <stdio.h>
#include <string.h>

/* "P <512 hex>" lines in, the canonical key's 512 hex digits a line out */
int main(void)
{
	char line[2048];

	while (fgets(line, sizeof line, stdin))
	{
		unsigned char wire[SHADER_KEY_WIRE_BYTES], out[SHADER_KEY_WIRE_BYTES];
		struct nv2a_pixel_shader_key key, canonical;
		size_t i;

		if (line[0] != 'P' || strlen(line) < 3 + 2 * SHADER_KEY_WIRE_BYTES)
			continue;
		for (i = 0; i < SHADER_KEY_WIRE_BYTES; i++)
		{
			unsigned value;

			sscanf(line + 2 + 2 * i, "%2x", &value);
			wire[i] = (unsigned char)value;
		}
		shader_key_read(wire, &key);
		shader_key_canonical(&key, &canonical);
		shader_key_write(&canonical, out);
		for (i = 0; i < SHADER_KEY_WIRE_BYTES; i++)
			printf("%02x", out[i]);
		printf("\n");
	}
	return 0;
}
"""


def canonical_tool(folder: Path) -> Path:
    """the program that writes canonical keys, built in folder from the game's header"""
    from harness import enum_with, structure

    pdb = (ROOT / "port/include/xdk/xdk_pdb.h").read_text(encoding="latin-1")
    xgpu = (ROOT / "port/linux/src/xgpu.h").read_text()
    (folder / "xgpu.h").write_text(
        "#ifndef KEY_CLASSES_XGPU_H\n#define KEY_CLASSES_XGPU_H\n#include <stdint.h>\ntypedef uint32_t DWORD;\n" +
        enum_with(pdb, "D3DRS_PSALPHAINPUTS0") + "\n" + enum_with(pdb, "D3DCMP_NEVER") + "\n" + enum_with(pdb, "D3DFOG_NONE") + "\n" +
        enum_with(xgpu, "_xgpu_sampler_none") + "\n" + structure(xgpu, "nv2a_pixel_shader_key") + "\n#endif\n")
    (folder / "main.c").write_text(MAIN)
    program = folder / "canonical"
    result = subprocess.run([os.environ.get("CC", "cc"), "-O2", "-std=gnu11", "-I", str(folder), "-I", str(ROOT / "port/wiiu/src"), str(folder / "main.c"),
                             "-o", str(program)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("the canonical key tool does not build:\n" + result.stdout + result.stderr)
    return program


def canonical_keys(pixel_keys):
    """{key hex: canonical key hex} for pixel keys (512 hex digits each)"""
    pixel_keys = sorted(pixel_keys)
    if not pixel_keys:
        return {}
    with tempfile.TemporaryDirectory(prefix="opence-key-classes-") as folder:
        program = canonical_tool(Path(folder))
        result = subprocess.run([str(program)], input="".join("P %s\n" % key for key in pixel_keys), capture_output=True, text=True)
    lines = result.stdout.split()
    if result.returncode or len(lines) != len(pixel_keys):
        raise RuntimeError("the canonical key tool answered %d of %d keys" % (len(lines), len(pixel_keys)))
    return dict(zip(pixel_keys, lines))


def read_all(paths):
    """({pixel key hex: line}, {vertex identity: line}) of files; ValueError names a broken line"""
    pixel, vertex = {}, {}
    for path in paths:
        keys, broken, problem = key_diff.read_keys(path)
        if broken:
            raise ValueError(problem)
        for (kind, identity), line in keys.items():
            (pixel if kind == "P" else vertex)[identity if kind == "P" else identity] = line
    return pixel, vertex


def analyse(needed_pixel, have_pixel):
    """(canonical map of needed's keys, the keys the list lacks, the keys of classes the list lacks, those classes)"""
    classes = canonical_keys(set(needed_pixel) | set(have_pixel))
    have_classes = {classes[key] for key in have_pixel}
    lacked = sorted(key for key in needed_pixel if key not in have_pixel)
    new_keys = [key for key in lacked if classes[key] not in have_classes]
    new_classes = sorted({classes[key] for key in new_keys})
    return classes, lacked, new_keys, new_classes


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], epilog=__doc__.split("\n\n", 1)[1],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("needed", nargs="+")
    parser.add_argument("--have", default=None, help="the key list to compare with (default: none, so every class is new)")
    parser.add_argument("-o", "--output", help="write the keys the list lacks here")
    parser.add_argument("--classes", help="write the canonical keys of the classes the list has none of here")
    args = parser.parse_args(argv)
    try:
        needed_pixel, needed_vertex = read_all(args.needed)
        have_pixel, have_vertex = read_all([args.have]) if args.have else ({}, {})
        classes, lacked, new_keys, new_classes = analyse(needed_pixel, have_pixel)
    except (OSError, ValueError, RuntimeError) as error:
        print("key_classes: %s" % error, file=sys.stderr)
        return 2
    needed_classes = {classes[key] for key in needed_pixel}
    have_classes = {classes[key] for key in have_pixel}
    lacked_vertex = [identity for identity in sorted(needed_vertex, key=str) if identity not in have_vertex]
    print("needed: %d pixel keys in %d classes, %d vertex keys; the list has %d pixel keys in %d classes and %d vertex keys" % (
        len(needed_pixel), len(needed_classes), len(needed_vertex), len(have_pixel), len(have_classes), len(have_vertex)))
    print("the list lacks: %d pixel keys (%d of them in classes it has, %d in %d new classes), %d vertex keys" % (
        len(lacked), len(lacked) - len(new_keys), len(new_keys), len(new_classes), len(lacked_vertex)))
    if args.output:
        with open(args.output, "w", encoding="ascii") as handle:
            handle.write("".join("P %s\n" % key for key in lacked))
            handle.write("".join(needed_vertex[identity] + "\n" for identity in lacked_vertex))
    if args.classes:
        with open(args.classes, "w", encoding="ascii") as handle:
            handle.write("".join("P %s\n" % key for key in new_classes))
    return 1 if new_classes else 0


if __name__ == "__main__":
    sys.exit(main())
