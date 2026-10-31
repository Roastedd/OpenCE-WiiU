#!/usr/bin/env python3
"""Compile the independently written mip pair and embed it for the GX2 unit.

Uses the existing CafeGLSL command-line compiler. No runtime compiler or game
shader pack is required for this fixed helper.
"""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    vertex, pixel = root / 'shaders/mip.vert', root / 'shaders/mip.frag'
    with tempfile.TemporaryDirectory(prefix='wiiu-mip-') as work:
        compiled = Path(work) / 'mip.gsh'
        command = [str(args.compiler.resolve()), '--uniform-mode', 'register',
                   '-vs', str(vertex), '-ps', str(pixel), '-o', str(compiled)]
        subprocess.run(command, check=True)
        data = compiled.read_bytes()
    if data[:4] != b'Gfx2':
        raise ValueError('Compiler did not produce a GFD file')
    target = root / 'src/wiiu_gx2_mip_gsh.inc'
    target.write_text('/* CafeGLSL register-mode GFD for shaders/mip.vert and mip.frag. */\n' +
                      '\n'.join('\t' + ', '.join('0x%02x' % b for b in data[i:i + 16]) + ','
                                for i in range(0, len(data), 16)) + '\n')
    identity = {'uniform_mode': 'register', 'gfd_size': len(data),
                'gfd_sha256': hashlib.sha256(data).hexdigest(),
                'source_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in (vertex, pixel)}}
    (root / 'shaders/mip-identity.json').write_text(json.dumps(identity, indent=2) + '\n')
    print(json.dumps(identity, indent=2))


if __name__ == '__main__':
    main()
