# toybrowser

A toy web browser built from scratch: C++20, CMake, and no ready-made browser components.

**Status: M8b — real fonts and PNG images.** The window and `--headless --screenshot` render the
page given by an optional URL, including `<img src="...png">` images (approved libpng) and, when a
real font is selected with `--font PATH` or `--font-family NAME` (approved freetype/harfbuzz/
fontconfig), anti-aliased true-type text; without a font flag the built-in bitmap font is used and
all pre-M8b output is byte-identical. In the window, links are clickable, the wheel and keyboard
scroll, Alt+Left/Right move through history and the title follows `<title>`. Committed demos:
[`tests/fixtures/font_site/`](tests/fixtures/font_site/) for real text + images,
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
./build/toybrowser --size 320x600 "file://$PWD/tests/fixtures/font_site/index.html" \
    --font tests/fixtures/fonts/toy_test.ttf   # real TTF text + image demo (fixture font)
./build/toybrowser --size 400x200 --font-family "DejaVu Sans" \
    "file://$PWD/tests/fixtures/nav_site/index.html"   # any installed font family
./build/toybrowser --size 320x600 "file://$PWD/tests/fixtures/img_site/index.html"   # images demo
./build/toybrowser "file://$PWD/tests/fixtures/nav_site/index.html"  # the committed nav demo site
./build/toybrowser --dump-body file:///etc/hostname  # fetch a URL, raw bytes to stdout
./build/toybrowser --dump-dom  file:///etc/hostname  # fetch and dump the parsed DOM tree
./build/toybrowser --dump-style file:///etc/hostname # fetch, style, dump computed styles
./build/toybrowser --dump-boxes file:///etc/hostname # fetch, style, lay out, dump the box tree
```

The window backend links libX11, image decoding links libpng, real-font rendering links freetype and
harfbuzz, and family lookup links fontconfig (all approved; see
[dependencies](docs/dependencies.md)), so those development headers are build-time requirements
even when only headless modes are used. Headless runs need no display and are the
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

Five third-party packages are linked today (libX11, libpng + zlib, freetype, harfbuzz and
fontconfig); the browser engine itself has no ready-made components (no HTML parser, CSS engine, JS
engine, layout or rendering engine).

| Component | Purpose | License | Status |
| --- | --- | --- | --- |
| C++20 standard library + POSIX | baseline (containers, sockets, files) | standard / OS | always |
| libX11 | window, input, pixel presentation | MIT/X11 | linked since M1 |
| libpng + zlib | PNG image decoding | libpng license / zlib license | linked since M8a |
| freetype | font metrics and glyph rasterization | FreeType License | linked since M8b |
| harfbuzz | OpenType text shaping | MIT | linked since M8b |
| fontconfig | installed-font discovery and matching | MIT-style | linked since M8b |

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
