#!/usr/bin/env python3
"""Deterministic synthetic avatars; no network or image library required."""
import base64
import json
import random
import struct
import sys
import zlib
from pathlib import Path


def png(path, side, pixels):
    def chunk(kind, value):
        return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind + value))
    scan = b''.join(b'\0' + bytes(pixels[y * side * 4:(y + 1) * side * 4]) for y in range(side))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', side, side, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scan)) + chunk(b'IEND', b''))


def png_format_matrix(target):
    """Equivalent PNG encodings exercise LVGL conversion, not just signatures."""
    def chunk(kind, value):
        return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind + value))

    def pack(samples, depth):
        if depth == 16:
            return b''.join(struct.pack('>H', value * 257) for value in samples)
        if depth == 8:
            return bytes(samples)
        result = bytearray((len(samples) * depth + 7) // 8)
        for i, value in enumerate(samples):
            result[i * depth // 8] |= value << (8 - depth - i * depth % 8)
        return bytes(result)

    cases = [('rgb8', 2, 8, False), ('rgb8-key', 2, 8, False),
             ('palette2-alpha', 3, 2, False), ('gray-alpha8', 4, 8, False),
             ('rgb8-adam7', 2, 8, True), ('rgba8-adam7', 6, 8, True),
             ('palette2-adam7', 3, 2, True),
             ('rgb16', 2, 16, False), ('rgba16', 6, 16, False)]
    cases += [(f'gray{depth}', 0, depth, False) for depth in (1, 2, 4, 8)]
    manifest = []
    side = 54
    palette = [(46, 228, 164, 255), (22, 58, 43, 128)]
    for name, color, depth, interlaced in cases:
        samples, rgba = [], []
        for y in range(side):
            for x in range(side):
                index = (x // 6 + y // 6) % 2
                alpha = (0, 128, 255)[(x + y) % 3]
                if color in (0, 4):
                    grey = index * 255
                    pixel = (grey, grey, grey, alpha if color == 4 else 255)
                    sample = [index * ((1 << depth) - 1)] if depth < 8 else [grey]
                    if color == 4:
                        sample += [alpha]
                elif color == 3:
                    pixel, sample = palette[index], [index]
                else:
                    pixel = palette[index][:3] + ((alpha if color == 6 else 0 if name.endswith('-key') and index else 255),)
                    sample = list(pixel[:4 if color == 6 else 3])
                samples.append(sample)
                rgba.extend(pixel)
        passes = [(0, 0, 1, 1)] if not interlaced else [
            (0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
            (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
        scan = bytearray()
        for sx, sy, dx, dy in passes:
            for y in range(sy, side, dy):
                scan += b'\0' + pack([v for x in range(sx, side, dx) for v in samples[y * side + x]], depth)
        extra = b''
        if color == 3:
            extra = chunk(b'PLTE', bytes(v for p in palette for v in p[:3]))
            extra += chunk(b'tRNS', bytes(p[3] for p in palette))
        elif name.endswith('-key'):
            extra = chunk(b'tRNS', b''.join(struct.pack('>H', v) for v in palette[1][:3]))
        encoded = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', side, side, depth, color, 0, 0, int(interlaced)))
        encoded += extra + chunk(b'IDAT', zlib.compress(scan)) + chunk(b'IEND', b'')
        filename = 'format-' + name + '.png'
        (target / filename).write_bytes(encoded)
        entry = {'file': filename, 'reject': depth == 16}
        if depth != 16:
            entry['reference'] = 'reference-' + name + '.png'
            png(target / entry['reference'], side, rgba)
        manifest.append(entry)
    (target / 'png-formats.json').write_text(json.dumps(manifest, indent=2) + '\n')


def main():
    target = Path(sys.argv[1])
    target.mkdir(parents=True, exist_ok=True)
    cells = {(1,0),(2,0),(3,0),(0,1),(2,1),(4,1),(0,2),(1,2),(3,2),(4,2),(1,3),(3,3),(0,4),(1,4),(2,4),(3,4),(4,4)}
    pixels = []
    for y in range(54):
        for x in range(54):
            active = 4 <= x < 49 and 4 <= y < 49 and ((x-4)//9, (y-4)//9) in cells
            pixels.extend((46,228,164,255) if active else (22,58,43,255))
    png(target/'avatar.png',54,pixels)
    rng = random.Random(823)
    png(target/'avatar64.png',64,[rng.randrange(256) for _ in range(64*64*4)])
    # Baseline/progressive JPEG fixtures generated from the same synthetic icon.
    (target/'avatar64.jpg').write_bytes(base64.b64decode(BASELINE))
    (target/'avatar-progressive.jpg').write_bytes(base64.b64decode(PROGRESSIVE))
    (target/'avatar-corrupt.png').write_bytes((target/'avatar.png').read_bytes()[:33])
    png_format_matrix(target)

BASELINE = "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"
PROGRESSIVE = "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wgARCABAAEADASIAAhEBAxEB/8QAHAABAAIDAQEBAAAAAAAAAAAABwAGAwQIBQIB/8QAGgEBAAMAAwAAAAAAAAAAAAAAAAIFBgEDBP/aAAwDAQACEAMQAAABEcs340lu/FbOwhFjX9BPnKbWBur+qlK6wXLzzKOtbCVX+4uLSDO4Q8HxfSD0Gj6MCHgPZvfWytPddHOdDA1l/YObvZVLt9A8VzvAiDwDzwFr/8QAIxAAAAUEAgMBAQAAAAAAAAAAAQIDBAUABhEWNDUQEyQhMf/aAAgBAQABBQKkyexTS3VaW6rS3VKWe5ST8ETOoLZssVyLtDJTFOB1CJg5combA0XwYpiDZfPdj8gAGLX6O9OA0D6xEc3R3ll885QOTV42pOTcwr2GXPcS5LajiHq5+8gJVOJcp3g2UUq5+8svnqH9ae6Nal3pZGQImdQWrVcHQ/25W6p5qz0VE3zr9ag0XwYpiDZfPyPjNZ8ZGro7xlILxymzyVbPJVs8lWzyVbPJVs8lTp0o9X//xAApEQABAgQDCAMBAAAAAAAAAAABAAQCAwUhMVFxERIVYaHB0fATFDSR/9oACAEDAQE/AY4xCDdcXejE9FBVnkRHhbwOCq9npOicOIaxD8EixF7+nNS3cFMl/VnXi5YXVC/SdO4Tpi2nxGZMx1VDIDg7cu4VW2RPf4mzJs2j35OOqf0uc6nmbARs95LgTnMdfC4E5zHXwqbTZrOcZkZGC//EABQRAQAAAAAAAAAAAAAAAAAAAED/2gAIAQIBAT8BB//EADMQAAEDAQMJBwMFAAAAAAAAAAEAAgMRBCE0EBIxUXOCorHBEyIzQWKSoTJCcQUgI3KB/9oACAEBAAY/Akxg0uICxEPysRD8rEQ/Ke8zw0aK+eWjGOefSKqEmGQAPbeWHWvHi94VWkOGsFVe9rB6jRTATRklhuDxqXgS+wqjgWnUQrRsuqn2buSFys+9zVn2vRQbRvNG9Wjd5BWjZdU5p0EUKw595UljscnZWeOma2gOkV81JD+oHt44257R9N/+JrhBeDUd85LRu8gpJJGOeHMze7+Uxggmq4geWS0bvIK0bLqnvOhoJWHm+FJaGNLWupc78KjGOefSKqH+GT62/YdeSctie4d28NOpTl8b2Ds/ubTzU39Hcl4EvsKo4Fp1EK0bLr+7SrRu8gnPs78xzhQ3VWI4AsRwBYjgCxHAFiOALEcATppnZ0jtJov/xAAjEAEAAgEDBAMBAQAAAAAAAAABABFhITHwQVFxoRCB4bHR/9oACAEBAAE/IY7gAq5aiTXH9Tg/5nB/zHoksO0vtBsH4dAGqKvUetUgAUxLbvctYbe9rRDSBoI/1DrViCtsyi7XLSOznekxpVp+MTdf0TbTaaPJ/WEbtf1gbJ+Cb6bxX4IuJjBTtwYZTw/cAhaLVoWtndZo9VpouF3ToxM4ZOqfcW2epFQu0K2NHrD4gq97XeJSk9SLiYwzCrQwXBi+P7h2NpVZQOniOgDVFXqOaYMr2GJv+Ym/lFjbH0Cop0e8KMLUgPKVXY5aR2c70mNKtPxmZ+BHVindv4zIr8EWtEyrYu+sxueJjc8TG54mNzxMbniY3PE0IE0RdFGhP//aAAwDAQACAAMAAAAQnb6Xb6LyCiyHLjjX/8QAIREBAAICAQQDAQAAAAAAAAAAAQARIUFRMXGx0WGB8cH/2gAIAQMBAT8QwAEIgsD6epkgReHqCqQxDGjwIBNVxlFmlXY1zmMyQtujsMqPfEFd3A5+n4dDGIpqs4WBdieBFAZFbYsf4Q2BAZW8FaU/Th+nBYQUYW7sdhxP/8QAFBEBAAAAAAAAAAAAAAAAAAAAQP/aAAgBAgEBPxAH/8QAIRABAAICAgICAwAAAAAAAAAAAQARITFRYRBBofEgcdH/2gAIAQEAAT8QmjItAkF6zFyuGtR+sj9ZEQi8WREHLE7sL8AiSjA5QNEMyhgSKrQAyrGOKKyRWtapnrDSNMRElGF4FFscyjgQABZVwBDGi0xFS162O8ljkjKlV0xeSw+2XQB6IQIAayZKAX2ROwSemXYB7Yi1V3sviY5MXJVgiWdLAwFRgnFMiavAZLVlxcANSLeqlOQpazLUXk6QRpzyERF258XrXJr2AbBVEVItFBILwzOrGvF74mOzIlCMg7xD20XuIK/dlTdk3AIkowOUDRAH9BgFysZftS9ZQzo2kEYbNIaXSILYo40FqtQEFxyKlr1sd5LHJGVKrpjZ/bwNQRwMBpB2zU+wYi1V3sgQ2Pw1SghkPz9+/fv37OkYFgjAAYD1P//Z"

if __name__ == "__main__":
    main()
