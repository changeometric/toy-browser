# Architecture

MVP-oriented design: the thinnest thing that makes each milestone work. Nothing here is
speculative — abstractions are added when a real slice needs them.

## Module map

| Module | Responsibility | May depend on | First needed |
| --- | --- | --- | --- |
| `base` | Small shared utilities (geometry, logging, UTF-8), only when a need appears | — | when first needed |
| `platform` | Window, input events, event loop, presenting pixel surfaces | `base` | M1 |
| `render` | Display list, software rasterizer, built-in bitmap font (metrics and glyphs), PNG image decoding (approved libpng), real-font engine (approved freetype + harfbuzz) | `base` | M1 |
| `net` | URL parsing, sockets, HTTP/1.1 fetch over plain TCP or verified TLS (approved OpenSSL), `file://` reader | `base` | M2 |
| `dom` | HTML tokenizer, tree builder, DOM arena | `base` | M3 |
| `css` | CSS tokenizer, rule/declaration parser, selectors | `base` | M4 |
| `style` | Cascade and computed styles, default stylesheet | `dom`, `css` | M4 |
| `layout` | Box tree, block/inline layout, line breaking (word units plus CJK break units) | `dom`, `style`, a text-measurement callback from `shell` | M5 |
| `paint` | Display list construction from the box tree (element backgrounds and text) | `dom`, `style`, `layout`, `render` | M6 |
| `js` | Small JavaScript engine and DOM bindings (optional, deferred by the user) | `dom`, `base` | deferred |
| `shell` | Navigation, pipeline orchestration, window chrome, automatic font selection (approved fontconfig matching, run fallback, font cache) | all | M1 |

Dependencies point downward only; no cycles. `render` knows nothing about HTML/CSS (it consumes
display lists); `paint` knows the DOM/style/layout data but never rasterizes; `platform` knows
nothing about page content (it blits pixels and reports input). Only `shell` sees all modules and
wires them together. `base` does not exist yet: M1–M6 modules use only the standard library and
standard POSIX interfaces, and the module appears when a shared utility is actually needed.

New modules are registered in the explicit lists in the root and `tests` `CMakeLists.txt`; the Lead
adds the name once. `--dump-*` modes and exit codes (0 ok, 1 runtime error, 2 usage error) are
frozen interfaces.

## One page load, end to end

1. `shell` receives a URL (CLI argument or a link click).
2. `net` fetches it: bytes, final URL, status.
3. Bytes are decoded to UTF-8 (MVP: assume UTF-8, strip a BOM; other encodings later).
4. `dom` parses the HTML into a `Document`.
5. `css` parses style sheets (`<style>` and `style=` attributes first; external CSS later).
6. `style` resolves the cascade into a style tree.
7. `shell` walks the DOM for `<img>` elements and, for each first `src`, resolves it against the
   page's final URL, fetches it and decodes it with `render::DecodePng`; failures are skipped (M8a).
8. `layout` turns the style tree into a box tree with geometry, using the shell-supplied intrinsic
   sizes for replaced elements.
9. `paint` builds a flat display list from the box tree (backgrounds, images, text) and `render`
   rasterizes it into a `Surface`.
10. `platform` presents the surface (window blit, or PPM dump in headless mode).

MVP keeps the whole chain synchronous and single-threaded. Every stage takes plain data and
returns plain data, so each one can be exercised through a `--dump-*` command in tests.

Networking (M2, TLS in M9): `net` owns URLs, sockets and HTTP. Fetching is a blocking HTTP/1.1 GET
— one request per connection, connect and read timeouts, `Content-Length` or chunked bodies,
redirects (resolved with `net::ResolveUrl` since M9, so relative and protocol-relative `Location`
values work) and `Connection: close` — behind `Fetch()`, so it can become non-blocking later
without changing callers. `net` is the only module allowed to touch sockets; page code never sees
them. Proxy support (HTTP proxy first) is planned as M10 behind `FetchOptions` and stays inside
`net`; it needs no new dependency.

TLS / HTTPS (M9): `net` accepts `https` URLs (default port 443) and fetches them through the
approved OpenSSL library behind the unchanged blocking `Fetch()`. Plain TCP and TLS share one
HTTP/1.1 implementation: `src/net/http.cpp` builds requests and parses responses over a private
`internal::Stream` interface (`write all` + `read some` until EOF), implemented by a plain
`SocketStream` and a `TlsStream`; the transport is chosen by the URL scheme in one place
(`src/net/stream_internal.h`, `socket_internal.h`, `tls_internal.h` — all private, no OpenSSL type
in a public header). A process-wide `SSL_CTX` is created lazily on the first https fetch from
OpenSSL's default verify paths (the standard `SSL_CERT_FILE`/`SSL_CERT_DIR` overrides apply at that
moment), with `SSL_VERIFY_PEER` always on, per-connection SNI for DNS hosts, and chain + hostname
verification (`SSL_set1_host` for DNS names, IP-SAN verification for IPv4 literals). There is no
insecure mode. TCP connect uses `connect_timeout_ms`; the TLS handshake and all TLS I/O use
`read_timeout_ms` and run over the existing non-blocking socket + `poll` loops; timeouts,
certificate and hostname failures are ordinary readable `Fetch` errors. A clean `close_notify` and
an unclean EOF both end the stream (truncation is still caught by Content-Length/chunked), and the
TLS path ignores `SIGPIPE` once so a dead peer cannot kill the browser. No ALPN/HTTP/2,
compression, cookies, HSTS, client certificates, TLS session reuse or caching is implemented; the
shell needs no API or CLI change because it already goes through
`ParseUrl`/`ResolveUrl`/`Fetch`.

HTML and DOM (M3): `dom` owns the tokenizer, the simplified tree builder and the arena DOM. Parsing
is error-tolerant and always produces one `html` element with `head` and `body` children; the
tokenizer covers the supported subset (elements and attributes, comments, doctype, raw-text and
RCDATA elements, a small entity set) and never fails on malformed input. `DumpDocument()` is the
deterministic textual form used by `--dump-dom` and the parser tests. Nodes are addressed by integer
ids (no owning pointers, no cycles); the DOM knows nothing about CSS, layout or scripting.

Style (M4): `css` parses stylesheets and declaration lists into plain structures and knows nothing
about the DOM. `style` matches those selectors against the DOM, applies the UA/author/inline
cascade (specificity, `!important`, inheritance) and produces a `ComputedStyle` per element; the
embedded UA stylesheet is itself CSS parsed by our parser. `DumpStyleTree()` is the deterministic
text form behind `--dump-style` and the style tests. Layout will consume the style tree only, never
the raw CSS.

Layout (M5): `layout` turns the style tree into a `BoxTree` with absolute CSS-pixel geometry.
Line breaking needs text metrics, so `LayoutDocument()` takes a `MeasureText` callback (standard
types plus `style::ComputedStyle`) supplied by `shell`; the built-in monospace bitmap-font metrics
live in `render`, and `shell` adapts them — or the selected real font's shaped advances, line height
and ascent (M8b/M10) — without layout knowing about fonts. This keeps the dependency direction
intact: no `layout` → `render` edge and no font code duplicated in `layout`. Since M10 the callback
receives the text's computed style, so the shell can pick the font for that style; `TextMetrics`
carries an ascent and layout stores the line's maximum ascent as `Box::baseline_offset_px` on every
text box of the line (mixed fonts on one line then share a baseline in paint). Text is split into
break units (`src/layout/text_break.cpp`): whitespace separates words, non-CJK text stays grouped
into maximal words, and CJK characters are individual break opportunities with simplified
punctuation prohibitions, so Chinese text without spaces wraps normally. Each unit is an ordinary
measured word-level text box, so the M5 fitting, line-height and baseline machinery is reused. `DumpBoxTree()` is the deterministic text form behind
`--dump-boxes` and the layout tests. Layout consumes the style tree only: it never reads raw CSS and
never paints. The geometry is a pure function of its inputs; a summary of the MVP simplifications
lives in the known-limits list in [roadmap.md](roadmap.md). M8a adds replaced elements to the same
layout: `LayoutOptions::replaced_sizes` maps DOM node ids to intrinsic CSS-pixel sizes supplied by
`shell`; an inline element with an entry becomes an atomic inline-level box (CSS width/height px
override, one specified side keeps the intrinsic ratio, wraps as a whole, top-aligned, raises the
line height), while `layout` still never reads images or depends on `render`. M10's break-unit
model is a documented simplification of Unicode line breaking (no full UAX#14, no dictionary
breaking for Thai/Khmer/Lao, no hyphenation, no cross-text-node punctuation gluing); Latin words
are never split.

Painting (M6): `paint` walks the box tree in pre-order and emits a flat `render::DisplayList`:
every element box with a non-transparent computed background becomes a `FillRect` (backgrounds
before children), every inline replaced box whose node id has a decoded image becomes a `DrawImage`
(M8a, after the background and before children), and every text box becomes either one or more
`DrawTextWithFont` runs or (in bitmap mode) one `DrawText`, all with the inherited color and font
size. Since M10 paint receives a `paint::FontRunsProvider` from the shell instead of a single font:
the provider returns ordered real-font runs for the box text, and the painter advances a pen by
each run's shaped width and places every run on `box.baseline_offset_px`; an empty provider (or an
empty run list for a box) reproduces the pre-M10 bitmap command list exactly. `paint` never rasterizes and never includes render implementation
headers. `render` executes the list: rectangle fills use the frozen SrcOver integer formula over an
opaque destination, text uses the built-in 8x16 ASCII glyph table with nearest-neighbour cell
scaling (no anti-aliasing), images use a frozen nearest-neighbour source mapping (below), and an
integer rectangle clip stack bounds every command (M7 wraps the page in a viewport
`PushClip`/`PopClip` pair and translates every command by the scroll offset). `shell` fills the
canvas white, lays the page out at the surface width, builds the list and rasterizes it; the window
and `--screenshot` share exactly this path. Determinism is the point: identical inputs produce
byte-identical PPMs on GCC and Clang, which the screenshot tests rely on.

Images (M8a): `render` owns PNG decoding behind `render::DecodePng()`: the approved libpng
dependency is wrapped in `src/render/image.cpp`, pixels are converted to straight-alpha ARGB
`render::Image`, dimensions and pixel counts are validated before allocation, and no libpng type
appears in a public header. `shell` owns the page-level policy in a private loader
(`src/shell/page.h`): after fetching, parsing and styling the page and before layout, it resolves
each `<img src>` against the page's final URL, fetches it with the same `net::Fetch` used for pages
(http/https/file), decodes it, and keeps a `paint::ImageMap` plus a `layout::ReplacedSizes` map keyed
by the img DOM node id.
Missing or failed images are skipped silently and the element lays out as an empty inline (MVP
limit). `paint` turns an inline replaced box with a decoded image into a `DrawImage` command; the
display list keeps a `shared_ptr<const Image>`, so a list may outlive the page data it was built
from. `render` executes images with a frozen nearest-neighbour integer source mapping and the same
SrcOver formula and clip stack as fills. `shell::Browser` stores the images with the page, paints
them, and re-lays-out on resize without refetching; `--dump-boxes` uses the same loader so image
geometry is part of the dump, while `--dump-dom`/`--dump-style` stay image-free.

Real fonts (M8b): `render::Font` wraps the approved freetype and harfbuzz dependencies behind a
pimpl: the public header names no freetype/harfbuzz type, loading failures are ordinary errors,
and the font owns its face bytes and freetype library. Metrics come from the font's design units
(`ascent = size * ascender / unitsPerEm`, line height from `face->height`), advance widths are
harfbuzz-shaped at the requested size (26.6 sums), and `internal::FontRasterizer` (private to
render) shaves 8-bit freetype coverage bitmaps onto the display list's baseline with a frozen
per-glyph integer placement formula and SrcOver coverage compositing, clipped to the current clip
rectangle. The effective font size is clamped to `kMaxRealFontSizePx` (4096 px) so adversarial CSS
cannot request unbounded glyph bitmaps. `paint` emits one `DrawTextWithFont` command per real-font run (M10; the M8b single-face session was
the special case of one run per box), placed on the layout-provided baseline, and the frozen bitmap
`DrawText` command when the provider returns nothing; the display list keeps a shared reference to
each font, like images. M8b loaded exactly one face per run from `--font PATH` or a fontconfig
family match; M10 replaces that with automatic per-style selection (next paragraph). Fontconfig
never leaks outside the shell and the font never outside render's private implementation.

Font selection (M10): `shell`'s public `Fonts` object (`toy/shell/fonts.h`, pimpl) turns the
computed text style and the page language into concrete faces. For every distinct (font-family
list, weight, slant, language) it builds a fontconfig pattern (`FC_FAMILY` per family,
`FC_WEIGHT` via `FcWeightFromOpenType`, `FC_SLANT`, `FC_LANG` when the root `<html lang>` value is
non-empty), runs the normal substitute/default/sort pipeline, and caches the ordered candidate
faces with their character sets; loaded `render::Font` faces are cached per file path. A text box is
split into runs by code point: each code point goes to the first candidate whose character set
covers it (so Han text falls back to an installed Han face on a Latin page and vice versa), runs
with the same face are grouped, and `MeasureText` sums the run advances while `Runs` returns the
same runs to paint — measurement and painting therefore agree by construction. If no usable face is
available (fontconfig failure, no match, no loadable file) the object operates in bitmap mode: the
built-in bitmap metrics are measured and `Runs` returns empty, which keeps the browser usable and
the pre-M10 golden outputs reproducible. `--font PATH` forces one exact face (no fallback);
`--font-family NAME` forces the preferred family (still with character fallback) and now also
follows the computed weight/style. The initial computed family is `serif`; other scripts are
best-effort through the same character-coverage mechanism (no bidi/RTL reordering and no
dictionary-based breaking yet).

Performance (M11): the browser is optimised for the interactive path without changing any rendered
pixel. The default build type is `RelWithDebInfo` when the caller does not choose one, and
first-party targets compile with `-ffp-contract=off` so layout geometry is byte-identical across
build types and compilers. `render::Font` keeps private, bounded LRU caches: one reusable
`hb_font`/`hb_buffer` per font (one `hb_face`, so harfbuzz's shape-plan cache works), a shaped-run
cache keyed by the exact 26.6 size plus UTF-8 text (16 MiB / 16k entries), and a glyph-coverage
cache keyed by glyph id plus 26.6 size (32 MiB / 16k entries); allocation failure is a cache miss
and eviction never changes output. The shell's `Fonts` adds an allocation-free resolved-style fast
path with stable style ids, a text-metrics LRU (32 MiB / 100k entries) and a run LRU (16 MiB /
100k entries); the bitmap-degradation path bypasses those caches. `paint` culls the display list to
the viewport: fills/images by exact destination-rect intersection and text boxes by their box
expanded with `ceil(2 * font_size_px)` on every side (glyph overhang/italic/anti-aliasing), before
any style/image/font-run lookup, while keeping one viewport clip pair and command order; the
rendered pixels are identical to an unculled list. The window loop repaints on Expose without
re-laying-out, re-lays-out only when the window size actually changed, and the X11 backend
coalesces consecutive queued Expose/ConfigureNotify bursts (input/close events are never merged,
dropped or reordered); one `render::Surface` is reused per window size. A frame therefore costs
O(visible commands + one box-tree walk) instead of O(page) text shaping/rasterization; relayout is
still a full reflow made cheap by the measurement caches. Rendering remains single-threaded and
software-only: measurements showed fill rate is not the bottleneck (the same system-font raster
time at 800x600 and 1600x1000), so there is no GPU backend; the window still presents the whole
surface with `XPutImage` (XShm/dirty rectangles are future work needing a dependency approval).
Measured on an 81k-text-box stress page (800x600, automatic system fonts): frame paint 10.6 s ->
3.5 ms, layout 2.1 s -> 31 ms, page load 2.2 s -> 69 ms, with every pre-M11 fixture screenshot and
all 33 scrolled reference frames byte-identical.

Navigation (M7): `shell::Browser` owns one browser session: the current page (final URL, DOM, style
tree, box tree and decoded images), the navigation history, the integer vertical scroll offset and
painting. The history is a vector of URLs plus an index; every successful load appends an entry
(loading after going back truncates the forward entries), and a failed load leaves the page, scroll
offset and history unchanged. A click is converted to page space with the scroll offset, hit-tested
against the box tree (`layout::HitTest` returns the last box in paint order containing the point),
mapped to the nearest DOM ancestor element with an `href` attribute, resolved with
`net::ResolveUrl` against the page's final URL and loaded. Only `http`/`https`/`file` references navigate; a
reference that only changes the fragment is a no-op. Scrolling is vertical and in integer device
pixels, clamped to `[0, content_height - viewport_height]` by `layout::ContentHeight`; `paint` wraps
the page in a viewport clip and translates it by `-scroll_y`, and `render` executes the frozen clip
stack. The window event loop forwards clicks, wheel notches and keys (Alt+Left/Right for
back/forward, arrows/PageUp/PageDown/Home/End for scrolling) to the session and repaints on demand;
the window title follows the first `<title>` text, falling back to the URL. Loading is synchronous
inside the handler, keeping the single-threaded MVP model. The page loader and image maps are
private shell implementation details: `toy/shell/browser.h` keeps the same public methods and hides
the page data behind a forward declaration and an out-of-line destructor.

## Data model

- **DOM**: an arena (`std::vector<Node>`) addressed by integer `NodeId`; parent, first/last child,
  previous/next sibling links are ids. Attributes live in a small vector per node. No parent/child
  pointer cycles, no `shared_ptr`.
- **Style tree**: parallel structure of computed values keyed by DOM ids.
- **Box tree**: layout arena addressed by integer `BoxId` (same shape as the DOM); each box
  references its DOM origin (element or text-node id) and holds an absolute border-box rectangle in
  CSS pixels. Text boxes hold the word placed on their line. Anonymous boxes have no DOM origin.
- **Display list**: flat `std::vector` of commands (`FillRect`, `DrawText`, `DrawImage`,
  `PushClip`/`PopClip`); an image command holds a shared reference to an immutable `render::Image`,
  and a real-font text command holds a shared `render::Font` plus a baseline `y` (bitmap text keeps
  the frozen top-of-cell `y` with a null font). Commands are integer device pixels.
- **Units**: CSS pixels as `float` in style/layout; device pixels as `int` in the rasterizer.
  MVP assumes 1 CSS px = 1 device px (no zoom, no DPI scaling).

Arena + ids are chosen because the trees are trivially destructible, easy to dump in tests, and
can't form ownership cycles. Revisit only if it actually hurts.

## Threading and event loop

- One process, one thread, one event loop. No worker threads in the MVP. A `render::Font` caches a
  mutable freetype face size between measures/draws, so it is intentionally single-threaded; do not
  share one `Font` across threads.
- A page load runs synchronously inside a navigation callback; the window is unresponsive while
  loading. That is acceptable for the toy, and async networking only arrives if it becomes a real
  problem.
- `net::Fetch` is a blocking call behind an interface, so it can later be replaced by a
  non-blocking state machine without touching callers.
- The platform event loop waits for window events and forwards close/resize/input/paint requests
  to `shell`; `shell` decides what to redraw. Redraw happens on demand (expose, resize, scroll),
  not on a timer.
- No global mutable state: the application object owns all modules.

## Rendering model

- Software rasterization only; no GPU, no render threads.
- Surface: 32-bit ARGB, straight alpha, SrcOver blending, top-left origin.
- Clipping is an integer rectangle stack pushed by display list commands; the initial clip is the
  whole surface, and rasterizer work is bounded by the clip area (so adversarial input such as a
  huge font size cannot make painting unbounded).
- The shell fills the canvas white before painting; element `background-color` is painted as that
  element's box (M6 does not propagate `html`/`body` backgrounds to the canvas).
- Text defaults to a small built-in bitmap font with hand-authored 8x16 ASCII glyphs: glyph cells
  are scaled to the computed font size with nearest-neighbour sampling and there is no
  anti-aliasing, and non-ASCII code points render the `?` glyph. With a real font selected
  (`--font`/`--font-family`), text is shaped by harfbuzz and rasterized by freetype into 8-bit
  coverage, placed on the glyph baseline and composited over the opaque destination with coverage
  as alpha; glyphs are clipped to the current clip rectangle and the effective size is capped at
  4096 px. The bitmap path remains the fallback and its pixels are frozen.
- Images are decoded to straight-alpha ARGB (bounds: each side at most 8192 px and at most
  16,777,216 pixels in total) and painted with nearest-neighbour scaling: destination pixel (dx, dy)
  samples source `((dx - x) * width / dst_width, (dy - y) * height / dst_height)` with integer floor
  division, so scaling adds no interpolation or anti-aliasing. Image pixels are composited with the
  same SrcOver formula as rectangle fills.
- Real-font text is the only anti-aliased drawing path: freetype coverage values are composited
  with the same SrcOver formula, and the per-glyph integer placement rounds 26.6 pen positions to
  device pixels (ties away from zero) so the output is deterministic for a given font, size and
  freetype/harfbuzz version. Tests assert ink regions and AA presence rather than exact coverage
  bytes, which are library-version dependent.
- Paint order is the box-tree pre-order: a box paints its background, then its children; there are
  no stacking contexts or z-index yet, so overlapping boxes paint in document order.
- Presentation: the platform receives a pixel view (pixels, width, height, stride measured in
  pixels per row — the same units as `render::Surface::stride()`) and copies it out. The headless
  backend writes a PPM file instead, which keeps render tests dependency-free.
- **Headless-first**: every stage runs without a window. Page loading, parsing, layout, painting
  and screenshots are driven and inspected through CLI modes (`--dump-body`, `--dump-dom`,
  `--dump-style`, `--dump-boxes`, `--screenshot [url]`); the window only ever presents pixels that
  the headless path already produced. Automated tests never require a display.
- The M1 placeholder page (white with a blue band) remains the fallback when no URL is given, so
  the pixel path can still be exercised without a page; real pages use the M6 paint path.

## Errors and ownership

- Recoverable failures today use simple, explicit results: `std::optional<T>` (URL parsing) or
  `bool` plus a `std::string* error` human-readable message (fetching, window creation). Callers
  check the result directly and never rely on exceptions. A `Result<T>` type with module error
  enums is introduced only if the simple pattern starts to hurt.
- No exceptions across module boundaries and no exceptions for ordinary control flow; internal
  invariants fail with `assert` in debug builds.
- Ownership: `std::unique_ptr` and values own; references/raw pointers are non-owning; arena ids
  stay inside trees. No `shared_ptr` unless two owners genuinely exist.

## Repository layout

```
.
├── .github/workflows/ci.yml        # GCC + Clang build/test CI mirroring the local gates
├── AGENTS.md
├── AI-DISCLOSURE.md                # prominent disclosure of heavy AI use
├── CMakeLists.txt
├── CMakePresets.json               # convenience presets: default build, dev (tests + WERROR)
├── CONTRIBUTING.md
├── LICENSE
├── README.md / README.zh.md
├── SECURITY.md
├── docs/
│   ├── assets/                     # screenshots used by README.md/README.zh.md
│   └── <design and process docs>
├── src/
│   ├── main.cpp
│   └── <module>/                    # appears with the module's first real file
│       ├── include/toy/<module>/*.h # public API: #include "toy/<module>/<file>.h"
│       └── *.cpp                    # implementation and private headers
└── tests/
    ├── CMakeLists.txt
    ├── toolchain_smoke.cpp
    ├── fixtures/                    # committed fixtures; binary ones ship their generator
    │   └── <kind>/{README.md,generate*.py,<fixture files>}
    └── <module>/                    # one executable per test subject
```

- No placeholder directories: a module directory is created together with its first file.
- Each module becomes one CMake static library target `toy_<module>`; `toy_shell` is linked into
  the `toybrowser` executable. Every target gets `toybrowser_set_warnings()`.
- Only `include/` is public; everything else is private to the module.

## Testing hooks (added with the stages that need them)

- CLI flags: `--dump-body`, `--dump-dom`, `--dump-style`, `--dump-boxes`,
  `--screenshot <file> [url]`, `--headless`, and the optional positional page URL for window mode.
- Tests are plain C++ executables registered with CTest; new tests share the header-only
  `tests/support/check.h` (interface target `toy_test_support`). No test framework dependency.
- Parser/layout golden files are text dumps compared line by line. Screenshots are deterministic
  (integer pixels, no anti-aliasing), so paint tests compare exact pixels and cross-compiler runs
  produce byte-identical PPMs; no image tolerance is needed.
- Tests are deterministic and never need a network, display, installed font or particular locale.
- M8b real-font tests use the committed self-authored `tests/fixtures/fonts/toy_test.ttf` (never an
  installed font): `render.font_ttf` covers loading/metrics/shaping/clamping, the extended
  `render.paint` covers AA coverage/clip/baseline/no-ops, `shell.fonts` covers the end-to-end
  real-font page, and CLI checks cover `--font`, `--font-family` (with a test-local
  `FONTCONFIG_FILE`), error exit codes and screenshot output.
- M9 TLS tests use committed test-only certificates in `tests/fixtures/tls/` (a CA, a `localhost`
  leaf and an unrelated untrusted leaf; the README documents regeneration) and the header-only
  in-process TLS loopback server `tests/support/tls_server.h`. Trust is supplied only through the
  CTest `ENVIRONMENT` property (`SSL_CERT_FILE=<test CA>`), so the normal verification path is
  exercised. `net.tls` covers a successful GET, untrusted certificate, hostname mismatch, handshake
  timeout, http<->https redirects and the body cap; `shell.https` serves the committed HTML+PNG page
  (`tests/fixtures/https_site/`) over TLS through `shell::Browser` and asserts the rendered pixels
  plus the untrusted-certificate failure. No test uses the public network or a public CA, and the
  M7 nav fixture uses an offline `ftp://` link for its unsupported-scheme case.
- M10 font tests use the committed self-authored fixtures (`tests/fixtures/fonts/`: regular,
  bold, italic and a tiny Han face) and two generated fontconfig configs: the fixture config (only
  the fixture fonts, generic families mapped to the Latin face) for automatic selection, weight/
  style, unknown-family fallback and the mixed Latin+Han run segmentation; the empty config (no font
  directories) forces the bitmap path and is used by all pre-M10 goldens. `style.parser`,
  `layout.baseline`, `layout.cjk_line_break`, `shell.font_selection`, `shell.page_language`, the
  `shell.cjk_*` tests (per-unit boxes, wrapping, painted ink) and the updated `shell.fonts*` cover
  the feature; tests never use installed fonts or the machine font configuration.
- M8a image tests use hand-generated PNG fixtures with documented pixel content
  (`tests/fixtures/images/`) and a committed image demo site (`tests/fixtures/img_site/`): the
  decoder is exercised directly, layout with fake replaced sizes, and the shell end to end through
  `shell::Browser` plus the CLI `--dump-boxes`/`--screenshot` checks.

## Reference material

Behavior is checked against the standards first and a real browser second; both are references
only — their code is never copied:

- HTML Living Standard: <https://html.spec.whatwg.org/multipage/>
- DOM Standard: <https://dom.spec.whatwg.org/>
- URL Standard: <https://url.spec.whatwg.org/>
- CSS Syntax Level 3: <https://www.w3.org/TR/css-syntax-3/>
- Selectors Level 3: <https://www.w3.org/TR/selectors-3/>
- CSS Cascading and Inheritance Level 4: <https://www.w3.org/TR/css-cascade-4/>
- CSS Values and Units Level 3: <https://www.w3.org/TR/css-values-3/>
- HTTP Semantics (RFC 9110) and HTTP/1.1 (RFC 9112); TLS 1.2/1.3 (RFC 5246/RFC 8446)
- OpenSSL documentation (approved M9 dependency, linked since M9): <https://docs.openssl.org/>
- PNG (Third Edition): <https://www.w3.org/TR/png-3/> and the libpng manual (approved M8a
  dependency): <https://www.libpng.org/pub/png/libpng.html>
- FreeType, HarfBuzz and fontconfig documentation (linked M8b dependencies):
  <https://freetype.org/>, <https://harfbuzz.github.io/>,
  <https://fontconfig.pages.freedesktop.org/fontconfig/>
- Web Platform Tests: <https://github.com/web-platform-tests/wpt>

## Deliberately out of scope

GPU rendering, multiprocess architecture, sandboxing, plugin systems, telemetry, config
frameworks, custom logging frameworks. Features land only when a milestone requires them.
