# Architecture

MVP-oriented design: the thinnest thing that makes each milestone work. Nothing here is
speculative — abstractions are added when a real slice needs them.

## Module map

| Module | Responsibility | May depend on | First needed |
| --- | --- | --- | --- |
| `base` | Small shared utilities (geometry, logging, UTF-8), only when a need appears | — | when first needed |
| `platform` | Window, input events, event loop, presenting pixel surfaces | `base` | M1 |
| `render` | Display list, software rasterizer, bitmap font, image decoding | `base` | M1 |
| `net` | URL parsing, sockets, HTTP/1.1 fetch | `base` | M2 |
| `dom` | HTML tokenizer, tree builder, DOM arena | `base` | M3 |
| `css` | CSS tokenizer, rule/declaration parser, selectors | `base` | M4 |
| `style` | Cascade and computed styles, default stylesheet | `dom`, `css` | M4 |
| `layout` | Box tree, block/inline layout, line breaking | `dom`, `style` | M5 |
| `js` | Small JavaScript engine and DOM bindings (optional, last) | `dom`, `base` | M9 |
| `shell` | Navigation, pipeline orchestration, window chrome | all | M1 |

Dependencies point downward only; no cycles. `render` knows nothing about HTML/CSS (it consumes
display lists); `platform` knows nothing about page content (it blits pixels and reports input).
Only `shell` sees all modules and wires them together. `base` does not exist yet: M1-M3 modules use
only the standard library and standard POSIX interfaces, and the module appears when a shared
utility is actually needed.

## One page load, end to end

1. `shell` receives a URL (CLI argument or a link click).
2. `net` fetches it: bytes, final URL, status.
3. Bytes are decoded to UTF-8 (MVP: assume UTF-8, strip a BOM; other encodings later).
4. `dom` parses the HTML into a `Document`.
5. `css` parses style sheets (`<style>` and `style=` attributes first; external CSS later).
6. `style` resolves the cascade into a style tree.
7. `layout` turns the style tree into a box tree with geometry.
8. `render` builds a display list and rasterizes it into a `Surface`.
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

## Data model

- **DOM**: an arena (`std::vector<Node>`) addressed by integer `NodeId`; parent, first/last child,
  previous/next sibling links are ids. Attributes live in a small vector per node. No parent/child
  pointer cycles, no `shared_ptr`.
- **Style tree**: parallel structure of computed values keyed by DOM ids.
- **Box tree**: layout arena; each box references its style/DOM origin and holds a float rectangle.
- **Display list**: flat `std::vector` of commands (`FillRect`, `DrawText`, `DrawImage`,
  `PushClip`/`PopClip`).
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
- Clipping is a simple rectangle stack pushed by display list commands.
- Text starts with a small built-in bitmap font (own glyph data, ASCII) so no font library is
  needed; real TTF text comes later, after approval.
- Presentation: the platform receives a pixel view (pixels, width, height, stride measured in
  pixels per row — the same units as `render::Surface::stride()`) and copies it out. The headless
  backend writes a PPM file instead, which keeps render tests dependency-free.
- **Headless-first**: every stage runs without a window. Page loading, parsing, layout, painting
  and screenshots are driven and inspected through CLI modes (`--dump-body`, `--dump-dom`,
  `--dump-boxes`, `--screenshot`); the window only ever presents pixels that the headless path
  already produced. Automated tests never require a display.
- M1's visible result — a white page with a blue band in a window — uses exactly this path, so
  later stages only swap display lists in.

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

- CLI flags: `--dump-body`, `--dump-dom`, `--dump-style`, `--dump-boxes`, `--screenshot <file>`,
  `--headless`.
- Tests are plain C++ executables registered with CTest; each test file carries its own small
  assertion helpers. No test framework dependency.
- Parser/layout golden files are text dumps compared line by line; images are compared with a
  small tolerance once paint exists.
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
