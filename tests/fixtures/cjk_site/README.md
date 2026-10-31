# CJK fixture site

A committed page for the M10 end-to-end tests of automatic font selection and CJK
text layout: the browser resolves the computed CSS font family/weight/style
through fontconfig, splits text into character-coverage runs and falls back to a
font that covers Han characters, and M10 layout splits Chinese text into break
units so a paragraph without spaces can wrap. Tests never use installed fonts:
they point `FONTCONFIG_FILE` at the generated fixture config whose only `<dir>` is
`tests/fixtures/fonts/` (see [the font fixtures README](../fonts/README.md) for the
recipe and the attributes of the committed faces).

## Page

- `index.html` is a `file://` page with `<html lang="zh-CN">` and Chinese text from
  the Toy CJK charset (`你 好 网 中 文`), mixed with Latin text.
- `<style>`: `body { font-family: serif; font-size: 16px; margin: 8px }`,
  `.bold { font-weight: 700 }`, `.italic { font-style: italic }`. The user-agent
  sheet keeps its rules: `h1 { font-size: 32px; font-weight: 700; margin: 16px 0 }`
  and the default 8px body margin.
- The fixture config maps the generic families to `Toy Test Font` (regular, bold
  for weight 700, italic for `font-style: italic`); `Toy CJK` stays the
  character-coverage fallback for Han text. `lang="zh-CN"` is passed to fontconfig
  as `FC_LANG`; the fixture faces carry no language metadata, so the Han fallback
  is determined by character coverage only.
- The last paragraph repeats the five Han characters seven times (35 break units in total). Chinese text has no
  spaces, so layout splits it into break units (one Han code point per unit) and
  the paragraph wraps into two lines at the frozen viewport: 27 units on the first
  line (27 * 0.7 em = 302.484375 px, fitting the 304 px content width) and the
  remaining 8 units on the second line. The paragraph block is therefore 32 px
  tall and the page content height is 288 px.

## Frozen geometry

`toybrowser --size 320x288 --dump-boxes` with the fixture config (1 CSS px = 1
device px; positions/widths are `std::lround` of the layout geometry). The text
boxes are in box-tree (pre-order) order; a space separator produces no text box,
it only advances the next text box:

```text
<html> block x=0 y=0 width=320 height=288
  <body> block x=8 y=8 width=304 height=272
    <h1> block x=8 y=24 width=304 height=32
      text "你" x=8 y=24 width=22 height=32
      text "好" x=30 y=24 width=22 height=32
      text "网" x=53 y=24 width=22 height=32
    <p> block x=8 y=88 width=304 height=16
      text "Hello" x=8 y=88 width=48 height=16
      text "中" x=66 y=88 width=11 height=16
      text "文" x=77 y=88 width=11 height=16
    <p> block x=8 y=136 width=304 height=16
      text "ABC" x=8 y=136 width=31 height=16
      text "网" x=50 y=136 width=11 height=16
    <p> block x=8 y=184 width=304 height=16
      text "abc" x=8 y=184 width=30 height=16
      text "好" x=48 y=184 width=11 height=16
      text "Hello" x=69 y=184 width=50 height=16
    <p> block x=8 y=232 width=304 height=32
      text "中" x=8 y=232 width=11 height=16
      ... 27 units on the first line, last one x=299 y=232 width=11 height=16 ...
      text "你" x=8 y=248 width=11 height=16
      ... 8 units on the second line, last one x=86 y=248 width=11 height=16 ...
```

The long paragraph first line starts at x=8 and advances by the 0.7 em Han
break-unit distance (11.203125 px) per character: 8, 19, 30, 42, 53, ... up to
the 27th unit at x=299 (299 + 11.203125 > 312 = the content right edge, so the
28th unit starts the second line at y=248). The second line has 8 units at x=8,
19, ..., 86, so the paragraph block is 32 px tall.

Why the widths: a Han break unit is one Toy CJK glyph with a 0.7 em advance, so
11.203125 px at 16 px -> 11 and 22.40625 px -> 22 at 32 px. `Hello` at 16 px is
five regular `Toy Test Font` advances (5 * 0.6 em = 48 px); `ABC` in the bold
paragraph uses the bold fixture face (3 * 0.65 em = 31.2 px -> 31); `abc` in the
italic paragraph uses the italic face (3 * 0.62 em = 29.76 px -> 30) and `Hello`
there is five italic advances (5 * 0.62 em = 49.6 px -> 50). One line is one em
(32 px for the heading, 16 px otherwise); layout sets each line's ascent (0.8 em)
on its text boxes, so paint baselines are 50 for the heading, 101 / 149 / 197 for
the 16 px paragraphs and 245 / 261 for the two wrapped lines.

## Expected pixels

`toybrowser --size 320x288 --screenshot` renders the whole page white with black
text (no backgrounds). The CJK fixture glyphs are synthetic block art, so tests
assert ink presence and anti-aliased (blended) pixels rather than exact coverage
bytes (Freetype/harfbuzz output may differ between library versions).
`tests/shell/` checks these device-pixel regions, which are the frozen glyph
extents:

| Region | Rectangle (x0, y0) - (x1, y1) |
| --- | --- |
| heading (32 px Han) | (8, 20) - (76, 60) |
| paragraph Latin `Hello` | (8, 88) - (56, 106) |
| paragraph Han fallback `中文` | (66, 88) - (88, 106) |
| bold paragraph `ABC 网` | (8, 136) - (42, 154) |
| italic paragraph `abc 好` | (8, 184) - (40, 202) |
| wrapped long paragraph, line 1 | (8, 232) - (311, 248) |
| wrapped long paragraph, line 2 | (8, 248) - (98, 264) |

Every region must contain at least one non-white pixel (ink) and at least one
pixel that is neither white nor opaque black (real-font anti-aliasing); the
frozen bitmap path would only produce opaque black cells for ASCII and `?` for
Han text, so it cannot satisfy the blended-pixel check.

## Tests

- `shell.cjk_dump_boxes`: `--dump-boxes` output contains the frozen per-unit
  text-box positions/widths above, including the heading's three Han boxes, the
  two Han boxes after `Hello`, and the wrapped paragraph's first-line last unit
  (x=299, y=232) plus second-line first unit (x=8, y=248) and last unit (x=86).
- `shell.cjk_layout`: loads the same page through the shell-private loader and
  asserts the exact text-box sequence and UTF-8 contents (one Han code point per
  box, Latin words grouped), the wrapped line break and the per-line baseline
  offsets, so a segmentation change that keeps the dump positions identical still
  fails.
- `shell.cjk_screenshot`: `--screenshot` succeeds, writes a 320x288 binary PPM,
  and the ink/anti-aliasing checks above pass.
- `shell.font_selection` / `shell.font_selection_bitmap`: the font-selection unit
  tests (including the "English + Chinese mixed runs, no .notdef gap" checks);
  the bitmap variant runs the same code with the empty fontconfig config.

Known M10 limits: one text box per CJK character (no word dictionary), no
cross-text-node punctuation gluing (the fixture has no punctuation because the
Toy CJK charset covers none), Latin words are never split, and other scripts
(beyond English + Chinese) only work through generic fontconfig coverage (no
bidi/RTL, no Thai/Khmer dictionary breaking).
