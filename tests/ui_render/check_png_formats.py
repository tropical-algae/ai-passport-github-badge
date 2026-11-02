#!/usr/bin/env python3
"""Check native avatar pixels against equivalent RGBA PNGs and reject unsafe modes."""
import json
import subprocess
import sys
from pathlib import Path


def main():
    binary, fixtures, output = (Path(arg).resolve() for arg in sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)

    def render(filename, reject=False):
        destination = output / Path(filename).stem
        destination.mkdir(exist_ok=True)
        with (destination / 'render.log').open('wb') as log:
            subprocess.run([str(binary), str(fixtures / filename),
                            'reject' if reject else 'accept', 'avatar-only'],
                           cwd=destination, stdout=log, stderr=subprocess.STDOUT, check=True)
        return None if reject else (destination / 'avatar-owned-pixels.ppm').read_bytes()

    manifest = json.loads((fixtures / 'png-formats.json').read_text())
    for entry in manifest:
        pixels = render(entry['file'], entry['reject'])
        if not entry['reject'] and pixels != render(entry['reference']):
            raise AssertionError(f"PNG conversion changed rendered pixels: {entry['file']}")
        print(f"PNG {'rejected safely' if entry['reject'] else 'pixels verified'}: {entry['file']}", flush=True)
    print(f'PNG format matrix passed: {len(manifest)} variants')


if __name__ == '__main__':
    main()
