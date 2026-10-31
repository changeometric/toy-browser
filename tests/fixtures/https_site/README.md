# HTTPS fixture site

The committed page served over TLS by the in-process test server in
`tests/support/tls_server.h` for the `shell.https` end-to-end test. It is never fetched from
the public network: the test reads these files into memory and serves them on loopback with the
test-only certificates in `tests/fixtures/tls/`. Image format and pixel content are documented in
`tests/fixtures/images/README.md`.

## Files

| File | Content |
| --- | --- |
| `index.html` | One page with an `h1`, two paragraphs and one relative image reference (`flag.png`). Title: `HTTPS fixture page`. |
| `flag.png` | Byte-identical copy of `tests/fixtures/images/flag_16x8.png`: 16x8 8-bit RGB, columns 0-7 red `#ff0000`, columns 8-15 blue `#0000ff`, all rows identical. Committed here so the HTTPS fixture directory is self-contained. |

The page deliberately refers to the image with the relative URL `flag.png`, so the image only
loads if the shell resolves that reference against the `https://` page URL and fetches it through
the same transport.

## Frozen geometry

At a 320x200 viewport, frozen from `toybrowser --size 320x200 --dump-boxes` on this page
(1 CSS px = 1 device px). The dump is transport-independent, so the same layout must come out of
the TLS-served page. Relevant dump lines:

```
<html> block x=0 y=0 width=320 height=184
  <body> block x=8 y=8 width=304 height=168
    <h1> block x=8 y=24 width=304 height=32
      text "Secure" x=8 y=24 width=96 height=32
    <p> block x=8 y=88 width=304 height=24
      text "HTTPS" x=8 y=88 width=40 height=24
      text "text" x=56 y=88 width=32 height=24
      <img> inline x=96 y=88 width=48 height=24
      text "after" x=152 y=88 width=40 height=24
    <p> block x=8 y=144 width=304 height=16
      text "Second" x=8 y=144 width=48 height=16
      text "paragraph" x=64 y=144 width=72 height=16
      text "line." x=144 y=144 width=40 height=16
```

The content height (`layout::ContentHeight`, the bottom edge of the tallest box) is 184 device px,
so the whole page fits the 320x200 viewport at scroll 0 and the image is fully visible. The first
paragraph is 24 px tall because its 48x24 replaced box raises the line height; the text boxes in
that line report the same 24 px height.

## Pixels asserted by `shell.https`

The canvas behind the page is white (`#ffffffff`); all other painted content is black bitmap text
and the image. Text assertions only require ink in the region: the hand-authored glyph shapes are
deliberately not frozen.

| Geometry | Assertion |
| --- | --- |
| `h1` text region x 8..103, y 24..55 | non-white ink present |
| paragraph text before the image, x 8..95, y 88..111 | non-white ink present |
| image box x 96..143, y 88..111 | at (96,88) red; at (119,88) red; at (120,88) blue; at (143,111) blue; the row above (y=87) and the column after (x=144) stay white; exactly 576 red and 576 blue pixels in the whole surface (24 red scaled columns x 24 rows and 24 blue scaled columns x 24 rows) |
| paragraph text after the image, x 152..191, y 88..111 | non-white ink present |
| second paragraph, x 8..183, y 144..159 | non-white ink present |
| below the content, y 184..199 | entirely white |

The frozen `content_height` of 184 is also asserted through `Browser::content_height()`. The image
colours are unique on the page, so the exact counts prove the 16x8 PNG was decoded and painted at
the CSS 48x24 size with the frozen nearest-neighbour mapping, and that it was fully inside the
viewport.

## Trust setup

The test process trusts exactly `tests/fixtures/tls/test_ca.pem`: the CTest registration for
`shell.https` sets `SSL_CERT_FILE` to its absolute path through the `ENVIRONMENT` property. The
test code never touches the trust store or disables verification, and a second in-process server
with `tests/fixtures/tls/untrusted_cert.pem` asserts that a certificate which does not chain to the
test CA fails the load and leaves no page.
