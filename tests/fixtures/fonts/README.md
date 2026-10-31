# Font fixtures

Self-authored fonts used by the real-text (M8b) tests. They contain no third-party
font data and are small enough to keep in the repository, so the tests never depend
on fonts installed on the machine.

## `toy_test.ttf`

A deliberately minimal TrueType font generated once by a throwaway Python script in
`.tmp/` (never committed; only the generated font is checked in).

| Property | Value |
| --- | --- |
| Family | `Toy Test Font` (style `Regular`, PostScript name `ToyTestFont-Regular`) |
| unitsPerEm | 1000 |
| Ascender / descender / line gap | 800 / -200 / 0 (1 em line height) |
| Advances | monospace: every glyph advances 600 units (0.6 em), left side bearing 50 |
| Glyph count | 13, including `.notdef` |
| Charset | space, `.`, `0`, `1`, `2`, `?`, `A`, `B`, `C`, `a`, `b`, `c`; every other code point maps to `.notdef` |
| Shapes | hand-authored 5x7 block grids; one 100x100-unit block per cell, baseline at the bottom row |

At 16 px the design metrics are: ascent 12.8 px, descent 3.2 px, line height 16 px,
one glyph advance 9.6 px in design units. Shaping and rasterization go through the
approved freetype/harfbuzz dependencies, so a shaped advance in 26.6 fixed point may
be one unit off the exact design value; tests should use the documented geometry of
the committed fixtures rather than re-deriving fractional values.

The font's `name` table makes it discoverable through fontconfig; the tests point
fontconfig at this directory with a test-local `FONTCONFIG_FILE` and ask for the
family `Toy Test Font`.

## Regenerating

The generator is intentionally not committed: the file is a tiny fixture, and
reproducing it means re-running the same one-off script (the table structure and
glyph grids are described above and in the generated font itself). If the fixture
ever changes, update this file and the tests that assert its metrics.

## Manual demos

The fixture font is only for deterministic tests. Manual demos of real text should
use an installed font through `--font PATH` or `--font-family NAME`; the tests and
the repository never depend on one of those being present.
