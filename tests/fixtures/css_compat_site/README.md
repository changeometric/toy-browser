# CSS compatibility fixture site (M14a/M14b)

Committed fixture site for the M14a/M14b CSS-compatibility batches. It is served
from `file://` only, needs no network, display or installed font, and is asserted
by `paint.display_list`, `shell.css_compat`, `shell.dump_style_css_compat*` and
`shell.css_compat_screenshot*`.

The expected values below are the deterministic built-in bitmap font path (the
tests pin `FONTCONFIG_FILE` to the generated empty test config) at a 320 px
viewport: 1 CSS px = 1 device px, the UA body margin is 8 px and the UA `p` margins
are 16 px 0.

## Files

- `values.html`: percentage and rem lengths, `rgb()`/`rgba()`/`hsl()` colors, the
  `background` shorthand's color part, logical properties (`margin-inline`,
  `padding-block`, `inline-size`, `max-inline-size`) and `min-width`/`max-width`.
- `typography.html`: `line-height` (unitless number, px, `normal`), `text-align`
  (center/right), `text-decoration: underline`, and `visibility: hidden` on a
  block with a visible child inside it.
- `boxes.html`: `min-width`/`max-width`, `margin: 0 auto` centering, percentage
  width, a percentage height treated as auto, and a replaced `<img>` with a CSS
  width (the image is `tests/fixtures/images/flag_16x8.png` referenced through
  `../images/`, so no binary fixture is duplicated here).
- `legacy.html`: uses only pre-M14 CSS (hex colors, px sizes, px margins/padding,
  font-size). Its box dump and screenshot must stay byte-identical to the pre-M14
  engine; only its `--dump-style` lines gain the appended M14a/M14b fields.
- `borders.html`: 1-4 sided borders with different widths/colors, `currentColor`,
  an `rgba()` border color, a non-solid style, `box-sizing` content-box vs
  border-box, and a nested bordered/padded box.
- `overflow.html`: a fixed-size `overflow: hidden` box with an oversized child and
  text that must be clipped, a sibling after it, the same child with `overflow:
  visible`, a bordered clipping box, and a box after the examples.
- `decorations.html`: `underline`, `line-through`, `overline` and their
  combination, plus an inherited decoration and a child that resets the flags.

## Expected computed values (`--dump-style`)

`values.html`:

- `#rgb`: `color=#ff0000` (`rgb(255, 0, 0)`). `#rgba`: background
  `rgba(0, 0, 255, 0.5)` (alpha 128 is kept; the style dump prints the RGB part
  `#0000ff`). `#hsl`: `background-color=#008000`. `#shorthand`:
  `background-color=#123456`.
- `#percent`: `width=50%`, `margin-left=10%` (percentages stay percentages in the
  computed style; layout resolves them).
- `#rem`: `padding: 1.5rem` -> `padding-*=24px`, `margin-top=8px` (root font size
  16 px).
- `#logical`: `margin-left=10px margin-right=20px`, `padding-top=4px
  padding-bottom=6px`, `width=120px` (logical properties map to the physical
  sides).
- `#maxlogical`: `width=300px max-width=200px` (`max-inline-size`).
- `#minmax`: `width=50px min-width=150px max-width=200px`.

`typography.html`:

- `#lh-number`: `font-size=20px line-height=1.5`; `#lh-px`: `line-height=24px`;
  `#lh-normal`: `line-height=normal`.
- `#center`: `text-align=center`; `#right`: `text-align=right`.
- `#underline`: `text-decoration=underline`.
- `#hidden`: `visibility=hidden` with background `#123456`; `#hidden-child`:
  `visibility=visible` with background `#abcdef`.

`boxes.html`:

- `#auto-center`: `width=120px margin-left=auto margin-right=auto`.
- `#pct-width`: `width=25%`.
- `#min-width`: `width=50px min-width=150px`.
- `#max-width`: `width=500px max-width=200px`.
- `#height-pct`: `height=50%` (layout treats it as auto).
- `#replaced-image`: `width=32px` (keeps the intrinsic 2:1 ratio).

`legacy.html`: unchanged pre-M14 values (`#box`: `background-color=#112233 width=100px
height=20px`; `#text`: `color=#ff0000 font-size=24px margin=4px 8px padding=2px 4px`).

## Expected box geometry (320 px viewport, `--dump-boxes`)

`values.html` (content height 328 px):

```
<div> block x=38 y=182 width=152 height=20      # 50% width, 10% margin-left
<div> block x=8  y=210 width=304 height=64      # 1.5rem padding (24 px) + 16 px line
<div> block x=18 y=274 width=120 height=26      # logical margins/padding/inline-size
<div> block x=8  y=300 width=200 height=10      # max-inline-size caps 300 -> 200
<div> block x=8  y=310 width=150 height=10      # min-width wins over width: 50px
```

`typography.html` (content height 390 px):

```
<p> block x=8 y=24  width=304 height=30         # line-height 1.5 at 20 px
<p> block x=8 y=86  width=304 height=24         # line-height 24px
<p> block x=8 y=142 width=304 height=16         # line-height normal
<p> block x=8 y=190 width=304 height=16         # text-align center
<p> block x=8 y=238 width=304 height=16         # text-align right
<p> block x=8 y=286 width=304 height=16         # text-decoration underline
<div> block x=8 y=318 width=304 height=16       # visibility hidden: space kept
<p> block x=8 y=350 width=304 height=16         # next block after the hidden space
```

The centered line box is `x=128` (`8 + (304 - 64) / 2`), the right-aligned line
box is `x=272` (`8 + 304 - 40`).

`boxes.html` (content height 128 px):

```
<div> block x=100 y=8  width=120 height=12      # margin: 0 auto centers it
<div> block x=8   y=20 width=76  height=12      # 25% of 304
<div> block x=8   y=32 width=150 height=12      # min-width 150
<div> block x=8   y=44 width=200 height=12      # max-width 200
<div> block x=8   y=56 width=304 height=16      # height: 50% -> auto (text line)
<p>   block x=8   y=88 width=304 height=16
  <img> inline x=8 y=88 width=32 height=16      # replaced image, intrinsic ratio
```

`legacy.html`:

```
<div> block x=8  y=8  width=100 height=20
<p>   block x=16 y=32 width=288 height=28       # 8 px margins, 2 px/4 px padding
  text "Legacy" x=20 y=34 width=72 height=24
```

## Expected painted pixels (`--screenshot`, 320 px viewport)

`values.html`:

- `rgba(0, 0, 255, 0.5)` over the white canvas blends with the frozen SrcOver
  formula to `#7f7fff`: pixel (200, 74).
- `hsl()` background `#008000` at (200, 116); `background: #123456` at (200, 158).
- The 50% box paints `#eeeeee` at (100, 190) while its 10% margin area (20, 190)
  stays white; the rem box paints `#dddddd` at (300, 250); the logical box paints
  `#ccddcc` at (100, 280) with white at (10, 280); the max-inline-size box paints
  `#ccccdd` at (150, 304); the min-width box paints `#ddccdd` at (140, 314).
- `rgb(255, 0, 0)` text paints red ink in the first paragraph.

`typography.html`:

- Centered text: ink only inside x 128..192 at y 190..205. Right-aligned text: ink
  only inside x 272..312 at y 238..253.
- The underline is one solid black row at y = 286 (box y) + 16 (rounded baseline
  offset) + 1 = 303 over x 8..87 (the 80 px text box width); (88, 303) is white.
- The hidden block's `#123456` background paints nowhere; the visible child's
  `#abcdef` background paints inside the hidden block (e.g. (100, 320)) and its
  text ink is painted. The text after the hidden block is painted normally.

`boxes.html`:

- Backgrounds at x positions from the geometry above (`#e0e0e0` at (150, 10),
  `#d0d0d0` at (50, 25), `#c0c0c0` at (150, 35), `#b0b0b0` at (200, 50),
  `#a0a0a0` at (200, 60)); the right edges stay white.
- The replaced flag image scales to 32x16: red columns 8..23, blue columns 24..39.

`legacy.html`: `#112233` background at (10, 10), red text ink in the paragraph;
the box dump and screenshot are byte-identical to the pre-M14 engine (the Lead's
byte-compatibility gate compares them against a pre-M14 binary).

## Reproducing the values

```sh
FONTCONFIG_FILE=<build>/tests/shell/fontconfig/empty-fonts.conf \
  <build>/toybrowser --dump-style "file://$PWD/tests/fixtures/css_compat_site/values.html"
FONTCONFIG_FILE=<build>/tests/shell/fontconfig/empty-fonts.conf \
  <build>/toybrowser --size 320x400 --dump-boxes \
  "file://$PWD/tests/fixtures/css_compat_site/typography.html"
FONTCONFIG_FILE=<build>/tests/shell/fontconfig/empty-fonts.conf \
  <build>/toybrowser --size 320x400 --screenshot /tmp/css_compat.ppm \
  "file://$PWD/tests/fixtures/css_compat_site/boxes.html"
```


## M14b: borders, overflow and decoration lines

All numbers are for the 320 px viewport (body content width 304 px) and the
bitmap font path (16 px text, baseline offset 16 px on one 16 px line).

### `borders.html`

Computed values (`--dump-style`):

- `#top-only`: `border-top-width=4px border-top-style=solid
  border-color=#ff0000`, other sides `0px`/`none`.
- `#four`: widths `2px 3px 4px 5px`, styles all `solid`, colors
  `#ff0000 #00ff00 #0000ff #ffff00` (top right bottom left).
- `#shorthand`: `border: 3px solid` with `color=#123456`; the color is
  `currentColor` (resolved to the text color `#ff123456` when painted).
- `#alpha`: `border: 2px solid rgba(255,0,0,0.5)` keeps a 128 alpha border color.
- `#dashed`: style `dashed` (painted as a solid rectangle in this MVP).
- `#content-box` / `#border-box`: `box-sizing=content-box` / `border-box`.

Frozen box geometry (the border widths are part of the border box):

```
<div> block x=8 y=8   width=304 height=16     # top border 4 + content 12
<div> block x=8 y=24  width=108 height=46     # content 100x40 + borders 5/3/4/2
<div> block x=8 y=70  width=304 height=26     # content 20 + 3 px borders
<div> block x=8 y=96  width=304 height=24     # content 20 + 2 px borders
<div> block x=8 y=120 width=304 height=14     # content 10 + 2 px borders
<div> block x=8 y=134 width=110 height=30     # content-box: 100x20 content + 5 px
<div> block x=8 y=164 width=100 height=20     # border-box: 100x20 including 5 px
<div> block x=8 y=184 width=304 height=24     # outer: 2 px border + 4 px padding
  <div> block x=14 y=190 width=292 height=12  # inner: 1 px border, content 10
```

Frozen pixels (`--screenshot`, 320x400):

- `#four`: top red at (8,24), right green at (114,40), bottom blue at (50,67),
  left yellow at (9,40); background `#eeeeee` at (50,40).
- Shorthand `#123456` border at (8,70), background `#dddddd` at (100,80).
- `rgba(255,0,0,0.5)` over white = `(255,127,127)` at (8,96); the interior is white.
- Dashed border `#770000` at (8,120).
- content-box: border `#333333` at (8,134), background `#bbbbbb` at (50,145);
  border-box: border `#444444` at (8,164), background `#aaaaaa` at (50,175).
- Nested: outer `#555555` (8,184), padding `#999999` (11,187), inner `#666666`
  (14,190), inner background `#888888` (20,195).
- Frozen exact color counts: red 1432, green 120, blue 432, yellow 200,
  `#123456` 1944, `#dddddd` 5960, `#333333` 1300, `#bbbbbb` 2000, `#444444` 1100,
  `#aaaaaa` 900, `#555555` 1296, `#999999` 2496, `#666666` 604, `#888888` 2900,
  alpha ring `(255,127,127)` 1296.

### `overflow.html`

Computed values: `#clip` and `#bordered-clip` have
`overflow=hidden` (both axes); `#visible` has `overflow=visible`. The bordered
clip has a 3 px cyan border.

Frozen box geometry:

```
<div> block x=8 y=8  width=60  height=24    # clipping box
  <div> block x=8 y=8  width=300 height=80  # oversized child (parent fixed 60x24)
  <p>   block x=8 y=104 width=60 height=32  # text that is clipped away
<div> block x=8 y=32 width=304 height=10    # sibling painted normally
<div> block x=8 y=42 width=60  height=24    # overflow: visible box
  <div> block x=8 y=42 width=300 height=80  # paints outside its parent
<div> block x=8 y=66 width=46  height=22    # 3 px border, content 40x16
  <div> block x=11 y=69 width=200 height=60
<div> block x=8 y=88 width=304 height=16    # after the examples
```

`ContentHeight` is 136 px: the fixed clipping box stays 24 px tall, but the
overflowing text box (y=104..136) is still included in the scrollable content
height (M14b clips painting only).

Frozen pixels (320x400):

- The oversized child background is clipped to exactly the 60x24 padding box:
  red count 1440, (50,20) and (67,31) red, (70,10) white; the clipped text paints
  no black ink inside the clip area.
- The sibling `#00ff00` paints 304x10 (green count 3040) at (50,40).
- `overflow: visible`: the blue child paints outside its 60x24 parent at (250,60).
- Bordered clip: cyan border at (8,66), yellow child at (12,70) inside the 40x16
  padding-box clip; yellow count 640 and cyan count 372; the cyan border at
  (8,70) stays visible outside the clip.
- The box after the examples paints `#cccccc` at (250,100) and its text ink is
  painted.

### `decorations.html`

Computed values: `#underline` sets underline only; `#line-through` line-through
only; `#overline` overline only; `#combined` all three (dump prints
`text-decoration=underline line-through overline`); `#parent` underline and its
plain child inherits it; `#child` declares `line-through`, which resets the
inherited underline flags (line-through only).

Frozen box geometry: the five paragraphs start at y = 24, 72, 120, 168, 216 and
264 (16 px line each; margins 16 px). `#parent` text is "Parent" (x=8 w=48) then
the inherited span (x=64 w=72); `#reset` text is "Reset" (x=8 w=40) then the
child span (x=56 w=40). Content height is 304 px.

Frozen decoration rows (bitmap 16 px text, baseline = box.y+16):

- underline: box.y + 17; line-through: baseline - max(1, round(16/3)) = baseline
  - 5; overline: box.y. All rows are 1 px high, in the text color, width =
  the text box width.
- `#underline`: row 41, x=8..87 (72 px of black), white at x=88.
- `#line-through`: row 83 covers the "Line" box (x=8..39, 32 px) and the
  "through" box (x=48..103, 56 px); the space x=40..47 stays white.
- `#overline`: row 120, x=8..71 (64 px).
- `#combined`: underline row 185, line-through row 179, overline row 168, each
  x=8..71 (64 px).
- Inherited: parent row 233 x=8..55 (48 px) and inherited child row 233
  x=64..135 (72 px).
- Reset child: row 275 x=56..95 (40 px line-through); no underline row (row 281
  is empty for both `#reset` text and the child).

`legacy.html` keeps the M14a-frozen geometry and pixels byte for byte (the style
dump only gains the appended M14b border/box-sizing/overflow fields from the
shared normalization point).
