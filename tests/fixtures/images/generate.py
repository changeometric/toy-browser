#!/usr/bin/env python3
"""Regenerate the committed hand-authored PNG fixtures in this directory.

Only Python's standard library is used. Run from anywhere:

    python3 tests/fixtures/images/generate.py

The script writes `flag_16x8.png`, `alpha_8x8.png`, `gray_4x4.png` and
`palette_4x2.png` next to itself. The pixel content is deterministic and documented
in README.md; the exact PNG bytes may depend on the Python/zlib version because the
IDAT chunk uses zlib compression, so compare content (or the documented pixel
values), not file bytes, when regenerating on another machine. Keep README.md and
the tests in sync when changing a fixture.
"""

import struct
import zlib
import pathlib

OUT = pathlib.Path(__file__).resolve().parent

def chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

def write_png(name, width, height, bit_depth, color_type, rows, palette=None):
    # rows: raw pixel bytes for each scanline (a filter byte is added here).
    ihdr = struct.pack('>IIBBBBB', width, height, bit_depth, color_type, 0, 0, 0)
    raw = b''.join(b'\x00' + row for row in rows)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr)
    if palette is not None:
        data += chunk(b'PLTE', b''.join(struct.pack('>BBB', *c) for c in palette))
    data += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    (OUT / name).write_bytes(data)

# 1. 16x8 RGB: left 8 columns red, right 8 columns blue.
rows = []
for y in range(8):
    rows.append(b''.join(b'\xff\x00\x00' if x < 8 else b'\x00\x00\xff' for x in range(16)))
write_png('flag_16x8.png', 16, 8, 8, 2, rows)

# 2. 8x8 RGBA: left 4 columns semi-transparent red (alpha 128), right 4 opaque green.
rows = []
for y in range(8):
    rows.append(b''.join(b'\xff\x00\x00\x80' if x < 4 else b'\x00\xff\x00\xff' for x in range(8)))
write_png('alpha_8x8.png', 8, 8, 8, 6, rows)

# 3. 4x4 8-bit grayscale: columns 0, 64, 128, 255.
rows = []
for y in range(4):
    rows.append(bytes([0, 64, 128, 255]))
write_png('gray_4x4.png', 4, 4, 8, 0, rows)

# 4. 4x2 palette image, 8-bit indices, colors red/green/blue.
palette = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
rows = [bytes([0, 1, 2, 0]), bytes([2, 1, 0, 2])]
write_png('palette_4x2.png', 4, 2, 8, 3, rows, palette)

print('generated:', sorted(p.name for p in OUT.glob('*.png')))
