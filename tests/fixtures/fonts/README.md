# Font fixtures

Self-authored fonts used by the real-text tests (M8b explicit fonts, M10 automatic font selection).
They contain no third-party font data and are small enough to keep in the repository, so the tests
never depend on fonts installed on the machine.

All fixtures were generated once by throwaway Python scripts in `.tmp/` (never committed; only the
generated fonts are checked in). The designs are documented below and in the fonts themselves.

## Files

| File | Family / style | Advances | Notes |
| --- | --- | --- | --- |
| `toy_test.ttf` | `Toy Test Font` / `Regular` (PostScript `ToyTestFont-Regular`) | 600 units (0.6 em) | The M8b fixture. |
| `toy_test_bold.ttf` | `Toy Test Font` / `Bold` (`ToyTestFont-Bold`) | 650 units (0.65 em) | OS/2 weight 700, fsSelection bold. Wider advance proves the bold face was selected. |
| `toy_test_italic.ttf` | `Toy Test Font` / `Italic` (`ToyTestFont-Italic`) | 620 units (0.62 em) | fsSelection italic, post italicAngle -120; outlines are **not** sheared (metadata only, documented limit). |
| `toy_cjk.ttf` | `Toy CJK` / `Regular` (`ToyCJK-Regular`) | 700 units (0.7 em) | Five synthetic Han glyphs only; used to test character-coverage fallback and Chinese fixture pages. |

Common design: unitsPerEm 1000, ascender 800, descender -200, line gap 0 (one em line height),
left side bearing 50, hand-authored 5x7 block-grid outlines (one 100x100-unit block per cell,
baseline at the bottom row). Every glyph shape is our own work.

| File | Glyph count | Charset |
| --- | --- | --- |
| `toy_test*.ttf` | 13 (including `.notdef`) | space, `.`, `0`, `1`, `2`, `?`, `A`, `B`, `C`, `a`, `b`, `c`; every other code point maps to `.notdef` |
| `toy_cjk.ttf` | 6 (including `.notdef`) | `中` (U+4E2D), `文` (U+6587), `你` (U+4F60), `好` (U+597D), `网` (U+7F51); the shapes are deliberately simple block art, not real glyph designs |

At 16 px the design metrics of `toy_test.ttf` are: ascent 12.8 px, descent 3.2 px, line height
16 px, one glyph advance 9.6 px (bold 10.4 px, italic 9.92 px, CJK 11.2 px). Shaping and
rasterization go through the approved freetype/harfbuzz dependencies, so a shaped advance in 26.6
fixed point may be one unit off the exact design value; tests should use the documented geometry of
the committed fixtures rather than re-deriving fractional values.

`toy_cjk.ttf` has no language metadata (fontconfig cannot classify it as Chinese by language), so
the deterministic tests rely on **character coverage** for fallback: `Toy Test Font` does not cover
Han code points, `Toy CJK` does, and the fontconfig test config keeps both fonts in the candidate
set. Language-hint handling (`<html lang>` → fontconfig `FC_LANG`) is exercised on real installed
fonts in manual checks, not through this fixture.

## Deterministic fontconfig setup for tests

The tests never read the machine's font configuration. Each test that needs real fonts sets
`FONTCONFIG_FILE` to a generated config whose only `<dir>` is this directory and whose only
`<cachedir>` is inside the build tree. The tested recipe:

- Map the generic families to the Latin fixture so the default (`serif`) and generic CSS families
  are deterministic. fontconfig normalizes `sans-serif` to the family `sans`, so both `sans` and
  `sans-serif` need a rule:

  ```xml
  <match target="pattern">
    <test name="family" qual="any" compare="eq"><string>serif</string></test>
    <edit name="family" mode="assign" binding="same"><string>Toy Test Font</string></edit>
  </match>
  ```

  (the same rule for `sans`, `sans-serif` and `monospace`).
- Do **not** map `Toy Test Font` itself, and leave `Toy CJK` in the directory: `FcFontSort` then
  returns `Toy Test Font` first and `Toy CJK` as the coverage fallback for Han text.
- With this config: `serif`/`sans-serif`/`monospace` → `Toy Test Font` Regular, `weight=700` →
  `toy_test_bold.ttf`, `slant=italic` → `toy_test_italic.ttf`, and a pattern with `:charset=4e2d`
  matches `toy_cjk.ttf`.
- The **bitmap fallback** is tested with a config that points at a nonexistent directory: fontconfig
  finds no font, the resolver degrades to the built-in bitmap font, and all pre-M10 geometry is
  reproduced byte for byte.

## Regenerating

The generators are intentionally not committed: these are tiny fixtures, and reproducing them means
re-running the same one-off scripts (the table structure and glyph grids are described above and in
the generated fonts). If a fixture ever changes, update this file and the tests that assert its
metrics.

## Manual demos

The fixture fonts are only for deterministic tests. Manual demos of real text should use an
installed font through `--font PATH` or `--font-family NAME`, or the automatic M10 system-font path
(with no font flags); the tests and the repository never depend on one of those being present.
