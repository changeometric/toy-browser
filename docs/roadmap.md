# Roadmap

Each milestone is a minimal vertical slice: small, buildable, demoable and tested. A milestone is
done when its demo command works, new logic has tests, and the docs are still accurate.

**Current status: M0–M4 are done; M5 (layout) is next.** For a snapshot and takeover guide, read
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

## M5 — Layout (next)

- Box tree (block, inline, anonymous boxes), block width/margins/padding. Borders are not in the
  M4 property set and are deferred to a later milestone.
- Line breaking using text metrics: `layout` takes a measurement callback (standard types only)
  supplied by `shell`; the built-in bitmap font stays in `render`, so `layout` does not depend on
  `render`. The bitmap-font decision is already locked (real TTF at M8); the M5 intake only settles
  the callback API and ASCII coverage.
- `--dump-boxes`.
- Done when box dumps for fixtures match expected geometry, the `layout` module is registered in
  the shared CMake lists, and all existing tests keep passing.

## M6 — Paint: first real page

- Display list from the box tree, rasterizer for rectangles and text, clipping; wire it into the
  window.
- Done when a local HTML page with headings, paragraphs and links renders in the window, with a
  golden screenshot test.

## M7 — Navigation

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
- **Test helper consolidation** — the per-file `Check`/`g_failures` test harness is duplicated in
  every test executable. Introduce a header-only `tests/support/check.h` when the next test file is
  added (M5); no framework dependency.

## Rules

- Do not start a milestone before the previous milestone's demo works.
- Never block a milestone on a pending dependency approval: ship the headless or
  own-implementation slice, then upgrade when approval arrives.
- Any milestone can be re-scoped by the user. MVP beats completeness, always.
- Headless-first: any visible or observable result must also be inspectable without a window (a
  dump command or a screenshot), so the project can be developed and tested entirely headless.
