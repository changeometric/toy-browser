# Roadmap

Each milestone is a minimal vertical slice: small, buildable, demoable and tested. A milestone is
done when its demo command works, new logic has tests, and the docs are still accurate.

**Current status: M0–M7 and M8a are done; M8b (real fonts, libraries approved) is next.** For a
snapshot and takeover guide, read [handoff.md](handoff.md).

## M0 — Design and skeleton ✅ (done)

- Documentation, buildable CMake skeleton, stub executable, toolchain smoke test.
- Done when `cmake --build` and `ctest` pass and the user has accepted the docs.

## M1 — Window, event loop, first pixels ✅ (done)

- `render`: `Surface`, solid rectangle fill, PPM writer.
- `platform`: X11 window backend (open/close, resize, expose, key events, present an ARGB surface),
  graceful failure without a display.
- `shell` + CLI: placeholder page, window event loop, `--headless --screenshot`, `--size`.
- Acceptance reached: window shows the placeholder page, the same frame can be dumped headless
  with no display, and `render.surface`, `render.ppm`, `platform.window_requires_display`,
  `shell.headless_screenshot` all pass under GCC and Clang with warnings as errors.

## M2 — Fetch a URL ✅ (done)

- URL parsing for absolute `http` and `file` URLs: percent decoding, default ports, no base
  resolution yet.
- `net` module: blocking HTTP/1.1 GET over POSIX sockets, one request per connection, connect and
  read timeouts, `Content-Length` and chunked bodies, redirects (301/302/303/307/308), a 16 MiB
  body cap; `file://` reads the local file.
- CLI: `--dump-body <url>` writes the raw body to stdout. Everything is headless; no display needed.
- Acceptance reached: byte-identical dumps for `file://` and for a loopback HTTP server; error paths
  exit non-zero; `net.url`, `net.http`, `net.file`, `shell.dump_body_file` and
  `shell.dump_body_missing_file` pass under GCC and Clang with warnings as errors. No TLS.
- Known M2 limits: a non-2xx response is still a successful fetch (the status is exposed in the API;
  error pages and navigation arrive later), and the shell-level end-to-end test covers `file://`
  while HTTP is covered by the in-process-server tests.

## M3 — HTML to DOM ✅ (done)

- `dom` module: self-written tokenizer and stack-based tree builder over an arena DOM, with error
  tolerance (malformed input never fails), a deterministic dump format, and roughly 40 golden dump
  cases plus entity tests.
- `--dump-dom <url>`: fetch through `net`, parse, dump the DOM to stdout; fully headless.
- Acceptance reached: `dom.parser`, `dom.entities`, `shell.dump_dom_file` and
  `shell.dump_dom_missing_file` pass under GCC and Clang with warnings as errors; a local HTTP page
  round-trips fetch → parse → dump.
- Known M3 limits (deliberate MVP subset): no HTML5 insertion modes (no implied end tags except at
  EOF, no foster parenting, no adoption agency), doctype only before `<html>`, no implicit head
  content routing (a `<title>` without an explicit `<head>` lands in body), comments are kept
  verbatim (so a source comment's spaces show up in the dump), whitespace text nodes are preserved
  with simple routing, entities are a small named set plus numeric forms (semicolon required,
  lowercase `x` only), no `<template>`, SVG or MathML namespaces, and input is assumed to be UTF-8.

## M4 — CSS and computed style ✅ (done)

- `css` module: own tokenizer/parser for rules, declarations and simple selectors (type, class, id,
  universal, descendant/child combinators, comma groups), skipping at-rules and unsupported
  syntax, tolerant of malformed input.
- `style` module: embedded UA stylesheet parsed by our own CSS parser, selector matching,
  UA/author/inline cascade with specificity and `!important`, inheritance, and the supported
  property set (display, color, background-color, font-size px/em, font-weight, font-style,
  margin/padding longhands and 1-4 value shorthands, width/height auto or px/em).
- `--dump-style <url>`: deterministic per-element computed-style tree on stdout, fully headless.
- Acceptance reached: `css.parser`, `style.cascade`, `style.dump`, `shell.dump_style_file` and
  `shell.dump_style_missing_file` pass under GCC and Clang with warnings as errors (20 tests
  total).
- Known M4 limits: no external `<link>` stylesheets (needs URL resolution and fetching; later), no
  at-rule contents, no pseudo-classes/elements, no attribute selectors, no `rgb()`/`hsl()`/`calc()`,
  `inherit`/`initial` keywords or CSS variables, no `%`/`pt`/`rem` units, no `margin: auto`, and
  named colors use the standard values (e.g. green = #008000).

## M5 — Layout ✅ (done)

- `layout` module: arena `BoxTree` with absolute CSS-pixel geometry (border-box rectangles; `float`
  geometry, integers in dumps), block and inline boxes, anonymous blocks, block width/margins/
  padding, auto and fixed heights, line breaking with a caller-supplied measurement callback,
  inline-element fragmentation, `<br>`, and a deterministic box dump (indent capped at 64 levels).
- `render`: built-in monospace bitmap-font metrics (`toy/render/font.h`) on an 8x16 design grid at
  a 16 px em. M5 ships metrics only; glyph painting arrives with M6.
- `shell`/CLI: `--dump-boxes <url>` fetches, parses, styles, lays out and dumps; the viewport width
  is `--size`'s width (default 800). Fully headless.
- Acceptance reached: `render.font_metrics`, `layout.block`, `layout.inline`, `layout.line_break`,
  `layout.dump`, `layout.deep_nesting`, `shell.dump_boxes_file`, `shell.dump_boxes_size`,
  `shell.dump_boxes_missing_file`, `shell.dump_boxes_missing_url` and `shell.dump_boxes_conflict`
  pass under GCC and Clang with warnings as errors (31 tests total, deterministic and headless).
- Known M5 limits (MVP simplifications): no margin collapsing (a parent's auto height includes its
  in-flow children's margins); no baseline alignment (boxes on one line are top-aligned and share
  the line height); horizontal inline margins/padding only (vertical inline margins/padding and
  inline width/height are ignored); no `text-align`/`line-height`/`vertical-align`/`white-space`
  support, so `<pre>` collapses whitespace like everything else; line breaks happen only at ASCII
  whitespace (non-ASCII text is one word: no Unicode line breaking, hyphenation or emergency
  breaking); no percentage units; and layout paints nothing yet.

## M6 — Paint: first real page ✅ (done)

- `render`: flat display list (`FillRect`, `DrawText`, `PushClip`/`PopClip`) and a deterministic
  software rasterizer — SrcOver rectangle fill, integer clip stack, and a hand-authored 8x16 ASCII
  bitmap font drawn with nearest-neighbour cell scaling (no anti-aliasing, no font dependency).
- `paint` module: turns the box tree into a display list — element backgrounds first (pre-order),
  then one text draw per text box using the computed color/font size. It does not rasterize, and
  `render` still knows nothing about HTML/CSS.
- `shell`/CLI: optional positional page URL (`toybrowser [options] [url]`) used by window mode and
  `--screenshot`; the page is fetched, parsed, styled, laid out at the surface width, painted and
  presented/written as a PPM. Without a URL both modes keep the M1 placeholder page.
- Acceptance reached: `render.paint`, `paint.display_list`, `shell.screenshot_page` and the
  positional-URL CLI tests pass under GCC and Clang with warnings as errors (37 tests total); a
  pixel end-to-end test proves the red heading/black paragraph geometry on a 200x100 surface, and
  GCC/Clang render byte-identical PPMs; the window shows the same page.
- Known M6 limits: all painting is integer device pixels with no anti-aliasing or subpixel layout
  (1 CSS px = 1 device px); no borders or real TTF fonts (images arrived in M8a, real fonts are
  M8b); the canvas is always filled white by the shell, and `html`/`body` `background-color` paints
  only its element box (no canvas background propagation); paint order is pre-order with no stacking
  contexts/z-index; text is ASCII bitmap glyphs only (other code points render `?`, and links are
  colored but not underlined because `text-decoration` is not in the property set); overflow is
  clipped by the surface only — scrolling and navigation arrive in M7; the rasterizer is bounded by
  the clip area, including for adversarial font sizes.

## M7 — Navigation ✅ (done)

- `shell::Browser`: one headless browser session owning the current page (final URL, DOM, styles,
  box tree), the navigation history, the vertical scroll offset and painting, so clicks, history,
  scrolling and titles are testable without a window.
- `net::ResolveUrl`: RFC 3986 resolution of relative references against the current page's final
  URL (http/file subset; file results drop the query/fragment this MVP cannot carry).
- `layout`: `HitTest` (the box painted last that contains a point) and `ContentHeight` (scrollable
  extent); `paint` wraps the page in a viewport `PushClip`/`PopClip` pair and translates every
  command by the scroll offset.
- `platform`: normalized input events (left click in window pixels, wheel notches, mapped keys with
  modifiers) and `SetTitle`; the X11 backend maps buttons/keysyms and publishes
  `_NET_WM_NAME`/`WM_NAME`.
- `shell` event loop: link clicks navigate, the wheel/arrow/PageUp/PageDown/Home/End keys scroll,
  Alt+Left/Right navigate history, and the window title follows `<title>` (falling back to the
  URL). `--screenshot` renders one frame through the same session at scroll 0; the `--dump-*` modes
  are unchanged and headless.
- A committed three-page fixture site (`tests/fixtures/nav_site/`) is browsable in the window and
  drives the headless `shell.navigation` test.
- Acceptance reached: `net.url_resolution`, `layout.hit_test`, the updated `paint.display_list`,
  `shell.navigation` and every pre-existing test pass under GCC and Clang with warnings as errors
  (40 tests total); scroll-0 screenshots and all existing renders are byte-identical to M6; a local
  multi-page site can be browsed in the window (click, back/forward, scroll, title).
- Known M7 limits: vertical scrolling only (no horizontal scroll, no scrollbars); a wheel notch is
  a fixed 3 lines (48 px) and scrolling is integer device pixels; navigation reloads the page (no
  cache), so Back/Forward reset the scroll offset to 0 instead of restoring it; fragment navigation
  does not scroll to anchors (fragment-only links are no-ops); only `http`/`file` links navigate
  (unsupported schemes report an error on stderr); a failed load keeps the current page and prints
  an error on stderr; no hover/active link styling, cursor changes, forms or in-page error pages.

## M8a — PNG images ✅ (done)

- `render`: `Image` (straight-alpha ARGB, size caps) and `DecodePng()` wrapping the approved libpng
  dependency behind a frozen interface (no libpng types leave the module; failures are ordinary
  errors; sizes are rejected before allocation). The display list gains `DrawImage` with shared
  image ownership, a frozen nearest-neighbour integer source mapping and the existing SrcOver/clip
  semantics; image-less pages produce exactly the pre-M8a command list.
- `layout`: inline elements with a caller-supplied intrinsic size (`LayoutOptions::replaced_sizes`,
  keyed by DOM node id) become atomic inline replaced boxes: CSS `width`/`height` (px) override the
  intrinsic size, one specified side keeps the intrinsic ratio, the box wraps as a whole, raises
  the line height and is top-aligned. `layout` still has no render dependency and pages without
  entries keep their old geometry and `<tag> inline` dump format.
- `paint`: emits one `DrawImage` per inline replaced box whose node id has a decoded image, after
  its background and before its children; empty image maps keep the M7 list.
- `shell`: a private page loader (final URL, DOM, styles, boxes, replaced sizes and image map)
  resolves each `<img src>` against the page's final URL, fetches it with the existing
  `net::Fetch` (http/file only), decodes it with `render::DecodePng`, and skips failed/missing
  images silently (no broken-image UI, no error output, no image cache). `shell::Browser` owns the
  page and its images, paints them, and re-lays-out on resize without refetching; `--dump-boxes`
  uses the same loader, so reserved image boxes appear in the box dump. The CLI is unchanged.
- Fixtures: hand-generated PNG files with documented pixel content
  (`tests/fixtures/images/`) and a committed `tests/fixtures/img_site/` demo page.
- Acceptance reached: `render.image`, `layout.replaced`, `shell.images`, the image CLI checks and
  every pre-existing test pass under GCC and Clang with warnings as errors (46 tests total);
  image-less dumps and screenshots are byte-identical to M7; GCC and Clang produce byte-identical
  image PPMs; the window shows the image page.
- Known M8a limits: PNG only (whatever libpng converts to 8-bit RGBA; no JPEG/GIF/WebP/SVG), no
  `background-image`, `srcset`, `alt` text or broken-image indicator, no image cache (a duplicate
  URL is fetched once per `<img>`), HTML `width`/`height` attributes are ignored (CSS px only),
  `display:block` replaced elements and margins/padding on replaced elements are unsupported,
  replaced boxes are top-aligned (no baseline alignment, as in M5), scaling is nearest-neighbour
  without anti-aliasing, images are fetched even when their element is not visible (no lazy
  loading), only `http`/`file` image URLs work (no `https`), and `--dump-dom`/`--dump-style` do not
  fetch images.

## M8b — Real fonts (next; libraries approved)

- TTF text: load a font file, use freetype for metrics and glyph rasterization and harfbuzz for
  shaping; fontconfig may discover installed fonts so a page can ask for a family instead of a
  hand-supplied file. The approved dependencies (freetype, harfbuzz, fontconfig) are recorded in
  [dependencies.md](dependencies.md); development packages must be installed by the user where
  missing.
- `layout` keeps consuming text metrics through the existing `MeasureText` callback; `paint`/`render` gain the baseline/advance information needed for real glyph placement and anti-aliased
  coverage text, while the built-in bitmap font stays the fallback path so all pre-M8b output that
  does not opt into a real font is unchanged. The exact font-selection contract and the AA-text
  contract change are frozen at the start of the round.
- Tests must stay deterministic and must not depend on installed fonts: a committed/generated font
  fixture drives the tests, while manual demos may use any installed TTF.
- Done when a page with real TTF text and at least one image renders (the original M8 acceptance),
  with all existing tests, GCC/Clang warning-free builds and the M8a image behavior still green.

## M9 — JavaScript subset (optional, large)

- Lexer, parser, small interpreter, DOM bindings and event handlers for a tiny ES5-like subset.
- Done when a fixture page's script mutates the DOM and the change gets painted.

## M10+ — Opportunistic

More CSS (flex, tables), forms, better text handling, performance, conformance suites. Each item
starts only after the previous slice is clean and the user agrees on priority.

## Backlog — planned follow-ups

- **Proxy support (M2.1)** — fetch `http://` URLs through an HTTP proxy: a proxy setting in
  `FetchOptions`, a `--proxy host:port` CLI flag and `http_proxy` environment fallback; requests to
  a proxy use absolute-form request targets. Tests use an in-process fake proxy. Deferred by the
  user on 2026-10-31: implement only when real-site testing needs it, or when it is cheap and
  clearly useful.
- **TLS / `https://`** — deferred by the user on 2026-10-31; OpenSSL is not approved. Reaching
  sites such as Google needs `https`, and proxy support alone is not enough for them.
- **More image formats and background images** — JPEG/GIF/WebP, `background-image`,
  `srcset`, an image cache and lazy loading; scheduled after M8a on demand, blocking no milestone.
- **Test helper consolidation** — the per-file `Check`/`g_failures` test harness is still
  duplicated in the older test executables. The header-only `tests/support/check.h` (interface
  target `toy_test_support`) was introduced in M5 and is used by the new tests; migrate the older
  files only when they are touched. No framework dependency.

## Rules

- Do not start a milestone before the previous milestone's demo works.
- Never block a milestone on a pending dependency approval: ship the headless or
  own-implementation slice, then upgrade when approval arrives.
- Any milestone can be re-scoped by the user. MVP beats completeness, always.
- Headless-first: any visible or observable result must also be inspectable without a window (a
  dump command or a screenshot), so the project can be developed and tested entirely headless.
