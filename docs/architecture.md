# Architecture

MVP-oriented design: the thinnest thing that makes each milestone work. Nothing here is
speculative — abstractions are added when a real slice needs them.

## Module map

| Module | Responsibility | May depend on | First needed |
| --- | --- | --- | --- |
| `base` | Small shared utilities (geometry, logging, UTF-8), only when a need appears | — | when first needed |
| `platform` | Window, input events, event loop, presenting pixel surfaces | `base` | M1 |
| `render` | Display list, software rasterizer, built-in bitmap font (metrics and glyphs), image decoding | `base` | M1 |
| `net` | URL parsing, sockets, HTTP/1.1 fetch | `base` | M2 |
| `dom` | HTML tokenizer, tree builder, DOM arena | `base` | M3 |
| `css` | CSS tokenizer, rule/declaration parser, selectors | `base` | M4 |
| `style` | Cascade and computed styles, default stylesheet | `dom`, `css` | M4 |
| `layout` | Box tree, block/inline layout, line breaking | `dom`, `style`, a text-measurement callback from `shell` | M5 |
| `paint` | Display list construction from the box tree (element backgrounds and text) | `dom`, `style`, `layout`, `render` | M6 |
| `js` | Small JavaScript engine and DOM bindings (optional, last) | `dom`, `base` | M9 |
| `shell` | Navigation, pipeline orchestration, window chrome | all | M1 |

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
7. `layout` turns the style tree into a box tree with geometry.
8. `paint` builds a flat display list from the box tree and `render` rasterizes it into a `Surface`.
9. `platform` presents the surface (window blit, or PPM dump in headless mode).

MVP keeps the whole chain synchronous and single-threaded. Every stage takes plain data and
returns plain data, so each one can be exercised through a `--dump-*` command in tests.

Networking (M2): `net` owns URLs, sockets and HTTP. Fetching is a blocking HTTP/1.1 GET — one
request per connection, connect and read timeouts, `Content-Length` or chunked bodies, redirects,
no TLS and no keep-alive — behind `Fetch()`, so it can become non-blocking later without changing
callers. `net` is the only module allowed to touch sockets; page code never sees them. Proxy
support (HTTP proxy first) is planned behind `FetchOptions` and stays inside `net`; it needs no new
dependency.

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
types only) supplied by `shell`; the built-in monospace bitmap-font metrics live in `render`, and
`shell` adapts them. This keeps the dependency direction intact: no `layout` → `render` edge and no
font code duplicated in `layout`. `DumpBoxTree()` is the deterministic text form behind
`--dump-boxes` and the layout tests. Layout consumes the style tree only: it never reads raw CSS and
never paints. The geometry is a pure function of its inputs; a summary of the MVP simplifications
lives in the known-limits list in [roadmap.md](roadmap.md).

Painting (M6): `paint` walks the box tree in pre-order and emits a flat `render::DisplayList`:
every element box with a non-transparent computed background becomes a `FillRect` (backgrounds
before children), and every text box becomes a `DrawText` with the inherited color and font size.
`paint` never rasterizes and never includes render implementation headers. `render` executes the
list: rectangle fills use the frozen SrcOver integer formula over an opaque destination, text uses
the built-in 8x16 ASCII glyph table with nearest-neighbour cell scaling (no anti-aliasing), and an
integer rectangle clip stack bounds every command (M6 emits no clip commands yet; overflow and
scroll clipping arrive with M7). `shell` fills the canvas white, lays the page out at the surface
width, builds the list and rasterizes it; the window and `--screenshot` share exactly this path.
Determinism is the point: identical inputs produce byte-identical PPMs on GCC and Clang, which the
screenshot test relies on.

## Data model

- **DOM**: an arena (`std::vector<Node>`) addressed by integer `NodeId`; parent, first/last child,
  previous/next sibling links are ids. Attributes live in a small vector per node. No parent/child
  pointer cycles, no `shared_ptr`.
- **Style tree**: parallel structure of computed values keyed by DOM ids.
- **Box tree**: layout arena addressed by integer `BoxId` (same shape as the DOM); each box
  references its DOM origin (element or text-node id) and holds an absolute border-box rectangle in
  CSS pixels. Text boxes hold the word placed on their line. Anonymous boxes have no DOM origin.
- **Display list**: flat `std::vector` of commands (`FillRect`, `DrawText`, `PushClip`/`PopClip`
  today; `DrawImage` when images arrive). Commands are integer device pixels.
- **Units**: CSS pixels as `float` in style/layout; device pixels as `int` in the rasterizer.
  MVP assumes 1 CSS px = 1 device px (no zoom, no DPI scaling).

Arena + ids are chosen because the trees are trivially destructible, easy to dump in tests, and
can't form ownership cycles. Revisit only if it actually hurts.

## Threading and event loop

- One process, one thread, one event loop. No worker threads in the MVP.
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
- Text uses a small built-in bitmap font with hand-authored 8x16 ASCII glyphs, so no font library
  is needed; glyph cells are scaled to the computed font size with nearest-neighbour sampling and
  there is no anti-aliasing. Non-ASCII code points render the `?` glyph for now; real TTF text
  comes later, after approval.
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
├── AGENTS.md
├── CMakeLists.txt
├── README.md
├── docs/
├── src/
│   ├── main.cpp
│   └── <module>/                    # appears with the module's first real file
│       ├── include/toy/<module>/*.h # public API: #include "toy/<module>/<file>.h"
│       └── *.cpp                    # implementation and private headers
└── tests/
    ├── CMakeLists.txt
    ├── toolchain_smoke.cpp
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
- HTTP Semantics (RFC 9110) and HTTP/1.1 (RFC 9112)
- Web Platform Tests: <https://github.com/web-platform-tests/wpt>

## Deliberately out of scope

GPU rendering, multiprocess architecture, sandboxing, plugin systems, telemetry, config
frameworks, custom logging frameworks. Features land only when a milestone requires them.
