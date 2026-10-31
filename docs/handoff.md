# Handoff — current state and takeover guide

This is the snapshot for a team taking over the repository. The canonical reading order is in
[docs/README.md](README.md); the hard rules are in [AGENTS.md](../AGENTS.md).

> **AI use:** this repository is built with heavy use of AI. Read
> [AI-DISCLOSURE.md](../AI-DISCLOSURE.md) before relying on the code or the documentation.

## Where the project is

Milestones done (details and acceptance in [roadmap.md](roadmap.md)):

| Stage | Delivered |
| --- | --- |
| M0 | Design docs, CMake skeleton, stub CLI, toolchain smoke test |
| M1 | X11 window, software `Surface`, headless PPM screenshots (`--headless --screenshot`) |
| M2 | URL parsing, HTTP/1.1 GET, `file://` fetching (`--dump-body`) |
| M3 | HTML tokenizer/tree builder, arena DOM, deterministic dump (`--dump-dom`) |
| M4 | CSS parser/selectors, UA + author cascade, computed styles (`--dump-style`) |
| M5 | `layout` box tree with block/inline/anonymous boxes and line breaking, bitmap-font metrics in `render`, deterministic box dump (`--dump-boxes`) |
| M6 | `render` display list + software rasterizer + hand-authored 8x16 ASCII glyphs, `paint` box-tree-to-display-list module, window/`--screenshot` painting with an optional positional page URL |
| M7 | Navigation: headless `shell::Browser` (final URL, history, scroll, title), `net::ResolveUrl`, box hit-testing, scroll-clipped painting, X11 click/wheel/keys + window titles, committed demo site, `shell.navigation` test |
| M8a | PNG images: `render::Image` + libpng decoder behind `render::DecodePng`, `DrawImage` display-list command/rasterizer, replaced inline `<img>` layout from caller-supplied intrinsic sizes, private shell page loader (fetch/resolve/decode/skip failures), image fixtures and `tests/fixtures/img_site/`, 46 tests |
| M8b | Real fonts: `render::Font` (freetype metrics/rasterization + harfbuzz shaping, 4096 px effective size cap) behind a frozen pimpl, anti-aliased `DrawTextWithFont` with baseline placement, `--font PATH` / `--font-family NAME` (fontconfig) selection, self-authored font fixture and `tests/fixtures/font_site/`, 55 tests; the bitmap path stayed byte-identical |
| M9 | `https://` fetching: `net` URL layer accepts https (default port 443), OpenSSL-backed TLS transport behind the unchanged blocking `Fetch()` sharing the HTTP/1.1 code through a private byte-stream abstraction, always-on certificate + hostname verification against OpenSSL's default trust store (no insecure mode), SNI, existing timeouts, redirects resolved with `net::ResolveUrl` (relative/protocol-relative, http<->https), committed test CA/leaf certs, in-process TLS loopback server, `net.tls` + `shell.https` end-to-end tests, 57 tests; http/file output stayed byte-identical |
| M10 | Automatic font selection: CSS `font-family`/`font-weight`/`font-style` matched through the approved fontconfig library with initial family `serif`, per-code-point character-coverage fallback runs (Han text on Latin pages and vice versa), root `<html lang>` as a fontconfig language hint; a shared `shell::Fonts` object drives layout measurement and painting (`TextMetrics::ascent_px`, `Box::baseline_offset_px`, `paint::FontRunsProvider`); simplified CJK break units in layout so Chinese paragraphs wrap; committed regular/bold/italic/Han fixture fonts and deterministic fixture/empty fontconfig test configs; `--font`/`--font-family` overrides kept; bitmap path remains the byte-identical degradation path; 66 tests |
| M11 | Performance: default RelWithDebInfo build with `-ffp-contract=off`; render text caches (one reusable hb_font/hb_buffer per font, bounded shaped-run and glyph-coverage LRU caches); shell font caches (allocation-free resolved-style fast path, metrics and run LRUs); paint viewport culling with a conservative text margin; window loop fixes (Expose without relayout, resize only on actual change, coalesced X11 Expose/ConfigureNotify bursts, reused surface); byte-identical rendering; 66 tests |
| M12 | External stylesheets: public `style::LinkedStyleSheet` + two-arg `ComputeStyles(document, linked_sheets)` merging linked sheets and `<style>` elements by document order (one-arg overload byte-identical to M11); shell-private `LoadStyledDocument()` sharing one fetch/parse/discover/filter/fetch-sheets/style path between page loading and `--dump-style`; `rel`/`disabled`/`media`/`type` filtering, `href` resolved against the final page URL after redirects, 200..299 bodies only, failed/filtered sheets skipped silently, one fetch per `<link>` occurrence (no dedup); `--dump-dom`/`--dump-body` still fetch no sheets; committed `tests/fixtures/css_site/`, header-only `tests/support/http_server.h` loopback server, `style.external_sheets` + `shell.external_css` + CLI tests; 70 tests; byte-compat and GCC/Clang gates green |
| M13 | Resource loading: public `net::Client` (pimpl, single-threaded) with one idle keep-alive HTTP/1.1 connection per origin, incremental Content-Length/chunked framing with read-ahead for the next request, HTTP/1.1/1.0 persistence rules, transparent gzip via the approved zlib (raw and decoded bodies bounded, other codings are errors), one stale-connection retry before any response byte, redirects and `file://` through the same client; the one-shot `Fetch()` keeps its exact request bytes/behavior. Shell `LoadPage` creates one Client per load for page + stylesheets + images and dedups image URLs per page load (duplicate URLs share one fetch/decode/image object; failed URLs remembered; every `<link>` occurrence still fetched; no cross-load cache). Shared loopback HTTP/TLS test servers gained persistent-connection modes and connection/request counters; `net.keep_alive`, `net.gzip`, `net.tls_keep_alive` and `shell.resource_loading`; 74 tests; byte-compat, GCC/Clang and measured real-page load-time gates |
| M14a | CSS values & typography: `%`/`rem` units, `rgb()`/`rgba()`/`hsl()`/`hsla()` + full CSS named-color set, `background` shorthand color, logical properties mapped to physical sides, min/max-width/height, `margin: auto`, `line-height`, `text-align`, `text-decoration: underline`, `visibility`; new computed `Length`/`AutoLength`/`LineHeight`/`TextAlign`/`Visibility` model (box percentages unresolved for layout), style dump appends `min-width/min-height/max-width/max-height/line-height/text-align/visibility/text-decoration`; layout resolves percentages against the containing content width, clamps min/max, centers with auto margins, applies line-height half-leading baselines and text-align line shifting; paint honors visibility and underlines; committed `tests/fixtures/css_compat_site/`; `style.values`, `style.typography`, `layout.style_properties`, `shell.css_compat`, `shell.dump_style_css_compat`, `shell.css_compat_screenshot`; 80 tests; byte-compat (style-normalized), GCC/Clang and real-site visual gates |
| M14b | Box decorations & constraints: `BorderSide`/`BorderStyle`/`BoxSizing`/`Overflow` computed types and `UsedBorderWidth`/`UsedBorderColor` helpers; border longhands (px/thin/medium/thick widths, ten styles, colors incl. alpha/transparent/currentColor), 1–4 value width/style/color shorthands, side + `border` shorthands with reset-to-initial semantics, `box-sizing`, `overflow` shorthand + `overflow-x/y`, three-flag `text-decoration` reset semantics; style dump extends `text-decoration=` and appends ` border-*-width= border-*-style= border-color= box-sizing= overflow=`; layout puts used borders into the box model (border box = content+padding+borders, content-box vs border-box width/height and px min/max, auto margins against the border box, inline horizontal borders, replaced borders ignored); paint order background→borders→image→children, four border FillRects (solid painting for every style, per-rect culling), overflow-hidden padding-box clip pairs, line-through/overline in addition to underline; css_compat_site gained borders/overflow/decorations pages; `style.borders`, `style.box_model`, `layout.box_model`, extended paint/shell tests and CLI checks; 85 tests; byte-compat (style-normalized), GCC/Clang and real-site visual gates |
| Review round | Fixed unbounded `--size`, file body cap, dump indentation; 20 CTest tests |

**Next planned milestone: M15 — Flexbox / position / float subset (planned, not started).**
M14 (CSS compatibility batch 1) is complete: M14a added the CSS value/typography layer and M14b the
box decorations — borders/border-*, box-sizing, overflow:hidden clipping and line-through/overline.
Manual real-site checks show both batches working (python.org paints per-item colored nav borders
and its universal `box-sizing: border-box` reflows 836 elements, w3.org's skip-link border and
overflow clips apply, gnu.org's skip link paints its border; earlier M14a pass: nav line spacing,
centering, percentages/colors). M15 is the next large layout milestone (position/float/flex/grid
subset). The HTTP proxy (M16) and the JavaScript subset remain deferred.

Not implemented yet: parallel/threaded resource loading, cross-page image caching and lazy loading
(M13 keeps serial reuse and per-page dedup only), `border-radius`, `box-shadow`, position/float/
flexbox/grid and other display values, CSS variables, at-rules/webfonts, background images,
transforms/animations (M15 and later/backlog), proxies (M16), JavaScript (deferred), horizontal
scrolling/scrollbars, hover/active link styling and form controls, `@font-face`/webfont downloads,
XShm/dirty-rect presentation, and GPU rendering (a deliberate decision, see roadmap M11). Since
M14a the CSS value layer covers `%`/`rem`, color functions and full named colors (legacy comma
syntax only), logical properties (horizontal-tb), min/max sizes, auto margins, line-height,
text-align (not justify), decorations and visibility; since M14b borders (all styles painted solid,
no percentage widths), box-sizing, overflow:hidden clipping (painting only) and line-through/
overline work — see the known limits in [roadmap.md](roadmap.md). Keep-alive HTTP/1.1,
gzip decoding and per-page image deduplication work since M13; external `<link rel="stylesheet">`
sheets work since M12 (discovered in document order, filtered, resolved against the final URL,
fetched and cascaded with `<style>` and `style` attributes; no `@import`, media queries,
alternate/disabled sheet selection or CSS images yet); pages without links are byte-identical to
M11. Font selection works automatically since M10
(CSS families/weight/style through fontconfig, character-coverage fallback, CJK line breaking);
other scripts are best-effort and bidi/RTL plus full Unicode line breaking are not implemented. PNG
images work since M8a, real fonts since M8b and `https://` since M9 (see the known limits in
[roadmap.md](roadmap.md)); `http://` and `file://` remain supported. The window and `--screenshot`
paint a real page when a URL is given and the M1 placeholder page otherwise; with a page URL the
window is also browsable (links, wheel/keyboard scrolling, Alt+Left/Right history, `<title>`). The
`--dump-*` modes remain the primary debugging path; with system fonts their text output is
machine-dependent by design (tests pin `FONTCONFIG_FILE`).

## Verify the current state (no context needed)

Run the following from the repository root:

```sh
BUILD=build/onboard-check
# Tests are opt-in (the default build only compiles the browser); pass the flag for verification.
cmake -S . -B "$BUILD" -DTOYBROWSER_BUILD_TESTS=ON -DTOYBROWSER_WERROR=ON
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure

"$BUILD"/toybrowser --help
"$BUILD"/toybrowser --dump-dom    "file://$PWD/README.md" | head
"$BUILD"/toybrowser --dump-style  "file://$PWD/README.md" | head
"$BUILD"/toybrowser --dump-boxes  "file://$PWD/README.md" | head
"$BUILD"/toybrowser --headless --screenshot /tmp/toybrowser.ppm && file /tmp/toybrowser.ppm
"$BUILD"/toybrowser --size 640x400 --screenshot /tmp/toybrowser-page.ppm \
    "file://$PWD/README.md" && file /tmp/toybrowser-page.ppm
```

Expected: 85/85 tests pass; building with `-DTOYBROWSER_WERROR=ON` is warning-free on GCC and
Clang; dumps print deterministic trees; screenshots write a binary PPM (the URL form paints the
real page, the URL-less form keeps the placeholder). All headless modes need no display. CMake 3.20
or newer is required; building needs the X11, libpng, freetype, harfbuzz, fontconfig and OpenSSL
development headers (all approved dependencies); only windowed mode needs a running display. When
`CMAKE_BUILD_TYPE` is not given the build defaults to `RelWithDebInfo` (M11); pass
`-DCMAKE_BUILD_TYPE=Debug` for unoptimised debugging. `-ffp-contract=off` is applied on purpose so
layout geometry stays byte-identical across build types and compilers. `CMakePresets.json` packages
the same commands as `default`/`dev` presets, and `.github/workflows/ci.yml` runs the configure,
build, test and install-smoke sequence with both GCC and Clang.

```sh
# External stylesheets (M12): the committed fixture site links a subdirectory sheet
# between two inline <style> elements and has filtered, failing and duplicate links.
"$BUILD"/toybrowser --dump-style \
    "file://$PWD/tests/fixtures/css_site/index.html" | grep -E '#(external|before|after|inline)'
"$BUILD"/toybrowser --size 320x200 --dump-boxes \
    "file://$PWD/tests/fixtures/css_site/index.html" | head
"$BUILD"/toybrowser --size 320x200 --screenshot /tmp/toybrowser-css.ppm \
    "file://$PWD/tests/fixtures/css_site/index.html" && file /tmp/toybrowser-css.ppm
```

Expected: the external rule applies (`#external` gets `color=#123456 background-color=#abcdef
font-size=24px` and its 6/10/12/14 px margins only from `styles/site.css`), document order decides
the same-specificity ties (`#before` green from the linked sheet that follows the first `<style>`,
`#after` blue from the second `<style>`), and the inline `style` attribute still wins (`#inline`
green). The box dump shows the frozen external-rule geometry (`<p>` at x=22 y=14 width=280
height=24, page height 202). `--dump-style` fetches the sheets; `--dump-dom`/`--dump-body` do not.
The tests are offline: committed `file://` fixtures plus an in-process loopback HTTP server
(`tests/support/http_server.h`), never the public network. `style.external_sheets`,
`shell.external_css`, `shell.dump_style_external` and `shell.dump_style_filtered_only` cover
ordering, filtering, redirects, absolute URLs, failure skips and no-dedup request counts.

```sh
# Resource loading (M13): keep-alive reuse, gzip and per-page image dedup are
# covered by offline loopback tests (no public network, no wall-time assertions).
ctest --test-dir "$BUILD" -R '^(net\.keep_alive|net\.gzip|net\.tls_keep_alive|shell\.resource_loading)$' --output-on-failure

# Manual real-page check (network-dependent, not part of the suite): the same
# screenshot stays byte-identical to the pre-M13 binary while loading faster.
"$BUILD"/toybrowser --size 800x600 --screenshot /tmp/toybrowser-m13.ppm https://www.python.org/
```

Expected: the four tests pass deterministically and assert the connection/request counts (one
reused connection for a page + stylesheet + images, one fetch per unique image URL, one retry of a
stale idle connection, gzip decoding); the one-shot `Fetch()` stayed byte-compatible and every
pre-M13 dump/screenshot is unchanged. On the development machine the manual page dropped from
~2.1 s to ~1.0 s with identical pixels (M13 round report); the exact numbers are machine/network
dependent, so do not turn them into test assertions.

```sh
# CSS values & typography (M14a): the committed css_compat_site fixture covers percentages/rem,
# colors, logical properties, min/max sizes, auto margins, line-height, text-align, underline and
# visibility; its legacy.html page uses only pre-M14 CSS and must stay byte-identical.
"$BUILD"/toybrowser --dump-style \
    "file://$PWD/tests/fixtures/css_compat_site/values.html" | tail -n +2 | head
"$BUILD"/toybrowser --size 320x200 --dump-boxes \
    "file://$PWD/tests/fixtures/css_compat_site/boxes.html" | head
"$BUILD"/toybrowser --size 320x400 --screenshot /tmp/toybrowser-css-compat.ppm \
    "file://$PWD/tests/fixtures/css_compat_site/typography.html" && file /tmp/toybrowser-css-compat.ppm
```

Expected: every style line keeps the pre-M14 fields and appends ` min-width= min-height=
max-width= max-height= line-height= text-align= visibility= text-decoration=` (px lengths round,
percentages print with `%`, `auto`/`none` where applicable, line-height prints `normal`, a trimmed
unitless number or `NNpx`). The box dump shows the frozen M14a geometry (percentage widths, auto
centering, min/max clamping, line-height line boxes) and the screenshot paints the aligned/
underlined text. `--dump-boxes`/screenshots of pages that use none of the new properties are
byte-identical to M13; a `--dump-style` difference is limited to the appended fields (the byte-compat
gate normalizes them). Tests: `style.values`, `style.typography`, `layout.style_properties`,
`shell.css_compat`, `shell.dump_style_css_compat`, `shell.css_compat_screenshot`; 80 tests total.

```sh
# Box decorations (M14b): borders, box-sizing, overflow clipping and line-through/overline have
# their own fixture pages; legacy.html and the M14a pages must keep their M14a geometry/pixels.
"$BUILD"/toybrowser --dump-style \
    "file://$PWD/tests/fixtures/css_compat_site/borders.html" | head -n 4
"$BUILD"/toybrowser --size 320x300 --dump-boxes \
    "file://$PWD/tests/fixtures/css_compat_site/borders.html" | head
"$BUILD"/toybrowser --size 320x200 --screenshot /tmp/toybrowser-borders.ppm \
    "file://$PWD/tests/fixtures/css_compat_site/overflow.html" && file /tmp/toybrowser-borders.ppm
```

Expected: border lines print the used widths (0 for none/hidden), style keywords, four resolved
`border-color` values (currentColor resolved to the text color, alpha dropped), `box-sizing=` and
`overflow=`; the box dump includes borders in the boxes (border box = content + padding + borders,
content-box vs border-box widths differ); the overflow screenshot clips the oversized child to the
padding box; decorations paint underline/line-through/overline rows. Pre-M14b pages keep
byte-identical box dumps/PPMs (style dumps differ only in the appended fields, which the byte-compat
gate strips); a comparison against the pre-M14 (M13) binary differs only in the M14a fixture pages.
Tests: `style.borders`, `style.box_model`, `layout.box_model`, extended paint display-list cases,
extended `shell.css_compat`, `shell.dump_style_css_compat_borders`,
`shell.css_compat_screenshot_borders`; 85 tests total.

```sh
# Images (M8a): the committed PNG fixtures render end to end.
"$BUILD"/toybrowser --size 320x200 --screenshot /tmp/toybrowser-img.ppm \
    "file://$PWD/tests/fixtures/img_site/index.html" && file /tmp/toybrowser-img.ppm
"$BUILD"/toybrowser --size 320x200 --dump-boxes \
    "file://$PWD/tests/fixtures/img_site/index.html" | grep '<img>'

# Real fonts: the committed self-authored fixture is deterministic and installed fonts are only
# for manual demos (e.g. --font-family "sans-serif").
"$BUILD"/toybrowser --font tests/fixtures/fonts/toy_test.ttf --size 320x600 \
    --screenshot /tmp/toybrowser-font.ppm "file://$PWD/tests/fixtures/font_site/index.html" \
    && file /tmp/toybrowser-font.ppm
"$BUILD"/toybrowser --font tests/fixtures/fonts/toy_test.ttf --size 320x200 --dump-boxes \
    "file://$PWD/tests/fixtures/font_site/index.html"
"$BUILD"/toybrowser --font-family "sans-serif" --size 400x200 \
    --screenshot /tmp/toybrowser-system-font.ppm "file://$PWD/tests/fixtures/nav_site/index.html"
```

Expected: the image screenshot renders the committed PNG fixtures (flags, alpha, gray, palette) and
the box dump contains `<img> inline` boxes with their intrinsic/CSS sizes; `missing.png` reserves
no box. The `--font` fixture dump keeps the M8b metrics (e.g. `<h1>` text "ABC" width 58 at 32 px).
`--font-family` now follows the computed weight/style, so the UA bold h1 renders a bold face (the
`shell.fonts_dump_boxes_family` test pins that). Without a font flag, pages now use automatic system
fonts (machine-dependent by design); tests pin `FONTCONFIG_FILE` to deterministic fixture/empty
configs, and the empty config keeps the built-in bitmap output for all pre-M10 goldens.

```sh
# HTTPS (M9): deterministic TLS tests use the committed test certificates
# (tests/fixtures/tls/) and in-process loopback servers, never the public network.
ctest --test-dir "$BUILD" -R '^(net\.tls|shell\.https)$' --output-on-failure

# Manual real-site check: network-dependent, not part of the test suite.
"$BUILD"/toybrowser --dump-body https://example.com/ | head
"$BUILD"/toybrowser --size 800x600 --screenshot /tmp/toybrowser-https.ppm \
    https://example.com/ && file /tmp/toybrowser-https.ppm
```

Expected: `net.tls` and `shell.https` pass; a public `https://` page fetches and paints. Certificate,
hostname and timeout failures are ordinary fetch errors (CLI exit 1 for page modes), verification is
always on and there is no insecure mode.

```sh
# Font selection (M10): deterministic tests with the committed fixture fonts and test fontconfig
# configs; the Chinese fixture page is served from disk (file://) and laid out/painted with the
# fixture Han face and real per-character fallback runs.
ctest --test-dir "$BUILD" -R '^(style\.parser|layout\.baseline|layout\.cjk_line_break|shell\.font_selection|shell\.page_language|shell\.cjk_.*|shell\.fonts.*)$' --output-on-failure

# Manual system-font check: no font flags at all. Text uses installed fonts (serif default,
# weight/style, per-character fallback) and is machine-dependent. Chinese sites render with a
# Han fallback face and wrap because CJK text has break units.
"$BUILD"/toybrowser --size 800x900 --screenshot /tmp/toybrowser-zh.ppm \
    "$PWD/tests/fixtures/cjk_site/index.html"
"$BUILD"/toybrowser --size 800x600 --dump-boxes "$PWD/tests/fixtures/nav_site/index.html" | grep 'text "'
```

Expected: all font tests pass deterministically (fixture config: default serif, bold/italic faces,
mixed Latin+Han runs, the committed Chinese page with per-unit boxes, wrapping and painted ink;
empty config: the byte-identical bitmap path). The manual page shows real system text, including
readable Chinese with wrapping.

```sh
"$BUILD"/toybrowser --size 400x200 --screenshot /tmp/toybrowser-nav.ppm \
    "file://$PWD/tests/fixtures/nav_site/index.html" && file /tmp/toybrowser-nav.ppm
```

Window demo (needs a display):

```sh
"$BUILD"/toybrowser --size 320x600 "file://$PWD/tests/fixtures/img_site/index.html"
"$BUILD"/toybrowser --font-family "sans-serif" --size 400x200 \
    "file://$PWD/tests/fixtures/nav_site/index.html"
# img page: check the PNG images are painted; real-font page: check anti-aliased text; nav page:
# click "About page", scroll with the wheel/arrows/PageUp/PageDown/Home/End, Alt+Left/Right for
# history; the title follows <title>.
```

## Next round: M15 — Flexbox / position / float subset (planned; starts only on the maintainer's go-ahead)

**Status:** design sketch only (in [roadmap.md](roadmap.md)); no M15 code or contract exists. M14 is
complete and green, so the starting point is the current HEAD. The M15 scope must be re-confirmed
with the maintainer before starting (it is the largest remaining CSS/layout arc). The HTTP proxy
stays at M16 and the JavaScript subset stays deferred.

- Goal: a small flexbox/position/float subset that makes real page layouts usable: enough of
  CSS position (`position: relative/absolute` with `top/right/bottom/left`, containing-block
  resolution) and floats (`float: left/right`, clearing/line interaction) to place typical page
  chrome, plus a deliberately minimal flexbox subset (row/column containers, `justify-content`,
  `align-items`, `gap`, grow/shrink/basis basics) if the maintainer keeps it in scope; the exact
  slice is frozen at kickoff from real-site frequency evidence (position/float dominate the
  surveyed stylesheets at ~219 declarations, with `display: inline-block/flex/grid/table` ~132).
- Expected shape (freeze before spawning): parser/computed style additions, a positioning/float
  layer in `layout` with a containing-block model, and paint changes for stacking/positioned boxes;
  small separately tested slices with fixtures; pages not using the new properties keep
  byte-identical output; `border-radius`/background images/transforms stay out unless separately
  scoped.
- Out of scope (unless re-scoped): full CSS grid, full flexbox conformance, `position: fixed/
  sticky`, transforms/animations, `border-radius`, CSS variables, at-rules/webfonts.
- Tests: fixtures for each supported property/slice with frozen box geometry and pixels; hit-testing
  and navigation interaction tests for positioned boxes; no regressions; manual real-site check.
- Acceptance: every supported property covered by fixtures; all tests GCC/Clang WERROR green;
  pre-M15 pages byte-identical; navigation/hit-testing still correct; real-site checks show the
  supported layout constructs applying; docs updated.
- After M15: M16 proxy/backlog items (proxy, CSS variables, border-radius, background images,
  `@media`/`@font-face`, etc.) as the maintainer prioritises them.

## Map of the repository

- `src/<module>/` — one CMake library target per module (`base` does not exist yet; see
  [architecture.md](architecture.md) for the module map and dependency direction).
- `src/main.cpp` — CLI parsing only; `shell` orchestrates fetch → parse → style → images → layout →
  paint → dump/present, and `toy/shell/browser.h` owns one browser session (history, scroll, clicks,
  title).
- `src/shell/page.h` — shell-private page loader (final URL, DOM, styles, box tree, decoded images)
  plus the M12 `LoadStyledDocument()` path that fetches and filters the page's external
  `<link rel="stylesheet">` sheets before styling and is shared by `LoadPage` and `--dump-style`;
  since M13 `LoadPage` creates one `net::Client` per load and passes it through page, stylesheet and
  image fetches, and images are deduplicated per resolved URL within the load;
  `src/style/` matches selectors and resolves the wrapper cascade (UA, author `<style>` and linked
  sheets in document order, inline `style`), with `style::LinkedStyleSheet` entries supplied by the
  shell so `style` never fetches anything. Since M14a the computed value model carries
  `Length`/`AutoLength`/`LineHeight`/`TextAlign`/`Visibility`, the value layer supports `%`/`rem`,
  hex/rgb/rgba/hsl/hsla and the CSS named colors, logical properties and min/max/auto constraints,
  and `--dump-style` appends the M14a fields (see the dump contract);
  `src/render/include/toy/render/image.h` — `Image` and the libpng-backed `DecodePng`;
  `src/render/include/toy/render/font.h` — bitmap-font metrics plus the real `Font` API (load,
  metrics, shaped advances; no freetype/harfbuzz types in the header), implemented by
  `src/render/font_ttf.cpp` (plus the private `src/render/font_cache_internal.h` LRU and the M11
  shaped-run/glyph-coverage caches); the display list + rasterizer live in `toy/render/paint.h`,
  and `src/paint/` builds the list from the box tree without rasterizing (M11: viewport culling in
  `src/paint/paint.cpp`; since M14b it also paints background/borders/image/children in that order
  with border FillRects, overflow-hidden padding-box clip pairs and decoration rows). Font selection
  lives in `src/shell/fonts.h`/`fonts.cpp` (`Fonts` pimpl:
  fontconfig matching, run fallback, M11 private resolved-style/metrics/run caches), the only
  fontconfig user; `src/shell/application.cpp` builds it from `--font`/`--font-family` or the
  automatic system default. `src/platform/window.cpp` coalesces X11 Expose/ConfigureNotify bursts
  (M11). `layout` consumes text metrics through a `shell`-supplied callback and has no `render`
  dependency; `src/layout/text_break.cpp` owns the break-unit tokenizer.
- `src/net/` — URLs (`url.cpp`), shared HTTP/1.1 request/response code and the M13 `net::Client`
  keep-alive/gzip implementation (`http.cpp`), and the M9 private transport split:
  `socket_internal.h`/`socket.cpp` (TCP connect + poll helpers),
  `stream_internal.h`/`stream.cpp` (the private `Stream` interface, plain `SocketStream` and
  `ReadUntilEof`), `tls_internal.h`/`tls.cpp` (the OpenSSL-backed `TlsStream`, process-wide
  `SSL_CTX`, verification/handshake loop, SIGPIPE guard). OpenSSL and zlib stay out of the public
  `include/` headers; the only net public API is `toy/net/url.h` + `toy/net/http.h` (now including
  `Client`, whose pimpl keeps the connection pool private).
- `tests/<module>/` — one executable per subject, registered with CTest; new tests use the
  header-only `tests/support/check.h`. Committed fixtures: `tests/fixtures/nav_site/` (three-page
  navigation demo; its "Unsupported page" link is a deliberate unsupported-scheme case so no test
  touches the public network), `tests/fixtures/img_site/` (image pipeline demo),
  `tests/fixtures/font_site/` (real-font + image demo), `tests/fixtures/images/` (hand-generated
  PNGs with documented pixel content), `tests/fixtures/fonts/` (self-authored minimal TTFs:
  regular, bold, italic and a tiny Han face, all with documented metrics/family; the tests never
  use installed fonts), `tests/fixtures/cjk_site/` (Chinese page served from disk, asserting
  per-unit boxes, wrapping and painted ink), `tests/fixtures/css_site/` (M12 external-stylesheet
  fixture site: relative subdirectory sheet, ordering, filtered/failing/duplicate links and a
  documented expected style/geometry/pixel table), `tests/fixtures/css_compat_site/` (M14 CSS
  compatibility pages for values/typography/boxes and M14b borders/overflow/decorations plus a
  legacy page whose output stays M13-identical; its README documents every expected computed value,
  box rectangle, decoration row and pixel), `tests/fixtures/https_site/`
  (HTML+PNG page served over TLS by the shell test) and `tests/fixtures/tls/` (committed test-only
  CA, `localhost` leaf and untrusted leaf certificates/keys; see its README for regeneration).
  The binary fixtures ship their generators next to them: `tests/fixtures/fonts/generate_toy_test.py`,
  `tests/fixtures/fonts/generate_variants.py` and `tests/fixtures/images/generate.py` (stdlib-only,
  deterministic; see the fixture READMEs).
  `tests/support/tls_server.h` is the header-only in-process TLS loopback server shared by
  `net.tls`/`net.http`/`shell.https`, and `tests/support/http_server.h` (M12) is the header-only
  plain-HTTP loopback server; since M13 both can keep a connection open for multiple requests and
  expose connection/request counters (the HTTP one also has `SetForceCloseAfterResponse` for stale
  idle-connection tests), which is how keep-alive behavior is asserted without the public network.
  No test ever contacts the public network.
- Root public files — `README.md`/`README.zh.md` (user-facing entry points), `AI-DISCLOSURE.md`,
  `CONTRIBUTING.md`, `SECURITY.md`, `CMakePresets.json`, `.github/workflows/ci.yml` and
  `docs/assets/` screenshots.
- `docs/` — architecture, roadmap, conventions, team workflow, decisions, dependencies, open
  questions (this file is the snapshot).
- Shared task board — provided by the agent runtime (task create/claim/update with revision); if
  the runtime has none, use `docs/tasks/<milestone>.md` with the same fields. See
  [team-workflow.md](team-workflow.md).
- `README.md` / `README.zh.md` and `docs/zh/` — user-facing entry points (English authoritative).
- `.tmp/` — gitignored scratch space for local notes, fixtures, downloads and test output.

## Frozen contracts (do not change casually)

- CLI modes, flags and exit codes: `0` success, `1` runtime error, `2` usage error. The optional
  positional page URL (`toybrowser [options] [url]`) is used only by window mode and
  `--screenshot`; at most one URL, it must not be combined with a `--dump-*` mode, and no URL keeps
  the M1 placeholder page. M8b adds `--font PATH` and `--font-family NAME`: each needs a non-empty
  argument, they are mutually exclusive, and both are usage errors (exit 2) with
  `--dump-body`/`--dump-dom`/`--dump-style`; they are allowed with window mode, `--screenshot` and
  `--dump-boxes`. A selected font that cannot be loaded/resolved is a runtime error (exit 1).
  Without either flag the browser uses automatic system fonts (M10: computed CSS
  family/weight/style through fontconfig, with character fallback); only when no usable system font
  exists does it degrade to the built-in bitmap font. `--font PATH` keeps one exact face with no
  fallback; `--font-family NAME` forces the preferred family and still allows character fallback,
  and since M10 it also follows the computed weight/style.
- Dump formats (DOM, style, boxes): deterministic, line-oriented, asserted by tests; any change
  needs tests and docs in the same round. The style dump has a `font-family=` field after
  `font-style=` since M10: items joined with ", ", raw when the item matches `[A-Za-z0-9_-]+`,
  otherwise double-quoted with `\` and `"` backslash-escaped. Since M14a every style line also
  appends, in this order, ` min-width= min-height= max-width= max-height= line-height= text-align=
  visibility= text-decoration=`: lengths round to integral CSS px (`12px`) or percentages (`50%`),
  unconstrained min/max print `none`, auto width/height/margins print `auto`, line-height prints
  `normal`, a trimmed unitless number (up to 3 decimals) or `NNpx`, text-align prints
  `left|right|center|justify`; since M14b `text-decoration=` prints `none` or the space-joined
  tokens `underline line-through overline` and the line continues with
  ` border-top-width= border-right-width= border-bottom-width= border-left-width=
  border-top-style= border-right-style= border-bottom-style= border-left-style= border-color=
  box-sizing= overflow=`: border widths are the USED widths in px (0 for none/hidden, rounded),
  styles are the CSS keywords, `border-color` is four space-separated `#rrggbb` values in
  top/right/bottom/left order (currentColor resolved to the text color, alpha dropped),
  box-sizing is `content-box|border-box` and overflow `visible|hidden` (hidden if either axis is
  hidden). All M14b fields are appended after the M14a fields, so the byte-compat normalization
  (strip from ` min-width=`) covers them. The box dump is pre-order lines of
  `<tag> block|inline`, `anonymous block` or `text "..."` followed by `x= y= width= height=`, all
  integers (`std::lround`) of absolute CSS pixels; indent is two spaces per box depth, capped at 64
  levels, and its format is unchanged by M10/M11 (CJK text simply yields one text box per break unit).
- CSS values contract (M14a): `style::Length {value, unit(px/percent)}`, `AutoLength` margins,
  `LineHeight {kind: normal/number/px, value}` and the `TextAlign`/`Visibility` enums. Font-relative
  units resolve in style (`em` against the element's own final font size, `%` font-size against the
  parent, `rem` against the root html element's computed font size; the root's own rem uses the
  16px initial size). Box-property `%` lengths stay unresolved in `ComputedStyle` for layout. Colors:
  `#rgb`/`#rgba`/`#rrggbb`/`#rrggbbaa`, legacy comma-separated `rgb()`/`rgba()`/`hsl()`/`hsla()`,
  the full CSS Color Level 4 named-color set and `transparent`; text color is opaque (alpha
  dropped), background color keeps alpha in 0xAARRGGBB. The `background` shorthand applies only its
  color component (the first parseable color token; other components ignored). Logical properties
  map to physical fields in horizontal-tb: `margin/padding-block-start|end`, `margin/padding-inline-
  start|end`, the `margin/padding-block|inline` shorthands, `inline-size`/`block-size`,
  `min/max-inline-size`/`min/max-block-size`. `line-height`/`text-align`/`visibility`/the three
  `text-decoration` flags are inherited in this MVP (text-decoration propagation is simplified to
  inheritance; underline/line-through/overline are all represented and painted since M14b);
  `justify` is parsed but laid out as left. Unsupported units (`pt/rem` on line-height, `vh/vw`,
  ...), modern space-separated color-function syntax and percentage heights are documented limits
  (roadmap M14a).
- Box decorations contract (M14b): `BorderSide {Length width; BorderStyle style; optional<uint32_t>
  color}` (nullopt = `currentColor`), `BoxSizing` and `Overflow`, plus `UsedBorderWidth`/
  `UsedBorderColor` helpers. Parsed: `border-*-width` px/thin/medium/thick (negative/%/other units
  ignored), all ten styles (none/hidden used width 0; every visible style paints solid),
  `border-*-color` (the M14a color forms incl. alpha/transparent; currentColor stores nullopt), 1–4
  value width/style/color shorthands, per-side and full `border` shorthands with CSS
  reset-to-initial semantics (invalid/duplicate component invalidates the whole declaration and
  performs no reset), `box-sizing`, `overflow` shorthand + `overflow-x/y` (unsupported keywords are
  a no-op; a box clips when either axis is hidden), and the three-flag `text-decoration` reset
  semantics. Borders/box-sizing/overflow are not inherited; the decoration flags inherit. Dump
  formatting is documented in the dump-format bullet above.
- Layout semantics frozen through the layout tests and the M5/M8a/M10 known limits: viewport width
  from `--size`, no margin collapsing, top-aligned lines, word-level text boxes, and line breaking
  at whitespace plus M10 CJK break units (individual CJK characters, simplified punctuation
  prohibitions; Latin words never split, no full UAX#14/dictionary breaking). Since M10 the
  `MeasureText` callback also receives the text's `style::ComputedStyle`; `TextMetrics` carries
  `ascent_px` and each text box carries `baseline_offset_px` = the line's maximum text baseline
  (metadata only, not dumped; since M14a it includes line-height half-leading, and for `normal`
  line-height it equals the M13 maximum ascent). M14a layout contract: `Length::kPercent` box
  lengths resolve against the containing block CONTENT WIDTH (width, min/max-width, all four
  margins and paddings; vertical percentages use the width too, per CSS); percentage heights and
  min/max-heights are unsupported and behave as auto/no-constraint; content widths are clamped
  min-then-max after the specified/auto-width resolution; horizontal `auto` margins distribute the
  remaining space when the width is definite (specified or clamp-resolved), are 0 for an auto width
  and 0 when over-constrained; px min/max-height clamps apply after the content/fixed height; a text
  item's line-height contribution is the px value, number*font-size, or the measured line height for
  `normal`, the line box is as tall as the tallest contribution and half-leading positions the
  shared baseline (`normal` contributes exactly the measured line height, so pre-M14 geometry is
  unchanged); `text-align` shifts every box of a finalized line by the right/center offset after
  line breaking (`justify` lays out as left, overlong lines stay left); inline/replaced percentage
  margins/paddings use the line width, replaced percentage width uses the containing width and
  percentage height is auto. Visibility never changes layout. M14b layout contract: used border
  widths enter the box model — border box = content + padding + borders on each side, content box
  is inset by border + padding. `box-sizing: content-box` (default) keeps width/height as content
  sizes (M14a behaviour) and makes an auto width fill `containing_width - margins - paddings -
  borders`; `box-sizing: border-box` makes width/height the border-box sizes (content = clamp(size -
  paddings - borders, >= 0)), auto width still fills. Px min/max-width/height follow the box-sizing
  (content-box clamps the content box, border-box clamps the border box, then non-negative; min
  before max); percentage sizes/constraints still use the containing content width; auto margins
  distribute `containing_width - margins - final_border_box_width` (CSS 10.4 centering and
  over-constrained 0 as in M14a); fixed/auto height handling follows the same rules (percentage
  heights remain unsupported). Inline elements add horizontal borders to the left/right advances
  and fragment widths (first fragment gets border-left, last border-right; vertical inline borders
  are ignored) and replaced inline elements ignore borders/padding/margins per the M8a limit.
  Overflow and decorations never change layout, and pre-M14b geometry is byte-identical when
  borders are 0/box-sizing unchanged. M8a replaced elements: `LayoutOptions::replaced_sizes` (DOM node id → intrinsic CSS px)
  makes an inline element an atomic inline-level box; CSS `width`/`height` px override the
  intrinsic size, one specified side keeps the intrinsic ratio, the box wraps as a whole, raises
  the line height and is top-aligned; its dump line stays `<tag> inline x= y= width= height=`, it
  has no children, `display:block` replaced elements are unsupported, and margins/padding on
  replaced boxes are ignored. A page without entries keeps its old geometry byte for byte.
- Painting semantics frozen through the M6/M7 tests: display-list commands are integer device pixels
  (`FillRect`, `DrawText`, `DrawImage`, `PushClip`/`PopClip`); fills use the SrcOver formula
  `(src * a + dst * (255 - a) + 127) / 255` over an opaque destination; the initial clip is the
  whole surface and rasterizer work is bounded by the clip; the shell fills the canvas white; paint
  order is box-tree pre-order (background, then image, then children/text); text uses the built-in
  8x16 ASCII glyphs with nearest-neighbour scaling, no anti-aliasing, `?` for non-ASCII, and
  opaque text pixels. M14a painting: a box whose computed `visibility` is hidden emits no
  background/image/text (and no underline) but the walk still descends into its children, so a
  visible descendant paints normally, and layout space is unchanged; a text box with
  `text-decoration: underline` emits one `FillRect(x = round(box.x), y = round(box.y) - scroll_y +
  round(box.baseline_offset_px) + 1, width = round(box.width), height = 1, color = text color)` right
  after that box's text commands, under the same text culling; pages using neither property keep the
  byte-identical pre-M14 command list. M14b painting: element order is background -> borders -> image
  -> children/text. Borders are up to four `FillRect`s from the border-box rectangle in
  top/right/bottom/left order using the resolved `UsedBorderColor` (currentColor -> text color,
  alpha kept), every parsed style painted solid, each rect culled exactly like a background; hidden
  boxes paint no borders. An `overflow:hidden` box (either axis) emits one balanced
  `PushClip`/`PopClip` pair around the whole subtree, clipping to its padding box (integer-rounded
  edges; the pair is emitted even when empty and nested clips nest); clipping is painting-only, so
  the scrollable content height still includes overflow. Text decorations are emitted after a text
  box's text commands in underline -> line-through -> overline order (underline: baseline+1;
  line-through: baseline - max(1, round(font_size/3)); overline: box top), each a 1px `FillRect` in
  the opaque text color, under the same text culling and never for hidden text. Pages without the
  new properties build the byte-identical pre-M14b command list. M7 painting: a non-empty page is wrapped in exactly one
  `PushClip(0, 0, viewport)`/`PopClip` pair and every command's y is translated by `-scroll_y`
  (integer device pixels, x unchanged); a scroll-0 render must stay byte-identical to M6/M7 when
  the page has no images. M8a images: one `DrawImage` per inline replaced box whose node id is in
  the `paint::ImageMap`, emitted after that box's background; the command holds a
  `shared_ptr<const render::Image>`; destination pixel (dx, dy) samples source
  `((dx - x) * image.width / width, (dy - y) * image.height / height)` with integer floor division
  (nearest-neighbour, no interpolation/AA), composited with the same SrcOver formula (alpha 0 no-op,
  alpha 255 writes `0xFF000000 | rgb`, result opaque); an empty image map reproduces the pre-M8a
  command list exactly. M10 real-font text: paint receives a `paint::FontRunsProvider`; for each
  text box it asks for ordered, non-empty runs concatenating exactly to the box text. Each run
  becomes a `DrawTextWithFont` command at x = `round(pen_x)` (pen starts at box.x and advances by
  `font->MeasureTextWidth(run.text, style.font_size_px)` CSS px) and y =
  `round(box.y) - scroll_y + round(box.baseline_offset_px)`; the font is kept by shared reference;
  glyphs are shaped by harfbuzz and rasterized by freetype as 8-bit coverage, placed with the frozen
  per-glyph integer formula (26.6 pen rounded to device pixels, ties away from zero, plus freetype
  bitmap_left/top), composited with the same SrcOver formula using coverage as alpha (result
  opaque), clipped to the current clip rectangle, and the effective size is clamped to
  `kMaxRealFontSizePx` (4096 px). A null/empty provider, or an empty run vector for a box, emits the
  frozen M10-pre bitmap command (`DrawText`, top-of-cell y, opaque unaliased pixels) and must
  reproduce the pre-M10 command list byte for byte. M11 culling: a background `FillRect` and a
  `DrawImage` are omitted when their exact device-pixel destination rectangle does not intersect the
  viewport after the scroll translation; a text box is omitted when its box expanded by
  `ceil(2 * font_size_px)` on every side does not intersect the viewport. Culling happens before
  style/image/font-run lookups, keeps exactly one `PushClip`/`PopClip` viewport pair for a non-empty
  box tree and preserves command order; every omitted command would have painted no pixel, so the
  rendered pixels are byte-identical to an unculled list. The display-list tests now assert this
  culling contract (a command list may legitimately omit never-visible commands); pixels remain the
  frozen contract.
- Render cache contract (M11): `render::Font` keeps private, single-threaded, bounded LRU caches —
  one reusable `hb_font`/`hb_buffer` per font (a single `hb_face`, so harfbuzz's shape-plan cache
  works; the freetype face size is synced with `hb_ft_font_changed`), a shaped-run cache keyed by
  exact 26.6 size + UTF-8 text (16 MiB / 16384 entries) and a glyph-coverage cache keyed by glyph id
  + 26.6 size (32 MiB / 16384 entries, negative results cached). Cache allocation failure is a
  cache miss; eviction never changes pixels, metrics or advances; the rendering formula, clip,
  SrcOver compositing and the bitmap-font path are unchanged; no public header/API changes.

- Font engine contract (M8b): `render::Font` is immutable after loading (pimpl, no
  freetype/harfbuzz type in a public header), loading failures are ordinary errors, face 0 only,
  metrics are design units scaled by `size / unitsPerEm`, advance widths are harfbuzz-shaped 26.6
  sums.
- Font selection contract (M10): `toy/shell/fonts.h` `Fonts` (pimpl) is created once per session as
  `SystemDefault`, `FromFile` or `FromFamily`. `SystemDefault` maps the computed `font_family`
  (initial `serif`)/`font_weight`/`font_style` plus the page language hint (root `<html lang>`) to
  fontconfig, splits a text box's UTF-8 into runs by code-point character coverage (first candidate
  whose charset covers it; uncovered code points use the primary face's `.notdef`), caches candidate
  lists per (families, weight, slant, language) and faces per file, and is shared by the layout
  measurement adapter and the paint run provider so measurement and painting always agree.
  `FromFile` is one exact face (no fallback); `FromFamily` forces the preferred family with
  character fallback and fails with a readable error when unusable. With no usable system font
  (fontconfig init failure, no candidates, no loadable file) `SystemDefault` operates in bitmap
  mode: bitmap metrics and empty runs, so pages still load. Since M11 `Fonts` also keeps private,
  bounded, single-threaded caches: a 16-slot allocation-free resolved-style fast path with stable
  style ids (falls back to a full StyleKey map on collisions), a text-metrics LRU keyed by
  (style id, language, text, exact font size; 100k entries / 32 MiB) and a run LRU keyed by
  (style id, language, text; 100k / 16 MiB) returning (font, offset, length) views into the current
  input. Allocation failure is a miss; eviction never changes results; the bitmap-degradation path
  bypasses the caches. Fontconfig stays shell-only; freetype/harfbuzz stay private to `render`; the
  default browser behavior and the automatic selection were introduced in M10, the caches in M11.
- Font determinism contract (M10): tests never use installed fonts or the machine font
  configuration. They set `FONTCONFIG_FILE` via CTest `ENVIRONMENT` to one of two generated configs:
  a fixture config (`<dir>` = `tests/fixtures/fonts/`, generic families mapped to `Toy Test Font`)
  for automatic selection/fallback tests, and an empty config (no font directories) that forces the
  bitmap path and must reproduce every pre-M10 dump/screenshot byte for byte. `Fonts` caches mean a
  fixed fontconfig configuration and font set produce deterministic runs/metrics; do not add an
  in-code trust/override hook.
- Image decoding contract (`render::DecodePng`): PNG only, via the approved libpng dependency behind
  the interface, output straight-alpha ARGB `render::Image`; accepted sizes are each side at most
  8192 px and at most 16,777,216 pixels in total (inclusive maxima), rejected before allocation;
  failures return false with a human-readable error and leave `*out` untouched; no exceptions; no
  libpng type crosses a public header.
- HTTPS/TLS contract (M9): the URL scheme set is `file`, `http`, `https` (lowercase); `https` uses
  default port 443 and the same RFC 3986 authority/port/path rules as http, and
  `ParseUrl`/`ResolveUrl`/`SerializeUrl` round-trip it. `Fetch` dispatches by scheme; http and https
  share one HTTP/1.1 request/response implementation through a PRIVATE byte-stream abstraction
  (`src/net/stream_internal.h`), and the plain transport keeps the pre-M9 observable behavior and
  output. TLS is OpenSSL (linked PRIVATE to `toy_net`; no OpenSSL type in a public header): one
  process-wide `SSL_CTX` created lazily on the first https fetch from OpenSSL's default verify paths
  (so `SSL_CERT_FILE`/`SSL_CERT_DIR` take effect at that moment), `SSL_VERIFY_PEER` always on, SNI
  sent for DNS hosts (not for IPv4 literals), chain + name verification (`SSL_set1_host` for DNS,
  IP-SAN check for IPv4 literals), and no insecure mode or way to disable verification. TCP connect
  uses `connect_timeout_ms`; the TLS handshake and all TLS reads/writes use `read_timeout_ms`.
  Failures are ordinary fetch errors with readable messages: timeouts contain "timed out",
  certificate verification failures contain "certificate", hostname mismatches contain "hostname".
  A clean `close_notify` and an unclean EOF are both end of stream (truncation is still caught by
  Content-Length/chunked parsing). The TLS path ignores `SIGPIPE` process-wide once on first use;
  library code prints nothing to stdout/stderr. Redirect `Location` values are resolved with
  `net::ResolveUrl` (absolute, relative, root-relative, protocol-relative, scheme-relative), may
  switch between http and https, and file targets are rejected; `max_redirects` is unchanged. No
  ALPN/HTTP2, compression, cookies, HSTS, client certificates, TLS session reuse or HTTP caching.
  The shell needs no API or CLI change: page fetches, links, redirects and images reuse the same
  `net` calls. TLS tests never use the public network: committed test-only certificates
  (`tests/fixtures/tls/`), an in-process TLS loopback server (`tests/support/tls_server.h`) and
  trust supplied only through the CTest `SSL_CERT_FILE` environment for the test CA.
- Shell page loader contract (`src/shell/page.h`, private): after fetching/parsing/styling, load
  every `<img>` in document order (iterative walk), take its first `src`, resolve it against the
  page's final URL, fetch it with `net::Fetch` (http/https/file), decode it, and keep
  `layout::ReplacedSizes` + `paint::ImageMap` keyed by the img DOM node id. Missing/failed/unsupported
  images are skipped silently (no broken-image UI, no stderr, page load still succeeds, the element
  lays out as an empty inline); there is no image cache (one fetch per occurrence) and images are
  fetched even if the element would not be visible. `--dump-boxes` uses the same loader;
  `--dump-dom`/`--dump-style` do not fetch images.
- External stylesheet contract (M12): public `style::LinkedStyleSheet { dom::NodeId link_node;
  std::string text; }` and a two-arg `style::ComputeStyles(document, linked_sheets)`. The style
  module does no I/O: it walks the document once and inserts each caller-supplied sheet at its
  `<link>` element's document position, interleaved with the document's `<style>` elements, so
  equal-specificity ties resolve by document order exactly like the pre-M12 `<style>` handling;
  specificity, `!important`, inheritance and inline `style` declarations are unchanged. The
  one-arg overload stays exactly pre-M12 (empty list, no extra per-element work). The shell owns
  everything else: one iterative pre-order DOM walk discovers `<link>` elements in head or body in
  document order; the first occurrence of each attribute is used; `rel` is split on ASCII
  whitespace and ASCII-lowercased, and a sheet applies only when a `stylesheet` token is present
  and no `alternate` token is present; any `disabled` attribute (any value) skips the link;
  `media` (trimmed, lowercased) applies only when empty/`all`/`screen`; `type` applies only when
  empty/`text/css`; `href` is trimmed, must be non-empty, and is resolved with `net::ResolveUrl`
  against the page's final URL after redirects. Sheets are fetched with `net::Fetch` default
  options (5 s connect / 10 s read / 16 MiB body cap), serially, once per applicable occurrence
  (no deduplication or cache, like images); only status 200..299 bodies are parsed. Every filtered,
  unresolvable, failed or non-2xx sheet is skipped silently: no stderr, no page-load failure, no
  broken-styles UI. The private `shell::LoadStyledDocument()` performs fetch → parse → sheet
  discovery/filtering/fetching → `ComputeStyles`, and is shared by `LoadPage` (window,
  `--screenshot`, `--dump-boxes`) and `--dump-style`; only its page fetch can fail (the out
  parameters are untouched on failure and the caller adds the message prefix). `--dump-body` and
  `--dump-dom` keep their old code paths and fetch no sheets. No CLI flags, exit codes or dump
  formats changed, and a page without `<link>` (or whose links are all skipped) keeps
  byte-identical dumps, exit status and PPM pixels to M11.
- Resource loading contract (M13): public `net::Client` (`toy/net/http.h`, pimpl, single-threaded,
  no concurrency/pipelining) is the keep-alive path. It keeps at most one idle HTTP/1.1 connection
  per origin (`scheme://host:port`) and reuses it; requests carry `Connection: keep-alive` and
  `Accept-Encoding: gzip`. Responses are framed incrementally on the private `Stream`: a bounded
  header read (64 KiB), then exactly the Content-Length body or the full chunked body including
  trailers, with read-ahead bytes kept on the pooled connection for the next request. A connection
  is pooled only when the exchange is persistent (HTTP/1.1 without `Connection: close`, or HTTP/1.0
  with `Connection: keep-alive`) and the body is delimited; a persistent response without a length
  delimiter is a readable error and closes the connection. `Content-Encoding: gzip` is inflated
  through the approved zlib (identity passes through, any other coding is an error), with both the
  raw encoded body and the decoded body bounded by `FetchOptions::max_body_bytes`; headers are kept
  as received. If a pooled connection fails before any response byte arrives it is closed and the
  request is retried once on a fresh connection; timeouts, fresh-connection failures and failures
  after received bytes are never retried. Redirects and `file://` behave like `Fetch()` and use the
  same pool (same-origin hops reuse; cross-origin hops keep an idle connection for their own origin).
  The one-shot free `Fetch()` keeps its exact observable behavior (same request bytes with
  `Connection: close`, no `Accept-Encoding`, same body/timeout/redirect/error semantics and file
  handling); only `Client` negotiates gzip and pools connections.
- Shell page-load contract for M13: `LoadPage()` keeps its signature and creates exactly one
  `net::Client` for the whole load, used for the page fetch, every external stylesheet fetch and
  every image fetch; the Client dies with the call, so no connection or cache survives a page load
  (and `RelayoutPage` still never refetches). Images are deduplicated per page load by the
  serialized resolved URL: each unique URL is fetched and decoded at most once, duplicate `<img>`
  occurrences share the same decoded `render::Image` object and intrinsic size, and a failed
  fetch/decode is remembered for the rest of the load so duplicate broken images are not retried.
  Stylesheet semantics stay the M12 ones (every applicable `<link>` occurrence fetched once; no
  stylesheet dedup/cache). `--dump-style` creates its own Client for page + sheets; `--dump-dom` and
  `--dump-body` keep their old one-shot `Fetch()` paths. No CLI, dump-format, shell-public-API or
  pixel changes: every pre-M13 output is byte-identical.
- Navigation/session contract (`toy/shell/browser.h`): one history entry per successful load,
  failed loads change nothing, every successful load resets the scroll offset to 0, a reference
  that only changes the fragment is a no-op, the title is the first `<title>` text (collapsed,
  falling back to the serialized final URL), and only `http`, `https` and `file` references
  navigate. M8a keeps the
  public methods identical but hides the page (DOM/styles/boxes/images) behind a forward
  declaration, an out-of-line destructor and a `std::unique_ptr`, so the header no longer exposes
  the page data and `Browser` is non-copyable.
- Platform input contract (`toy/platform/window.h`): `kMouseDown` is the primary button in window
  device pixels, `kMouseWheel` carries `wheel_delta` (positive = towards the document end), and
  `kKeyDown` carries the normalized `Key` plus alt/ctrl/shift; `SetTitle` is best effort and never
  fails the window. M11 `WaitEvent` coalescing: a backend may merge consecutive already-queued
  Expose events into one `kExpose`, and consecutive ConfigureNotify events into one `kResize`
  carrying the final size (a size-unchanged burst produces no event and WaitEvent keeps blocking);
  input and close events are never merged, dropped or reordered, and blocking semantics are
  unchanged.
- Module boundaries and dependency direction in [architecture.md](architecture.md).
- `--size` is clamped to 1..8192 per side and at most 16M pixels; decoded images use the same
  inclusive bounds (each side at most 8192 px, at most 16,777,216 pixels in total); bodies are
  capped by `FetchOptions::max_body_bytes` (16 MiB by default); dump indentation is capped at 64
  levels.
- Build contract (M11): when `CMAKE_BUILD_TYPE` is unset a single-config generator defaults to
  `RelWithDebInfo`; first-party targets compile with `-ffp-contract=off` so floating-point layout
  geometry is byte-identical across build types and GCC/Clang. Do not remove the default build type
  (plain builds were 3–7x slower) or the FP flag without re-running the full test, byte-compat and
  cross-compiler gates; keep tests free of wall-time assertions.
- HTML/CSS/JS support is deliberately partial: read the known-limits entries in `roadmap.md`
  instead of assuming spec behavior.

## How a new team starts working

1. Read this file, [docs/README.md](README.md), then `AGENTS.md`.
2. Run the verification commands above; if they differ from this snapshot, treat that as a bug
   and fix the docs or the code.
3. Pick the next milestone from `roadmap.md`; run the round bookkeeping from
   [team-workflow.md](team-workflow.md): intake → contracts → kickoff → parallel work →
   integration → delivery → retro.
4. The Lead owns docs, shared CMake files and commits; implementers claim one task, stay inside
   its write scope, verify locally and report.
5. Ask the user before adding any dependency, downloading code/test data, or changing a locked
   decision in [decisions.md](decisions.md).

## Gotchas that cost the previous team time

- Don't test with a display when a `--dump-*` mode can prove the same thing; everything is
  designed to be headless-first.
- Deeply nested input used to make dumps quadratic; keep any new dump depth-capped and tested.
  Layout is deliberately iterative (10,000 nested divs are a test): do not turn it back into DOM
  recursion.
- Build artifacts and object files must never be committed; use isolated build directories.
- `https` works since M9 (`http`/`https`/`file`); the M9 TLS contract above is frozen. Verification
  is always on: never add an insecure mode, a "skip verification" flag or a public-network test.
- The TLS tests rely on two pieces of infrastructure that must stay test-only: the committed
  certificates in `tests/fixtures/tls/` (a CA plus a `localhost` leaf and an unrelated untrusted
  leaf; they expire in 2046) and the header-only in-process server `tests/support/tls_server.h`.
  Trust is supplied only by the CTest `ENVIRONMENT` property pointing `SSL_CERT_FILE` at the test
  CA — do not `setenv` overrides inside tests and do not use installed/system CAs.
- The process-wide `SSL_CTX` is created lazily on the first `https://` fetch, so the default verify
  paths (including `SSL_CERT_FILE`/`SSL_CERT_DIR`) are read at that moment and later changes in the
  same process are ignored. That is why the CTest environment is the trust mechanism; tests must
  not switch trust stores mid-process.
- The TLS path installs `SIGPIPE` ignore once (process-wide) because OpenSSL's write path may raise
  it; the plain HTTP path uses `MSG_NOSIGNAL`. Do not remove either without a better mechanism.
- OpenSSL failures must stay ordinary `Fetch` errors with readable text (tests assert the
  substrings "timed out", "certificate", "hostname"): never let OpenSSL types or exceptions reach
  a public header or a caller, and never print from library code.
- `SSL_CTX_set_default_verify_paths` plus `SSL_VERIFY_PEER` is the whole trust policy: there is no
  certificate pinning, host allowlist, HSTS or client certificate support, and no HTTP/2/ALPN,
  compression, cookies or proxy. Real sites that require those features will fail or look wrong —
  documented M9 limits, not bugs.
- Redirects now go through `net::ResolveUrl`, so relative, protocol-relative and scheme-relative
  `Location` values are followed and http<->https switches are allowed; `file` redirect targets are
  rejected. The pre-M9 behavior (relative Location = error) is intentionally gone.
- The nav fixture's "Unsupported page" link deliberately uses `ftp://example.com/` so the
  unsupported-scheme click case cannot reach the network now that https navigates; do not change it
  back to an https link. The navigation test still hardcodes that link's coordinates.
- M9 has the same byte-compatibility gate as M8b: every pre-M9 dump and screenshot for http/file
  inputs must stay `cmp`-identical to the M8b baseline, and GCC/Clang must produce byte-identical
  PPMs. Rerun a `git worktree` baseline after touching `net`/painting.
- Real-site checking is manual: public `https://` pages render through the limited HTML/CSS subset,
  so they look incomplete; that is expected. Do not commit downloaded page content, captured
  screenshots, proxy/network details or site-specific fixtures.
- The HTML/CSS/URL parsers are error-tolerant by design; they never fail on malformed input, so
  "it didn't error" is not evidence of correctness — compare dumps with the golden tests.
- `--dump-boxes` uses `--size`'s width as the viewport and ignores the height; heights come from
  the content. `layout` must never include a `render` header — text metrics only arrive through the
  callback.
- Rendering is integer-pixel and anti-aliasing-free on purpose: changing rounding, glyph scaling or
  paint order will change golden pixel tests. GCC and Clang must produce byte-identical PPMs —
  `cmp` two builds' screenshots as a cheap determinism gate.
- Paint is documented as "background, then children" in pre-order; there are no stacking contexts
  yet, so do not add alpha/overlap semantics without a contract change.
- The canvas is filled white by the shell; `html`/`body` background-color paints its element box
  only (no background propagation to the canvas) — a deliberate M6 limit, not a bug.
- Rasterizer work must stay bounded by the clip/surface area: adversarial font sizes used to hang
  or overflow the old glyph loop; keep the finite-size guard, the visible-range iteration and the
  bounded cell math when touching `render::DrawDisplayList`.
- Scratch files (fixtures, screenshots, notes, downloads) go in the gitignored `.tmp/` directory at
  the repository root, never scattered through the tree.
- Mouse/keyboard behavior cannot be unit-tested headlessly. The real-window smoke test launches the
  browser on a display, finds the *client* window with `xwininfo -root -tree` (frames and clients
  both carry the title; capture the exact-size client), captures it with ImageMagick
  `import -window <id>`, and injects clicks/wheel/keys with a throwaway `.tmp` XSendEvent helper
  (libX11 only, never committed). Re-run it after touching the event loop.
- Click coordinates are window device pixels: add the scroll offset before `layout::HitTest`.
  Hit-testing returns the last pre-order box, which may be an anonymous or text box, so `shell`
  walks box parents to a DOM origin and then DOM ancestors to find an `href`.
- The navigation test hardcodes the fixture's link coordinates taken from a
  `toybrowser --size 400x200 --dump-boxes` run; changing the fixture or the layout constants means
  updating those coordinates and the expected content height.
- `paint` always wraps a non-empty page in one viewport clip pair now; older statements about M6
  emitting no clip commands are obsolete. The clip does not change scroll-0 pixels.
- Scroll resets to 0 on every successful load and fragment links are deliberate no-ops (documented
  M7 limits); do not 'fix' either without a contract change.
- The window loop used to rebuild the layout and display list on expose, resize and scroll; M11
  changed that: expose repaints only, resize relayouts only on an actual size change, and the X11
  backend coalesces queued Expose/ConfigureNotify bursts. Do not reintroduce per-expose relayout or
  per-event relayout without evidence; the display list is still rebuilt per frame (cheap after
  culling) and there is deliberately no cached display list.
- The built-in 8x16 glyph table is hand-authored and its shapes are reviewed visually: there is
  deliberately no frozen glyph-bitmap/fingerprint test. Changing a glyph is a visual change —
  render a sample and look at it before committing.
- libpng is a required build dependency now (`find_package(PNG REQUIRED)` in
  `src/render/CMakeLists.txt`); a build without its development headers fails at configure time.
  Do not hardcode versions or paths, and keep `png.h` out of public headers.
- PNG decoding goes through `render::DecodePng` only. Sizes are validated before allocation, the
  output is straight-alpha ARGB, and failures are ordinary errors. The hand-generated fixtures in
  `tests/fixtures/images/` document their exact pixel content; keep the README and the tests in
  sync if a fixture ever changes.
- Image-less dumps and screenshots must stay byte-identical to M7 and image PPMs must be identical
  between GCC and Clang: `cmp` a fresh build against a `git worktree` of the previous commit at the
  end of any round that touches painting. This M7 gate has already caught drift cheaply.
- Layout and paint must receive images keyed by the same DOM node ids (`layout::ReplacedSizes` and
  `paint::ImageMap`); `shell` builds both from one load. `RelayoutPage` reuses the stored sizes and
  decoded images and never refetches on resize.
- A display list image command owns a `shared_ptr<const render::Image>`; a built list may outlive
  the page, so never keep a raw image pointer in a command.
- The image tests (`shell.images*`, and the dump regexes for `tests/fixtures/img_site/`) hardcode
  geometry taken from `toybrowser --size 320x200 --dump-boxes`, the same way the navigation test
  hardcodes link coordinates: changing the fixture, the replaced-element layout or the text metrics
  means updating those constants in the same change.
- `shell.images_screenshot` is a `cmake -P` helper that only checks exit status, the P6 header and
  the file size; the pixel content of the same page is asserted by `shell.images`.
- `--dump-boxes` now loads and decodes the page's images too (that is how `<img>` boxes appear);
  image failures are still skipped, so the dump never fails because of a broken image.
- Image loading has no cache and no lazy loading: every `<img>` occurrence is fetched, even one
  hidden by `display:none` or far below the viewport. That is a documented M8a MVP limit, not a
  bug; only add caching/lazy loading with a contract change.
- M10 keeps the hard byte-compatibility gate through the bitmap path: with `FONTCONFIG_FILE`
  pointing at the generated empty config (no fonts), every pre-M10 dump and screenshot must stay
  `cmp`-identical to the M9 baseline; the only intentional difference is the new `font-family=`
  field in the style dump, which the gate normalizes away before comparing. Rerun a `git worktree`
  baseline of the previous commit after any painting/font/layout change.
- Font tests must use the committed self-authored fixtures (`tests/fixtures/fonts/`: regular,
  bold, italic, Han) and never installed fonts. Automatic-selection tests set `FONTCONFIG_FILE` to
  the generated fixture config (only the fixture fonts, generic families mapped to `Toy Test Font`);
  the bitmap-compatibility tests set it to an empty config. Installed fonts are for manual demos
  only. `tests/fixtures/fonts/README.md` documents the exact config recipe (including the
  fontconfig normalization of `sans-serif` to the family `sans`).
- Do not assert exact freetype coverage bytes in tests: anti-aliased pixels are library-version
  dependent. `render.paint` asserts ink regions, AA presence, clip bounds, no-ops and determinism;
  `shell.fonts` asserts page regions and the frozen layout geometry. GCC and Clang must still
  produce byte-identical PPMs on the same machine/library versions.
- The real-font effective size is clamped to `kMaxRealFontSizePx` (4096 px) in metrics, shaping and
  rasterization. Sizes above behave exactly like 4096, so a hostile CSS `font-size` cannot make
  freetype allocate an unbounded glyph bitmap; the bitmap path keeps its own guard.
- `Font` mutates its shared freetype face size internally between measure/draw calls: never share
  one `Font` across threads (the MVP is single-threaded). `DrawTextWithFont` with a null font
  appends no command; the bitmap path is `DrawText` with a null font and top-of-cell y.
- The shell's font fixture geometry (`shell.fonts*` dump regexes) is hardcoded from
  `toybrowser --font tests/fixtures/fonts/toy_test.ttf --size 320x200 --dump-boxes`; changing the
  fixture, its metrics or the font pipeline means updating those constants in the same change, as
  with the M7/M8a fixture geometry.
- `--font-family` forces the preferred family for every text (character fallback still applies)
  and since M10 also follows the computed `font-weight`/`font-style`, so the UA bold `h1` gets the
  bold face under `--font-family` (the M8b expectation moved accordingly). Unknown families fall
  back through fontconfig instead of erroring; face 0 of the matched file is used and TTC/face index
  is ignored. `--font PATH` remains one exact face with no fallback.
- `src/render/CMakeLists.txt` now `find_package`s freetype (config/module) and harfbuzz (CONFIG,
  target `harfbuzz::harfbuzz`); `src/shell/CMakeLists.txt` finds fontconfig (target
  `Fontconfig::Fontconfig`). All stay PRIVATE, no versions/paths are hardcoded.
- `src/net/CMakeLists.txt` finds OpenSSL (`OpenSSL::SSL` + `OpenSSL::Crypto`) PRIVATE. The
  `net.tls`/`net.http`/`shell.https` test executables also link OpenSSL because they compile the
  in-process test server; the fixture paths they pass are derived from `CMAKE_CURRENT_SOURCE_DIR`
  (CMake-generated, never hardcoded). Keep OpenSSL out of public headers and do not add versions,
  paths or vendoring.
- M10 automatic font selection depends on process-global fontconfig state created on first use:
  `FONTCONFIG_FILE`/`FONTCONFIG_PATH` are read when `Fonts` first resolves a style, so tests set
  the CTest `ENVIRONMENT` property before the process starts. Do not switch font configuration
  mid-process; do not add an in-code trust/override hook.
- `Fonts` caches candidate faces per (families, weight, slant, language) and loaded faces per file
  path, and is shared by layout measurement and painting: never share one `Fonts` across threads
  (single-threaded MVP) and never build a second selection for the same page, or measurement and
  paint can disagree. `--font`/`--font-family` selection failures are runtime errors (exit 1);
  automatic system-font failure degrades to the bitmap path instead of failing the page.
- The layout text tokenizer (`src/layout/text_break.cpp`) is a simplified line-break model: CJK
  code points are individual break units with closing/opening punctuation glued per small rules;
  there is no full UAX#14, no dictionary breaking for Thai/Khmer/Lao, no hyphenation, and
  punctuation in a different text node cannot glue to the previous unit. Latin words never split.
  Extending this needs tests and a roadmap/doc update.
- One CJK break unit means one kText box: a Chinese page produces many boxes (one per character),
  which is fine for the MVP (bounded by the 16 MiB body cap) but do not treat box counts as
  word counts in tests or documentation. Box dumps of CJK pages are large by design.
- CJK fixtures (`tests/fixtures/cjk_site/`, `layout.cjk_line_break`) freeze per-unit geometry and
  painted ink; `tests/fixtures/fonts/toy_cjk.ttf` contains synthetic block-art Han glyphs, not real
  glyph designs, and the M10 fixture fonts' italic face has italic metadata but un-sheared outlines.
- With system fonts (no font flags) the browser's text output is machine-dependent by design: dumps
  and screenshots are not reproducible across machines unless `FONTCONFIG_FILE` is pinned. That is
  expected for a real browser; tests must always pin it.
- `paint::BuildDisplayList` takes a `FontRunsProvider`; an empty/null provider must still produce
  the frozen pre-M10 bitmap command list (tests assert this). Real-font runs must concatenate
  exactly to the box text and be placed with the frozen pen/rounding rules, otherwise layout
  measurement and painting disagree.
- Fontconfig is already an approved dependency; M10 adds no new dependency. It is used only by
  `src/shell/fonts.cpp`; keep `fontconfig/*.h` out of every public header and do not add versions,
  paths or vendoring. The fixture fontconfig configs live in the build tree (generated by
  `tests/shell/CMakeLists.txt`); the committed font fixtures are the only font files tests may use.
- M11 culling means a display list may legitimately omit commands whose painted area cannot
  intersect the viewport. Pixels are the frozen contract; the paint display-list tests assert both
  the culling rules and pixel equality with an unculled reference. The text margin is
  `ceil(2 * font_size_px)` on every side; if a future font or paint path can paint further than
  that from its box, increase the margin and re-run the culled-vs-unculled equivalence tests.
- M11 render caches are private `Font` state: single-threaded, bounded LRU (16 MiB shaped runs,
  32 MiB glyph coverage per Font) and allocation-failure-is-a-miss. Do not share a `Font` across
  threads (unchanged) and do not add public cache APIs; every cache must remain output-neutral, and
  eviction tests must compare fresh vs warm results, never wall time.
- M11 shell `Fonts` caches are also private and bounded (16-slot style fast path, 32 MiB metrics,
  16 MiB runs). The bitmap-degradation path deliberately bypasses them; a cache must never change a
  measurement or run, and `MeasureText`/`Runs` must be safe to call in any order.
- X11 event coalescing only merges consecutive already-queued Expose/ConfigureNotify events; input
  and close events are never merged, dropped or reordered. Window behavior cannot be unit-tested
  headlessly: re-run the M11 real-window smoke (`.tmp` X11 helpers, not committed) after touching
  the event loop, and verify no stale frame after resize and a clean WM_DELETE exit.
- The M11 benchmark harness (`.tmp/perf_stages.cpp`, `.tmp/perf_bench.cpp`,
  `.tmp/run_m11_bench.sh`, `.tmp/gen_baseline_refs.sh`, the scroll_shot tool) is deliberately not
  committed: tests must not assert wall time, and the pages are generated stress fixtures. Recreate
  it when measuring a change; keep numbers machine-relative and document the before/after in the
  round's report. If a committed benchmark is ever wanted, make it an optional non-CTest target.
- Do not regress the default build configuration: removing the RelWithDebInfo default costs 3-7x on
  the bitmap path, and removing `-ffp-contract=off` risks cross-build/compiler pixel drift. The
  full test suite, byte-compat gate and GCC/Clang PPM equality are the checks.
- M11 did not add a GPU backend on purpose: measurements showed fill rate was not the bottleneck
  (system-font raster time was identical at 800x600 and 1600x1000); revisit only if rasterization
  still exceeds the frame budget after the caches/culling, and treat any GPU/XShm work as a new
  dependency decision requiring the user's approval.
- M12 external stylesheets are fetched on the page-load path (window, `--screenshot`,
  `--dump-boxes`) and by `--dump-style`, so a page with `<link rel="stylesheet">` can now make extra
  network or `file://` requests during a test or demo. Tests must use the committed `file://`
  fixtures or the in-process `tests/support/http_server.h` loopback server, never the public
  network; `--dump-dom`/`--dump-body` must keep fetching no sheets (verify when touching
  `application.cpp`).
- The M12 link filter is deliberately tiny: first attribute occurrences only, `rel` token list with
  `stylesheet` and without `alternate`, no `disabled`, `media` only empty/`all`/`screen`, `type` only
  empty/`text/css`, `href` trimmed and resolved against the final page URL, only 200..299 bodies,
  every skipped sheet silent, one fetch per occurrence (no dedup/cache). Do not add `@import`,
  media queries, alternate/disabled selection, parameterised `type` values, `url(...)` resources or
  a stylesheet cache without a contract change: `media="not print, ..."` (used by real sites such as
  python.org) being skipped is a documented MVP limit, not a bug.
- `style` must stay free of I/O: it only parses the texts the shell supplies and orders them by
  `<link>` DOM node id (`style::LinkedStyleSheet`, two-arg `ComputeStyles`). The one-arg overload
  must stay byte-identical to M11, including the no-links fast path; the shell keeps the
  discovery/filter/fetch policy in `LoadStyledDocument`, and only a page fetch may fail the load
  (out parameters stay untouched on failure; sheet problems never fail it).
- Stylesheet fetches are serial with the default `FetchOptions` (5 s connect / 10 s read, 16 MiB
  body cap), so a page with many slow or dead sheets can delay the load; that is M13's resource
  pipeline problem — do not add ad-hoc parallel/threaded fetching in the shell.
- `tests/fixtures/css_site/` freezes expected computed values, box geometry and painted pixel counts
  in its README and in `shell.external_css`/`shell.dump_style_*`; changing the fixture, the filter
  rules or the style dump output means updating the fixture README, the style tests and the shell
  tests together (same rule as the nav/img/font fixtures). Keep one external sheet in a
  subdirectory, one `<link>` inside `<body>`, and the document-order ordering page when editing it.
- `tests/support/http_server.h` and `tests/support/tls_server.h` are test-only header-only
  infrastructure. Since M13 they keep a connection open for multiple requests (the HTTP server
  closes when the response says `Connection: close` or a test enables `SetForceCloseAfterResponse`)
  and expose connection/request counters for deterministic keep-alive assertions. Keep production
  code out of them, never use them from the browser, and assert counts rather than wall time.
- M13 `net::Client` is single-threaded and owns the keep-alive pool: never share a Client across
  threads, never pipeline requests, and keep the one-idle-connection-per-origin rule. `Client` must
  frame responses incrementally (bounded head, then exactly Content-Length or chunked body with
  trailers); never switch it back to read-until-EOF, because an unread body byte would corrupt the
  next response on the pooled connection. A persistent response without a length delimiter is an
  error by contract. Statuses such as 1xx/204/304 are not specially handled yet (documented limit).
- The one-shot `Fetch()` must stay byte-compatible: same request bytes (`Connection: close`, no
  `Accept-Encoding`), same read/parse/timeout/redirect/file semantics. Do not "upgrade" Fetch
  callers to keep-alive or gzip behavior; use `Client` for that. The shell creates one Client per
  `LoadPage` (page + stylesheets + images) and it dies with the load: do not cache connections,
  Clients or images across page loads without a contract change.
- M13 gzip support is deliberately narrow: only `Content-Encoding: gzip`/`identity` are accepted,
  both raw and decoded bodies stay under `max_body_bytes`, headers are preserved, and the free
  `Fetch()` never advertises or decodes gzip. Tests build gzip fixtures in memory with zlib; no
  test may use the public network or assert wall time.
- M13 image deduplication is per page load and keyed by the serialized resolved URL (one fetch and
  decode per unique URL, failures remembered; no over-broad sharing of distinct URLs). Stylesheet
  fetches intentionally stay one per applicable `<link>` occurrence (M12 semantics) — do not add
  stylesheet dedup or cross-load lazy loading/caching without re-scoping the round.
- Real-page performance checks are manual and machine/network dependent: measure before/after with
  the same page and record numbers in the round report; never add wall-time assertions to CTest.
  zlib is linked PRIVATE to `toy_net` through CMake (`find_package(ZLIB REQUIRED)`); keep zlib types
  out of public headers and do not hardcode versions/paths.
- M14a `--dump-style` appended fields are a frozen format change: the byte-compat gate strips
  everything from ` min-width=` to end of line before comparing pre-M14 style dumps
  (`sed -E 's/ min-width=.*$//'`), while box dumps/PPMs must still `cmp` byte-identically. Adding
  further trailing style fields means updating the gate normalization, the style dump contract and
  the handoff/architecture docs in the same round.
- Percentage heights, `min/max-height` percentages, `line-height` in `rem`, `justify`, text alpha
  and modern space-separated `rgb()`/`hsl()` syntax remain deliberate limits after M14a/M14b — do
  not "fix" them ad hoc; a scope change needs the roadmap updated. Logical properties are
  horizontal-tb only; named colors are the full CSS Color 4 set but color functions only support
  legacy comma syntax. M14b added borders/box-sizing/overflow/line-through/overline; the remaining
  decoration/layout limits are listed below.
- M14a layout invariants: percentages of box properties resolve against the containing block
  CONTENT WIDTH (including vertical margins/padding), never against height; `auto` margins center
  only when the width is definite (specified or clamp-resolved) and are 0 when over-constrained;
  width clamping is min-then-max; `normal` line-height must keep the pre-M14 geometry byte for byte
  (half-leading 0); text alignment shifts the finalized line's boxes only (never affects line
  breaking) and `justify` behaves as left. The `baseline_offset_px` of text boxes is metadata used by
  real-font painting; bitmap painting still top-aligns glyph cells, so a large line-height shows
  extra space below bitmap glyphs and an underline sits at baseline+1 (real-font path centers via
  half-leading). Do not change the box dump format.
- M14a paint invariants: a hidden box must still be descended into so a visible descendant paints;
  underline geometry/color/order/culling is frozen by the paint tests; pages without the new
  properties must keep the byte-identical pre-M14 display list. `tests/fixtures/css_compat_site/`
  freezes expected computed values, box geometry and pixels in its README plus
  `shell.css_compat`/`shell.dump_style_css_compat`; changing the fixture, layout semantics or paint
  formulas means updating all of them together (same rule as the nav/img/font/css fixtures).
- Real-site CSS checks stay manual: keep page content/screenshots in `.tmp/`, never commit them or
  put them in CTest; the surveyed property frequencies (borders ~385 declarations, border-radius
  ~121, position/float ~219, media queries, etc.) are scratch evidence for scope decisions, not a
  contract.
- Cross-compiler coverage matters for new test helpers: GCC accepted `tests/layout/`'s unused
  anonymous-namespace constants while Clang `-Werror` rejected them (M14a round); always run both
  compilers on the full test tree.
- M14b border/box-model invariants: `UsedBorderWidth` is the only width that participates in layout
  and painting (0 for none/hidden; the stored initial width is medium 3px but invisible). Borders
  are drawn from the border-box rectangle with the frozen top/right/bottom/left formulas and
  per-rect viewport culling; every border style paints solid (dashed/dotted/double/groove/ridge/
  inset/outset patterns are not implemented), percentage border widths are ignored, and border
  color alpha is kept in paint but dropped in the style dump (`#rrggbb`, currentColor resolved to
  the text color). `box-sizing` changes width/height *and* px min/max interpretation (content-box
  clamps content, border-box clamps the border box); auto widths fill containing minus
  margins/paddings/borders and auto margins center against the final border box. Inline vertical
  borders and replaced-element borders/padding/margins stay ignored (M5/M8a limits).
- M14b overflow/decorations invariants: `overflow` clips painting only — no scrollbars/horizontal
  scrolling, `ContentHeight` still includes overflowing boxes, and `overflow-x`/`overflow-y: hidden`
  both clip (either axis hidden means clipped, documented MVP simplification). Clipping always
  emits one balanced PushClip/PopClip pair per clipping box (padding box, integer-rounded edges),
  even for an empty subtree, so the display-list shape is deterministic. Decoration rows are
  emitted after the text commands in underline -> line-through -> overline order with the frozen
  formulas; text-decoration style/color components are ignored (rows use the opaque text color,
  1px). Pages without the new properties must keep the byte-identical pre-M14b display list — the
  paint tests include a hand-built reference list.
- The M14b style dump appends a second block of fields after the M14a fields
  (`border-*-width/style`, `border-color`, `box-sizing`, `overflow`); the byte-compat gate's
  `sed -E 's/ min-width=.*$//'` normalization already covers them, and `text-decoration=` grew from
  `none|underline` to `none` or space-joined tokens. Any further dump field must keep the
  append-only order and be normalized by the gate in the same round.
- `tests/fixtures/css_compat_site/` now freezes borders/overflow/decorations pages too; changing
  the border formulas, clip box, decoration rows, box-sizing rules or fixture CSS means updating the
  fixture README, the paint/layout/style tests and the shell e2e assertions together (same rule as
  the earlier fixture sites). Real-site evidence stays in `.tmp/` and never enters the repo/CTest.
- Fixture generators are committed next to the fixtures they produce (`tests/fixtures/fonts/generate_toy_test.py`,
  `tests/fixtures/fonts/generate_variants.py`, `tests/fixtures/images/generate.py`). The font
  generators reproduce the committed TTFs byte for byte; the PNG generator reproduces the documented
  pixel content (exact PNG bytes may follow the local zlib version). Never hand-edit a binary fixture:
  change the generator, regenerate, run the full suite, and update the fixture README/tests in the
  same change. `__pycache__` is gitignored by the root `.gitignore`.
