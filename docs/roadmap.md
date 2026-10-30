# Roadmap

Each milestone is a minimal vertical slice: small, buildable, demoable and tested. A milestone is
done when its demo command works, new logic has tests, and the docs are still accurate.

## M0 — Design and skeleton (current)

- Documentation, buildable CMake skeleton, stub executable, toolchain smoke test.
- Done when `cmake --build` and `ctest` pass and the user has accepted the docs.

## M1 — Window, event loop, first pixels

- `platform`: a window backend (window, close/resize, input events) that presents an ARGB surface.
- `render`: `Surface`, solid rectangle fill, PPM writer. Headless mode: `--screenshot`.
- Done when a solid-color page area appears in a window, the same content can be dumped headless
  without a display, and the headless path is covered by a test.
- The X11 client library needs approval first (see `dependencies.md` and `open-questions.md`); the
  dependency-free headless backend goes first regardless.

## M2 — Fetch a URL

- URL parsing (scheme, host, port, path) for `http` and `file`.
- HTTP/1.1 GET over TCP: status line, headers, `Content-Length` and chunked bodies, basic
  redirects and timeouts.
- `--dump-body <url>`.
- Done when a local page is fetched and dumped, tested against an in-process TCP server. No TLS.

## M3 — HTML to DOM

- Tokenizer and tree builder for a deliberately small subset, plus basic error tolerance (never
  crash on malformed input).
- `--dump-dom`.
- Done when fixture pages produce expected DOM dumps.

## M4 — CSS and computed style

- Tokenizer/parser for rules, declarations and simple selectors; UA + author cascade; inheritance
  for inherited properties.
- `--dump-style`.
- Done when fixtures resolve to expected computed values.

## M5 — Layout

- Box tree (block, inline, anonymous boxes), block width/margins/padding/borders, line breaking
  with bitmap font metrics.
- `--dump-boxes`.
- Done when box dumps for fixtures match expected geometry.

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

## Rules

- Do not start a milestone before the previous milestone's demo works.
- Never block a milestone on a pending dependency approval: ship the headless or
  own-implementation slice, then upgrade when approval arrives.
- Any milestone can be re-scoped by the user. MVP beats completeness, always.
