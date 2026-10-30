# Roadmap

Each milestone is a minimal vertical slice: small, buildable, demoable and tested. A milestone is
done when its demo command works, new logic has tests, and the docs are still accurate.

**Current status: M0–M6 are done; M7 (navigation) is next.** For a snapshot and takeover guide, read
[handoff.md](handoff.md).

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
  (1 CSS px = 1 device px); no borders, images or real TTF fonts (M8); the canvas is always filled
  white by the shell, and `html`/`body` `background-color` paints only its element box (no canvas
  background propagation); paint order is pre-order with no stacking contexts/z-index; text is
  ASCII bitmap glyphs only (other code points render `?`, and links are colored but not underlined
  because `text-decoration` is not in the property set); overflow is clipped by the surface only —
  scrolling and navigation arrive in M7; the rasterizer is bounded by the clip area, including for
  adversarial font sizes.

## M7 — Navigation (next)

- Clickable links, scrolling, back/forward history, window title.
- Done when a small local multi-page site can be browsed.

## M8 — Real fonts and images (needs approval)

- TTF text and `<img>` decoding, either with approved libraries or with our own implementations.
- Done when a page with real font text and at least one image renders.

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
