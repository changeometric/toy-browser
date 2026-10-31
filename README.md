# toybrowser

A toy web browser built from scratch: C++20, CMake, and no ready-made browser components.

**Status: M8a — pages can show PNG images.** The window and `--headless --screenshot` render the
page given by an optional URL, including `<img src="...png">` images (fetched and decoded through the
approved libpng dependency); in the window, links are clickable, the wheel and keyboard scroll,
Alt+Left/Right move through history and the title follows `<title>`. Real TTF fonts are the next
milestone (M8b, with the approved freetype/harfbuzz/fontconfig stack). Two committed demos exist:
[`tests/fixtures/img_site/`](tests/fixtures/img_site/) for images and
[`tests/fixtures/nav_site/`](tests/fixtures/nav_site/) for navigation. The `--dump-*` modes remain
the primary headless debugging path. See the [roadmap](docs/roadmap.md). 中文版：
[README.zh.md](README.zh.md).

**New team or agent?** Start with [docs/handoff.md](docs/handoff.md) (current state and takeover
guide), then the [documentation index](docs/README.md).

## Build and run

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

./build/toybrowser file:///path/page.html            # window: click links, wheel/keys scroll, Alt+Left/Right
./build/toybrowser                                   # window with the placeholder page
./build/toybrowser --headless --screenshot out.ppm file:///path/page.html  # page, no display
./build/toybrowser --size 320x600 "file://$PWD/tests/fixtures/img_site/index.html"   # images demo
./build/toybrowser "file://$PWD/tests/fixtures/nav_site/index.html"  # the committed nav demo site
./build/toybrowser --dump-body file:///etc/hostname  # fetch a URL, raw bytes to stdout
./build/toybrowser --dump-dom  file:///etc/hostname  # fetch and dump the parsed DOM tree
./build/toybrowser --dump-style file:///etc/hostname # fetch, style, dump computed styles
./build/toybrowser --dump-boxes file:///etc/hostname # fetch, style, lay out, dump the box tree
```

The window backend links libX11 and image decoding links libpng (both approved; see
[dependencies](docs/dependencies.md)), so the X11 and libpng development headers are build-time
requirements even when only headless modes are used. Headless runs need no display and are the
primary mode for testing and debugging: every rendered result can be inspected without opening a
window.

CMake 3.20 or newer and any C++20 compiler work; nothing in the tree depends on a specific
machine's paths, toolchain version or installed package set.

## Documentation

| Document | Content |
| --- | --- |
| [handoff.md](docs/handoff.md) | Current state, verification steps, next milestone, takeover guide |
| [README.md](docs/README.md) | Documentation index and reading order |
| [architecture.md](docs/architecture.md) | Module map, data flow, data model, rendering and threading |
| [decisions.md](docs/decisions.md) | Locked technical decisions and their rationale |
| [roadmap.md](docs/roadmap.md) | Milestones, each a minimal vertical slice |
| [dependencies.md](docs/dependencies.md) | Third-party policy, approval log and pending requests |
| [team-workflow.md](docs/team-workflow.md) | Round-based team process: roles, phases, quality gates, lessons |
| [conventions.md](docs/conventions.md) | C++, CMake and testing conventions |
| [open-questions.md](docs/open-questions.md) | Decisions waiting for the user |
| [AGENTS.md](AGENTS.md) | Rules for AI agents working on the repository |

## Dependencies

Only two third-party libraries are linked today (libX11, and libpng + zlib); the browser engine
itself has no ready-made components (no HTML parser, CSS engine, JS engine, layout or rendering
engine).

| Component | Purpose | License | Status |
| --- | --- | --- | --- |
| C++20 standard library + POSIX | baseline (containers, sockets, files) | standard / OS | always |
| libX11 | window, input, pixel presentation | MIT/X11 | linked since M1 |
| libpng + zlib | PNG image decoding | libpng license / zlib license | linked since M8a |
| freetype | font metrics and glyph rasterization | FreeType License | approved, not linked (M8b) |
| harfbuzz | OpenType text shaping | MIT | approved, not linked (M8b) |
| fontconfig | installed-font discovery and matching | MIT-style | approved, not linked (M8b) |

The policy, the approval log and from-scratch alternatives live in
[dependencies.md](docs/dependencies.md).

## License

MIT — see [LICENSE](LICENSE). All approved dependencies use permissive licenses compatible with it.

## Ground rules

- C++20 and CMake, Linux desktop as the target.
- No ready-made HTML parser, CSS engine, JS engine, layout engine or browser engine: those parts
  are ours to write.
- Low-level libraries need the user's explicit approval before use; approved codecs and font
  machinery are allowed because "from scratch" applies to browser functionality.
- Every milestone ships the smallest working, verifiable slice — no speculative design.
