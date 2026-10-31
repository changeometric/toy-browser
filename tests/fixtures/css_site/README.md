# CSS fixture site (M12 external stylesheets)

Committed fixture site for the M12 external-stylesheet tests (`shell.external_css`,
`shell.dump_style_external`, `shell.dump_style_filtered_only`). It is served from
disk with `file://` only; the loopback-server cases build their pages in memory in
`tests/shell/external_css_test.cpp`. No public network, display or installed font
is involved, and the expected values below are the deterministic built-in bitmap
font path (`FONTCONFIG_FILE` = the generated empty test config).

## Files

- `index.html`: main page. A relative subdirectory sheet (`styles/site.css`) is
  linked between two inline `<style>` elements, and one element carries an inline
  `style` attribute.
- `ordering.html`: document-order interleaving. Equal-specificity rules appear
  before, at and after the `<link>`.
- `filter.html`: every filtered/failing link kind (print media, `text/plain`
  type, `disabled`, `alternate`, `ftp:`, empty `href`, missing file), plus one good
  head sheet and one good body sheet. Only the two good sheets apply.
- `filtered_only.html`: all of the filtered/failing link kinds and no applicable
  sheet; it must render exactly like `no_sheet.html`.
- `no_sheet.html`: a normal page without any `<link>`; proves pages without links
  make no stylesheet request and keep their pre-M12 output.
- `duplicate.html`: the same sheet URL linked twice (one fetch per occurrence, no
  deduplication).
- `styles/site.css`: external sheet of `index.html`.
- `styles/ordering.css`: external sheet of `ordering.html`.
- `styles/good.css`: the applicable head sheet of `filter.html`.
- `styles/body.css`: the sheet linked from `filter.html`'s body (discovery covers
  the whole DOM, not just the head).
- `styles/bad_media.css`: would recolor `#good` if `media="print"` were ignored.
- `styles/bad_type.css`: would shrink `#good` if `type="text/plain"` were ignored.
- `styles/bad_disabled.css`: would push `#good` down if `disabled` were ignored.
- `styles/bad_alternate.css`: would give `#good` a blue background if `alternate`
  were ignored.
- `styles/duplicate.css`: external sheet of `duplicate.html`.

## Expected computed values

`index.html` (`--dump-style` lines, all lengths in CSS px):

- `p#external` (`.external` in `styles/site.css`): `color=#123456`,
  `background-color=#abcdef`, `font-size=24px`, margins `6px 10px 12px 14px`.
- `p#before`: `color=#00ff00` — the first inline `<style>` sets red, but the linked
  sheet comes later in document order at equal specificity and wins.
- `p#after`: `color=#0000ff` — the second inline `<style>` comes after the link and
  wins over the linked sheet's `#ff8800`.
- `p#inline`: `color=#00aa00` — the inline `style` attribute wins over the sheet's
  `#ff00ff`.

`index.html` frozen box geometry (from
`toybrowser --size 320x200 --dump-boxes`):

```
<html> block x=0 y=0 width=320 height=202
  <body> block x=8 y=8 width=304 height=186
    <p> block x=22 y=14 width=280 height=24
      text "External" x=22 y=14 width=96 height=24
      text "rules" x=130 y=14 width=60 height=24
    <p> block x=8 y=66 width=304 height=16
      text "Before" x=8 y=66 width=48 height=16
    <p> block x=8 y=114 width=304 height=16
      text "After" x=8 y=114 width=40 height=16
    <p> block x=8 y=162 width=304 height=16
      text "Inline" x=8 y=162 width=48 height=16
```

The `.external` font-size changes the line height (`24` instead of `16`) and its
margins shift the box to `x=22 y=14 width=280`, so the geometry proves an external
rule reached layout. The same page at `--size 320x200` paints the external
`#abcdef` background over the 280x24 box and the ordered text colors:
6390 background pixels, 330 `#123456` text pixels, 102 `#00ff00`, 75 `#0000ff`
and 85 `#00aa00` text pixels.

`ordering.html`:

- `p#first`: `#00ff00` (linked sheet after the first inline sheet).
- `p#second`: `#0000ff` (second inline sheet after the link).
- `p#third`: `#0000ff` (the last inline sheet wins over both).

`filter.html`:

- `p#good`: `color=#336699`, `background-color=#cccccc`, `font-size=18px`,
  `margin-top=4px`, `margin-left=5px` — from `styles/good.css` only.
- `p#bad`: the default values (`color=#000000`, transparent background,
  `font-size=16px`, UA `p` margins `16px 0`) — the filtered sheets contributed
  nothing.
- `p#body-sheet`: `color=#445566`, `font-size=14px` — from `styles/body.css`,
  linked from the body (the discovery walk covers the whole DOM).

`duplicate.html`:

- `p#dup`: `color=#0f0f0f`, `font-size=20px`, `margin-left=7px`.

`no_sheet.html` and `filtered_only.html`:

- `p#plain`: the default values (`color=#000000`, transparent background,
  `font-size=16px`, UA `p` margins `16px 0`).
- Both pages produce the same box tree and identical painted pixels; the skipped
  `<link>` elements are `display:none` and contribute no stylesheet rules.

## Regenerating the expected values

The values above are reproducible with the default bitmap path:

```sh
FONTCONFIG_FILE=<build>/tests/shell/fontconfig/empty-fonts.conf \
  <build>/toybrowser --dump-style "file://$PWD/tests/fixtures/css_site/index.html"
FONTCONFIG_FILE=<build>/tests/shell/fontconfig/empty-fonts.conf \
  <build>/toybrowser --size 320x200 --dump-boxes \
  "file://$PWD/tests/fixtures/css_site/index.html"
```
