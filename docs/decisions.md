# Locked decisions

Decisions that are settled and should not be re-opened by a new team without a real reason and the
user's agreement. Each entry says what was decided, why, and where the details live.

| Decision | Why | Details |
| --- | --- | --- |
| C++20 and CMake only | User requirement; modern standard library, one build system | [AGENTS.md](../AGENTS.md), `CMakeLists.txt` |
| Linux desktop target | Scope control for a toy project; no cross-platform abstractions | [AGENTS.md](../AGENTS.md) |
| No ready-made browser components (HTML/CSS/JS engines, layout engines, GUI frameworks) | The point of the project is to write them ourselves | [AGENTS.md](../AGENTS.md), [dependencies.md](dependencies.md) |
| Any third-party library needs explicit user approval | Keeps the tree portable and the project honest | [dependencies.md](dependencies.md) |
| Module boundaries: one static library per module, dependencies point downward, no cycles | Keeps modules testable and replaceable | [architecture.md](architecture.md) |
| DOM is an arena addressed by integer `NodeId`; no owning pointers or cycles | Simple lifetimes, easy dumps, safe tree links | [architecture.md](architecture.md) |
| Single process, single thread, blocking fetch in the MVP | Complexity budget; async only if it becomes a real problem | [architecture.md](architecture.md) |
| Headless-first: every feature works and is tested without a window | Enables CI, debugging and automated verification | [AGENTS.md](../AGENTS.md), [architecture.md](architecture.md) |
| Software rasterization, ARGB32 surfaces, PPM screenshots | No GPU or graphics dependencies; deterministic tests | [architecture.md](architecture.md) |
| Dumps are deterministic, line-oriented contracts (`DOM`, style, boxes) | They are the testing and debugging interface | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| `layout` produces a pure arena `BoxTree`; text metrics arrive through a `shell`-supplied callback (no `layout` → `render` edge) | Keeps the layer boundaries and lets layout be unit-tested with a fake measurer | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| Painting is integer-device-pixel and anti-aliasing-free; text uses the built-in 8x16 bitmap glyphs with nearest-neighbour scaling | Deterministic pixel tests and byte-identical GCC/Clang screenshots without a font dependency | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| The shell fills the page canvas white; element `background-color` paints that element's box only (no canvas background propagation) | Keeps the MVP painter explicit and simple; revisit if real sites look wrong | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| Navigation state (current URL, history, scroll offset) lives in a headless `shell::Browser`; the window event loop and `--screenshot` are thin presenters on top of it | Link clicks, history, scrolling and titles stay testable without a display | [architecture.md](architecture.md), `src/shell/include/toy/shell/browser.h` |
| Click activation: hit-test the box tree at the scrolled page point, then follow the nearest DOM ancestor element with an `href`; only `http`/`file` references navigate, fragment-only references are no-ops, and a failed load keeps the current page, scroll offset and history | Deterministic link semantics with no HTML knowledge in `layout` or `render` | [architecture.md](architecture.md) |
| Scrolling is vertical, in integer device pixels, clamped to `[0, content_height - viewport_height]`; `paint` wraps the page in a viewport `PushClip`/`PopClip` pair and translates every command by `-scroll_y`; navigation resets the offset to 0 | Minimal deterministic scrolling; scroll restoration is a documented MVP limit | [architecture.md](architecture.md), [roadmap.md](roadmap.md) |
| Platform input is normalized (left click, wheel notches, mapped keys with modifiers, `SetTitle`); `shell` owns the key bindings and scroll units | Keeps X11 details out of `shell` and page content out of `platform` | [architecture.md](architecture.md) |
| HTML/CSS support is an explicit subset with documented known limits | A browser toy, not a spec compliance project | [roadmap.md](roadmap.md) |
| Text starts with a built-in bitmap font; real TTF deferred to M8 | Avoids a font dependency before layout exists | [roadmap.md](roadmap.md) |
| HTTP/1.1 only; no TLS and no proxy yet (deferred by the user) | Proportionate to the toy scope; revisit when needed | [roadmap.md](roadmap.md), [open-questions.md](open-questions.md) |
| UTF-8 input assumed (BOM stripped); no encoding detection | Keeps M2–M4 small; encodings can come later | [roadmap.md](roadmap.md) |
| One grouped commit per round; English repository files except `README.zh.md` / `docs/zh/**` | Readable history and a uniform repository | [AGENTS.md](../AGENTS.md), [conventions.md](conventions.md) |

If a decision becomes wrong, do not silently deviate: raise it in [open-questions.md](open-questions.md)
or with the user, then update this file, the affected docs and the code in the same round.
