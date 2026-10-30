# Architecture

MVP-oriented design: the thinnest thing that makes each milestone work. Nothing here is
speculative — abstractions are added when a real slice needs them.

## Module map

| Module | Responsibility | May depend on | First needed |
| --- | --- | --- | --- |
| `base` | Small utilities: `Result`, geometry, logging, UTF-8 helpers | — | M1 |
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
Only `shell` sees all modules and wires them together.

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
- Presentation: the platform receives pixels + width/height/stride and copies them out. The
  headless backend writes a PPM file instead, which keeps render tests dependency-free.
- M1's visible result — a solid-color window — uses exactly this path, so later stages only swap
  display lists in.

## Errors and ownership

- Recoverable failures return `toy::Result<T>`-style values with a module-specific error enum
  (fetch failed, file missing, malformed input recovered from, ...).
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
- Tests are plain C++ executables registered with CTest; a tiny assertion helper in
  `tests/support/` appears when the second test file does. No test framework dependency.
- Parser/layout golden files are text dumps compared line by line; images are compared with a
  small tolerance once paint exists.
- Tests are deterministic and never need a network, display, installed font or particular locale.

## Deliberately out of scope

GPU rendering, multiprocess architecture, sandboxing, plugin systems, telemetry, config
frameworks, custom logging frameworks. Features land only when a milestone requires them.
