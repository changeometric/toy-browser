#!/usr/bin/env python3
"""Regenerate the committed M8b regular test font fixture (toy_test.ttf).

Only Python's standard library is used. Run from anywhere:

    python3 tests/fixtures/fonts/generate_toy_test.py

The script overwrites `toy_test.ttf` next to itself. Output is deterministic for a
given Python version (no font/compression libraries are involved); the committed
README documents the design and the metrics the tests rely on. Keep that README
and the tests in sync when changing the generator.

Design (all our own work):
- unitsPerEm 1000, ascender 800, descender -200, lineGap 0
- monospace: every glyph advances 600 units, lsb 50
- glyph shapes come from a hand-authored 5x7 block grid (one 100x100 unit block
  per cell, origin x=50, baseline y=0 for the bottom row)
- a deliberately small charset: space, . 0 1 2 ? A B C a b c; every other
  character maps to .notdef (a hollow box)
"""

import struct
import pathlib

OUT = pathlib.Path(__file__).resolve().parent / "toy_test.ttf"

UNITS_PER_EM = 1000
ASCENDER = 800
DESCENDER = -200
LINE_GAP = 0
ADVANCE = 600
LSB = 50

# Hand-authored 5x7 glyph grids, top row first. '#' is ink, '.' is empty.
GLYPHS = {
    " ": [
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
    ],
    ".": [
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".##..",
        ".##..",
    ],
    "0": [
        ".###.",
        "#...#",
        "#..##",
        "#.#.#",
        "##..#",
        "#...#",
        ".###.",
    ],
    "1": [
        "..#..",
        ".##..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        ".###.",
    ],
    "2": [
        ".###.",
        "#...#",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        "#####",
    ],
    "?": [
        ".###.",
        "#...#",
        "....#",
        "...#.",
        "..#..",
        ".....",
        "..#..",
    ],
    "A": [
        ".###.",
        "#...#",
        "#...#",
        "#####",
        "#...#",
        "#...#",
        "#...#",
    ],
    "B": [
        "####.",
        "#...#",
        "#...#",
        "####.",
        "#...#",
        "#...#",
        "####.",
    ],
    "C": [
        ".####",
        "#....",
        "#....",
        "#....",
        "#....",
        "#....",
        ".####",
    ],
    "a": [
        ".....",
        ".....",
        ".###.",
        "....#",
        ".####",
        "#...#",
        ".####",
    ],
    "b": [
        "#....",
        "#....",
        "####.",
        "#...#",
        "#...#",
        "#...#",
        "####.",
    ],
    "c": [
        ".....",
        ".....",
        ".####",
        "#....",
        "#....",
        "#...#",
        ".###.",
    ],
}

NOTDEF = [
    "#####",
    "#...#",
    "#...#",
    "#...#",
    "#...#",
    "#...#",
    "#####",
]

# Sorted (codepoint, glyph id) mapping: id 1.. in this order, .notdef is 0.
CODEPOINTS = [ord(c) for c in sorted(GLYPHS)]
GLYPH_PATTERNS = [NOTDEF] + [GLYPHS[chr(cp)] for cp in CODEPOINTS]


def glyph_contours(pattern):
    """Returns contours as lists of (x, y) integer points, clockwise, y-up.

    Horizontal runs of ink in one grid row become one rectangle contour; this
    keeps the point count small and the outlines simple.
    """
    contours = []
    for row, line in enumerate(pattern):
        col = 0
        while col < 5:
            if line[col] != "#":
                col += 1
                continue
            start = col
            while col < 5 and line[col] == "#":
                col += 1
            x0 = LSB + start * 100
            x1 = LSB + col * 100
            y0 = (6 - row) * 100
            y1 = (7 - row) * 100
            contours.append([(x0, y0), (x0, y1), (x1, y1), (x1, y0)])
    return contours


def build_glyf_and_loca():
    glyf = bytearray()
    loca = [0]
    for pattern in GLYPH_PATTERNS:
        contours = glyph_contours(pattern)
        start = len(glyf)
        if not contours:
            loca.append(start)
            continue
        points = [point for contour in contours for point in contour]
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        data = struct.pack(">hhhhh", len(contours), min(xs), min(ys), max(xs), max(ys))
        end = -1
        for contour in contours:
            end += len(contour)
            data += struct.pack(">H", end)
        data += struct.pack(">H", 0)  # instructionLength
        data += bytes([1] * len(points))  # all points on-curve, 16-bit deltas
        previous = 0
        for x, _ in points:
            data += struct.pack(">h", x - previous)
            previous = x
        previous = 0
        for _, y in points:
            data += struct.pack(">h", y - previous)
            previous = y
        glyf += data
        while len(glyf) % 4:  # keep glyph offsets aligned
            glyf += b"\x00"
        loca.append(len(glyf))
    # Long loca: uint32 offsets.
    loca_data = b"".join(struct.pack(">I", offset) for offset in loca)
    return bytes(glyf), loca_data


def build_cmap():
    mapping = [(cp, gid) for gid, cp in enumerate(CODEPOINTS, start=1)]
    mapping.sort()
    segments = []
    index = 0
    while index < len(mapping):
        start_cp, start_gid = mapping[index]
        end_cp, end_gid = start_cp, start_gid
        index += 1
        while index < len(mapping):
            cp, gid = mapping[index]
            if cp == end_cp + 1 and gid == end_gid + 1:
                end_cp, end_gid = cp, gid
                index += 1
            else:
                break
        segments.append((start_cp, end_cp, (start_gid - start_cp) & 0xFFFF))
    segments.append((0xFFFF, 0xFFFF, 1))

    seg_count = len(segments)
    entry_selector = max(0, seg_count.bit_length() - 1)
    search_range = 2 * (1 << entry_selector)
    range_shift = 2 * seg_count - search_range
    subtable = struct.pack(
        ">HHHHHHH",
        4,  # format
        16 + 8 * seg_count,  # length
        0,  # language
        2 * seg_count,
        search_range,
        entry_selector,
        range_shift,
    )
    subtable += b"".join(struct.pack(">H", end) for _, end, _ in segments)
    subtable += struct.pack(">H", 0)  # reservedPad
    subtable += b"".join(struct.pack(">H", start) for start, _, _ in segments)
    subtable += b"".join(struct.pack(">H", delta) for _, _, delta in segments)
    subtable += b"".join(struct.pack(">H", 0) for _ in segments)

    header = struct.pack(">HH", 0, 1)  # version 0, one encoding record
    encoding = struct.pack(">HHI", 3, 1, 12)  # Windows Unicode BMP, offset 12
    return header + encoding + subtable


def build_name():
    strings = {
        1: "Toy Test Font",
        2: "Regular",
        3: "Toy Test Font; regular; generated by the toy-browser project",
        4: "Toy Test Font Regular",
        6: "ToyTestFont-Regular",
    }
    storage = bytearray()
    records = []
    for name_id in sorted(strings):
        encoded = strings[name_id].encode("utf-16-be")
        records.append((3, 1, 0x409, name_id, len(encoded), len(storage)))
        storage += encoded
    header = struct.pack(">HHH", 0, len(records), 6 + 12 * len(records))
    entries = b"".join(
        struct.pack(">HHHHHH", platform, encoding, language, name_id, length, offset)
        for platform, encoding, language, name_id, length, offset in records
    )
    return header + entries + bytes(storage)


def build_head(glyf_x_min, glyf_x_max, glyf_y_min, glyf_y_max, num_glyphs):
    # checkSumAdjustment stays 0 here; it is patched after assembling the file.
    # created/modified: fixed seconds since 1904-01-01, for byte-stable output.
    timestamp = 3_500_000_000
    return struct.pack(
        ">IIIIHHqqhhhhHHhhh",
        0x00010000,  # version
        0x00010000,  # fontRevision
        0,  # checkSumAdjustment (patched later)
        0x5F0F3CF5,  # magicNumber
        0x0001,  # flags: baseline at y=0
        UNITS_PER_EM,
        timestamp,
        timestamp,
        glyf_x_min,
        glyf_y_min,
        glyf_x_max,
        glyf_y_max,
        0,  # macStyle
        8,  # lowestRecPPEM
        2,  # fontDirectionHint
        1,  # indexToLocFormat: long
        0,  # glyphDataFormat
    )


def build_hhea(num_hmetrics):
    return struct.pack(
        ">IhhhHhhhhhhhhhhhH",
        0x00010000,
        ASCENDER,
        DESCENDER,
        LINE_GAP,
        ADVANCE,  # advanceWidthMax
        LSB,  # minLeftSideBearing
        ADVANCE - LSB,  # minRightSideBearing (glyphs end at x=550)
        LSB + 500,  # xMaxExtent
        1,  # caretSlopeRise
        0,  # caretSlopeRun
        0,  # caretOffset
        0,
        0,
        0,
        0,  # reserved
        0,  # metricDataFormat
        num_hmetrics,
    )


def build_maxp(num_glyphs, max_points, max_contours):
    return struct.pack(
        ">IHHHHHHHHHHHHHH",
        0x00010000,
        num_glyphs,
        max_points,
        max_contours,
        0,  # maxCompositePoints
        0,  # maxCompositeContours
        2,  # maxZones
        0,  # maxTwilightPoints
        0,  # maxStorage
        0,  # maxFunctionDefs
        0,  # maxInstructionDefs
        0,  # maxStackElements
        0,  # maxSizeOfInstructions
        0,  # maxComponentElements
        0,  # maxComponentDepth
    )


def build_os2():
    data = bytearray()
    data += struct.pack(">HhHHH", 4, ADVANCE, 400, 5, 0)
    data += struct.pack(">hhhh", 650, 600, 0, 75)
    data += struct.pack(">hhhh", 650, 600, 0, 350)
    data += struct.pack(">hh", 50, 300)
    data += struct.pack(">h", 0)  # sFamilyClass
    data += bytes([2, 0, 0, 0, 0, 0, 0, 0, 0, 0])  # panose: Latin text
    data += struct.pack(">IIII", 1, 0, 0, 0)  # ulUnicodeRange: Basic Latin
    data += b"TOY "
    data += struct.pack(">HH", 0x0040, 32)  # fsSelection: REGULAR; usFirstCharIndex
    data += struct.pack(">H", 99)  # usLastCharIndex
    data += struct.pack(">hhh", ASCENDER, DESCENDER, LINE_GAP)
    data += struct.pack(">HH", ASCENDER, -DESCENDER)
    data += struct.pack(">II", 1, 0)  # ulCodePageRange1: Latin 1
    data += struct.pack(">hh", 500, 700)  # sxHeight, sCapHeight
    data += struct.pack(">HHH", 0, 32, 1)  # usDefaultChar, usBreakChar, usMaxContext
    assert len(data) == 96, len(data)
    return bytes(data)


def build_post():
    return struct.pack(
        ">IIhhIIIII",
        0x00030000,  # version 3.0 (no glyph names)
        0,  # italicAngle
        -100,  # underlinePosition
        50,  # underlineThickness
        1,  # isFixedPitch
        0,
        0,
        0,
        0,
    )


def build_hmtx(num_glyphs):
    return b"".join(struct.pack(">Hh", ADVANCE, LSB) for _ in range(num_glyphs))


def table_checksum(data):
    padded = data + b"\x00" * ((4 - len(data) % 4) % 4)
    return sum(struct.unpack(">%dI" % (len(padded) // 4), padded)) & 0xFFFFFFFF


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    glyf_data, loca_data = build_glyf_and_loca()
    num_glyphs = len(GLYPH_PATTERNS)
    max_points = max(
        sum(len(contour) for contour in glyph_contours(pattern)) for pattern in GLYPH_PATTERNS
    )
    max_contours = max(len(glyph_contours(pattern)) for pattern in GLYPH_PATTERNS)
    tables = {
        "head": build_head(LSB, LSB + 500, 0, 700, num_glyphs),
        "hhea": build_hhea(num_glyphs),
        "maxp": build_maxp(num_glyphs, max_points, max_contours),
        "hmtx": build_hmtx(num_glyphs),
        "cmap": build_cmap(),
        "loca": loca_data,
        "glyf": glyf_data,
        "name": build_name(),
        "OS/2": build_os2(),
        "post": build_post(),
    }
    tags = sorted(tables)
    num_tables = len(tags)
    entry_selector = max(0, num_tables.bit_length() - 1)
    search_range = 16 * (1 << entry_selector)
    header = struct.pack(
        ">IHHHH", 0x00010000, num_tables, search_range, entry_selector,
        16 * num_tables - search_range,
    )
    offset = 12 + 16 * num_tables
    directory = bytearray()
    payload = bytearray()
    head_offset = None
    for tag in tags:
        data = tables[tag]
        directory += struct.pack(">4sIII", tag.encode("latin-1"), table_checksum(data), offset,
                                 len(data))
        if tag == "head":
            head_offset = offset
        payload += data
        while len(payload) % 4:
            payload += b"\x00"
        offset = 12 + 16 * num_tables + len(payload)
    font = bytearray(header + directory + payload)
    whole = sum(struct.unpack(">%dI" % (len(font) // 4), bytes(font))) & 0xFFFFFFFF
    adjustment = (0xB1B0AFBA - whole) & 0xFFFFFFFF
    struct.pack_into(">I", font, head_offset + 8, adjustment)
    OUT.write_bytes(bytes(font))
    print(f"wrote {OUT} ({len(font)} bytes, {num_glyphs} glyphs, {len(CODEPOINTS)} codepoints)")


if __name__ == "__main__":
    main()
