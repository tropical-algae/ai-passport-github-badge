#!/usr/bin/env python3
"""Compose the seven synthetic native-rendered badge pages into a README PNG."""
import argparse
import struct
import zlib
from pathlib import Path


def compose(source: Path, output: Path) -> None:
    page_width, page_height, gap = 240, 320, 24
    width, height = page_width * 4 + gap * 5, page_height * 2 + gap * 3
    canvas = bytearray(width * height * 3)
    for page in range(7):
        header, size, maximum, pixels = (source / f"page-{page}.ppm").read_bytes().split(b"\n", 3)
        if (header, size, maximum) != (b"P6", b"240 320", b"255") or len(pixels) != page_width * page_height * 3:
            raise ValueError(f"page-{page}.ppm is not a native 240 x 320 RGB preview")
        row, column = divmod(page, 4)
        left = gap if row == 0 else (width - (page_width * 3 + gap * 2)) // 2
        x, y = left + column * (page_width + gap), gap + row * (page_height + gap)
        for scanline in range(page_height):
            target = ((y + scanline) * width + x) * 3
            start = scanline * page_width * 3
            canvas[target:target + page_width * 3] = pixels[start:start + page_width * 3]

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    scanlines = b"".join(b"\0" + canvas[y * width * 3:(y + 1) * width * 3] for y in range(height))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"\x89PNG\r\n\x1a\n" +
                       chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                       chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Native renderer's synthetic avatar/ directory")
    parser.add_argument("output", type=Path, help="Destination PNG")
    args = parser.parse_args()
    compose(args.source, args.output)
