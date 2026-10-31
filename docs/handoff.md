# Handoff — current state and takeover guide

This is the snapshot for a team taking over the repository. The canonical reading order is in
[docs/README.md](README.md); the hard rules are in [AGENTS.md](../AGENTS.md).

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
| Review round | Fixed unbounded `--size`, file body cap, dump indentation; 20 CTest tests |

**Next planned milestone: M10 — HTTP proxy (planned, not started).** M9 (most-basic HTTPS) shipped;
the user approved OpenSSL for TLS on 2026-10-31 and it is linked since M9 (recorded in
[dependencies.md](dependencies.md)). M10 adds an HTTP proxy (absolute-form for `http`, `CONNECT`
for `https`) behind `FetchOptions`, which is what makes real-site testing practical on restricted
networks. The JavaScript subset was shelved by the user.

Not implemented yet: proxies (planned M10), JavaScript (deferred),
external stylesheets, borders, horizontal scrolling/scrollbars, hover/active link styling and form
controls, and CSS-driven font selection (`font-family`/`font-weight`/`font-style`, fallback chains,
`@font-face`). PNG images work since M8a, real TTF text since M8b (opt-in) and `https://` since M9
(see the known limits in [roadmap.md](roadmap.md)); `http://` and `file://` remain supported. The
window and `--screenshot` paint a real page when a URL is given and
the M1 placeholder page otherwise; with a page URL the window is also browsable (links,
wheel/keyboard scrolling, Alt+Left/Right history, `<title>`). The `--dump-*` modes remain the
primary debugging path.

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

Expected: 57/57 tests pass; building with `-DTOYBROWSER_WERROR=ON` is warning-free on GCC and
Clang; dumps print deterministic trees; screenshots write a binary PPM (the URL form paints the
real page, the URL-less form keeps the placeholder). All headless modes need no display. CMake 3.20
or newer is required; building needs the X11, libpng, freetype, harfbuzz, fontconfig and OpenSSL
development headers (all approved dependencies); only windowed mode needs a running display.

```sh
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
no box. The fixture-font box dump shows real-font metrics (e.g. `<h1>` text "ABC" width 58 at 32 px
instead of the bitmap font's 48); without a font flag every page keeps the bitmap output.

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

## Next round: M10 — HTTP proxy (planned; starts only on the user's go-ahead)

**Status:** design sketch only (in [roadmap.md](roadmap.md)); no M10 code exists. M9 is complete and
green, so the starting point is the current HEAD.

- Goal: fetch `http://` through an HTTP proxy using absolute-form request targets, and `https://`
  through a proxy `CONNECT` tunnel followed by the normal TLS handshake. The browser's own proxy
  support is what makes real-site testing practical on restricted networks.
- Expected shape (contracts are frozen at the start of that round): `--proxy host:port` plus the
  standard `http_proxy`/`https_proxy` environment fallback, kept inside `net` behind
  `FetchOptions`; no new third-party dependency is expected; in-process fake-proxy tests (no
  public network) and byte-identical output when no proxy is configured.
- Non-goals until re-scoped: SOCKS proxies, proxy authentication, bypass lists, PAC.

After M10 the umbrella roadmap is green; the remaining work is the documented backlog (more image
formats, CSS font selection, external stylesheets, etc.) and any new user priorities.

## Map of the repository

- `src/<module>/` — one CMake library target per module (`base` does not exist yet; see
  [architecture.md](architecture.md) for the module map and dependency direction).
- `src/main.cpp` — CLI parsing only; `shell` orchestrates fetch → parse → style → images → layout →
  paint → dump/present, and `toy/shell/browser.h` owns one browser session (history, scroll, clicks,
  title).
- `src/shell/page.h` — shell-private page loader (final URL, DOM, styles, box tree, decoded images);
  `src/render/include/toy/render/image.h` — `Image` and the libpng-backed `DecodePng`;
  `src/render/include/toy/render/font.h` — bitmap-font metrics plus the real `Font` API (load,
  metrics, shaped advances; no freetype/harfbuzz types in the header), implemented by
  `src/render/font_ttf.cpp`; the display list + rasterizer live in `toy/render/paint.h`, and
  `src/paint/` builds the list from the box tree without rasterizing. `src/shell/application.cpp`
  owns font selection (`--font`/`--font-family`, the only fontconfig user). `layout` consumes text
  metrics through a `shell`-supplied callback and has no `render` dependency.
- `src/net/` — URLs (`url.cpp`), shared HTTP/1.1 request/response code (`http.cpp`), and the M9
  private transport split: `socket_internal.h`/`socket.cpp` (TCP connect + poll helpers),
  `stream_internal.h`/`stream.cpp` (the private `Stream` interface, plain `SocketStream` and
  `ReadUntilEof`), `tls_internal.h`/`tls.cpp` (the OpenSSL-backed `TlsStream`, process-wide
  `SSL_CTX`, verification/handshake loop, SIGPIPE guard). OpenSSL types stay out of the public
  `include/` headers; the only net public API is `toy/net/url.h` + `toy/net/http.h`.
- `tests/<module>/` — one executable per subject, registered with CTest; new tests use the
  header-only `tests/support/check.h`. Committed fixtures: `tests/fixtures/nav_site/` (three-page
  navigation demo; its "Unsupported page" link is a deliberate unsupported-scheme case so no test
  touches the public network), `tests/fixtures/img_site/` (image pipeline demo),
  `tests/fixtures/font_site/` (real-font + image demo), `tests/fixtures/images/` (hand-generated
  PNGs with documented pixel content), `tests/fixtures/fonts/` (self-authored minimal TTF with
  documented metrics/family; the tests never use installed fonts), `tests/fixtures/https_site/`
  (HTML+PNG page served over TLS by the shell test) and `tests/fixtures/tls/` (committed test-only
  CA, `localhost` leaf and untrusted leaf certificates/keys; see its README for regeneration).
  `tests/support/tls_server.h` is the header-only in-process TLS loopback server shared by
  `net.tls`/`net.http`/`shell.https`; no test ever contacts the public network.
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
  `--dump-boxes`. A selected font that cannot be loaded/resolved is a runtime error (exit 1);
  without either flag the built-in bitmap font is used (the pre-M8b behavior).
- Dump formats (DOM, style, boxes): deterministic, line-oriented, asserted by tests; any change
  needs tests and docs in the same round. The box dump is pre-order lines of
  `<tag> block|inline`, `anonymous block` or `text "..."` followed by `x= y= width= height=`, all
  integers (`std::lround`) of absolute CSS pixels; indent is two spaces per box depth, capped at 64
  levels.
- Layout semantics frozen through the layout tests and the M5/M8a known limits: viewport width from
  `--size`, no margin collapsing, top-aligned lines, word-level text boxes, ASCII-only line
  breaking. M8a replaced elements: `LayoutOptions::replaced_sizes` (DOM node id → intrinsic CSS px)
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
  opaque text pixels. M7 painting: a non-empty page is wrapped in exactly one
  `PushClip(0, 0, viewport)`/`PopClip` pair and every command's y is translated by `-scroll_y`
  (integer device pixels, x unchanged); a scroll-0 render must stay byte-identical to M6/M7 when
  the page has no images. M8a images: one `DrawImage` per inline replaced box whose node id is in
  the `paint::ImageMap`, emitted after that box's background; the command holds a
  `shared_ptr<const render::Image>`; destination pixel (dx, dy) samples source
  `((dx - x) * image.width / width, (dy - y) * image.height / height)` with integer floor division
  (nearest-neighbour, no interpolation/AA), composited with the same SrcOver formula (alpha 0 no-op,
  alpha 255 writes `0xFF000000 | rgb`, result opaque); an empty image map reproduces the pre-M8a
  command list exactly. M8b real-font text: when the session selected a font, each text box becomes
  a `DrawTextWithFont` command (x = pen origin, y = baseline in device pixels, baseline =
  `round(box.y) - scroll_y + round(font->AscentPx(size))`), the font is kept by shared reference,
  glyphs are shaped by harfbuzz and rasterized by freetype as 8-bit coverage, placed with the frozen
  per-glyph integer formula (26.6 pen rounded to device pixels, ties away from zero, plus freetype
  bitmap_left/top), composited with the same SrcOver formula using coverage as alpha (result
  opaque), clipped to the current clip rectangle, and the effective size is clamped to
  `kMaxRealFontSizePx` (4096 px). With no font selected the frozen bitmap command list is emitted
  unchanged (`DrawText`, top-of-cell y, opaque unaliased pixels).

- Real-font contract (M8b): `render::Font` is immutable after loading (pimpl, no freetype/harfbuzz
  type in a public header), loading failures are ordinary errors, face 0 only, metrics are design
  units scaled by `size / unitsPerEm`, advance widths are harfbuzz-shaped 26.6 sums. The shell
  resolves one font per run (`--font` file or fontconfig's best match for `--font-family`, with
  fallback for unknown families) and shares it with layout metrics and painting; one face applies
  to the whole session (no per-element CSS font selection yet). Fontconfig is a shell-only
  dependency; freetype/harfbuzz are private to `render`.
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
  fails the window.
- Module boundaries and dependency direction in [architecture.md](architecture.md).
- `--size` is clamped to 1..8192 per side and at most 16M pixels; decoded images use the same
  inclusive bounds (each side at most 8192 px, at most 16,777,216 pixels in total); bodies are
  capped by `FetchOptions::max_body_bytes` (16 MiB by default); dump indentation is capped at 64
  levels.
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
- The window loop rebuilds the layout and display list on expose, resize and scroll. That is
  correct and deliberately simple for the MVP; only optimize it with evidence.
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
- M8b adds a hard byte-compatibility gate: without `--font`/`--font-family`, every pre-M8b dump and
  screenshot must stay `cmp`-identical to the M8a baseline; the real-font path is entirely opt-in.
  Rerun a `git worktree` baseline of the previous commit after any painting/font change.
- Real-font tests must use the committed self-authored fixture `tests/fixtures/fonts/toy_test.ttf`
  and never installed fonts; the fontconfig CLI test points `FONTCONFIG_FILE` at a generated config
  whose only `<dir>` is the fixture fonts. Installed fonts are for manual demos only (any
  fontconfig family works, e.g. `sans-serif`).
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
- `--font-family` uses fontconfig's normal semantics: an unknown family returns its best match and
  does not error, face 0 of the matched file is used, and TTC/face index is ignored. The M8b MVP
  supports one font per session; per-element CSS `font-family`/weight/style is future work.
- `src/render/CMakeLists.txt` now `find_package`s freetype (config/module) and harfbuzz (CONFIG,
  target `harfbuzz::harfbuzz`); `src/shell/CMakeLists.txt` finds fontconfig (target
  `Fontconfig::Fontconfig`). All stay PRIVATE, no versions/paths are hardcoded.
- `src/net/CMakeLists.txt` finds OpenSSL (`OpenSSL::SSL` + `OpenSSL::Crypto`) PRIVATE. The
  `net.tls`/`net.http`/`shell.https` test executables also link OpenSSL because they compile the
  in-process test server; the fixture paths they pass are derived from `CMAKE_CURRENT_SOURCE_DIR`
  (CMake-generated, never hardcoded). Keep OpenSSL out of public headers and do not add versions,
  paths or vendoring.
