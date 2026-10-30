# Roadmap

Each milestone is a minimal vertical slice: small, buildable, demoable and tested. A milestone is
done when its demo command works, new logic has tests, and the docs are still accurate.

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

## Backlog — planned follow-ups

- **Proxy support (M2.1)** — fetch `http://` URLs through an HTTP proxy: a proxy setting in
  `FetchOptions`, a `--proxy host:port` CLI flag and `http_proxy` environment fallback; requests to
  a proxy use absolute-form request targets. Tests use an in-process fake proxy. Deferred by the
  user on 2026-10-31: implement only when real-site testing needs it, or when it is cheap and
  clearly useful.
- **TLS / `https://`** — deferred by the user on 2026-10-31; OpenSSL is not approved. Reaching
  sites such as Google needs `https`, and proxy support alone is not enough for them.

## Rules

- Do not start a milestone before the previous milestone's demo works.
- Never block a milestone on a pending dependency approval: ship the headless or
  own-implementation slice, then upgrade when approval arrives.
- Any milestone can be re-scoped by the user. MVP beats completeness, always.
- Headless-first: any visible or observable result must also be inspectable without a window (a
  dump command or a screenshot), so the project can be developed and tested entirely headless.
