# toybrowser

A toy web browser built from scratch: C++20, CMake, and no ready-made browser components.

**Status: M11 — performance.** The browser fetches `http://`, `https://` and `file://` URLs
(`https://` uses the approved OpenSSL library with certificate and hostname verification always on,
no insecure mode) and renders pages headlessly or in an X11 window. M11 made large pages responsive
without changing a pixel: the default build is optimised, real fonts are shaped/rasterized through
bounded caches, painting is culled to the viewport, and the window loop no longer relayouts on every
expose/resize event (measured on a stress page: frame 10.6 s -> 3.5 ms, layout 2.1 s -> 31 ms).
Rendering stays software-only and single-threaded; there is no GPU backend on purpose. Without
any font flag it picks fonts the way a normal browser does: the computed CSS
`font-family`/`font-weight`/`font-style` are matched through the approved fontconfig library (the
initial family is `serif`), and text whose preferred face lacks characters falls back to an
installed face that has them. Layout splits text into runs and gives CJK text individual break
units, so Chinese pages are readable without spaces and wrap normally. `--font PATH` and
`--font-family NAME` remain explicit overrides; when no usable system font exists the browser
degrades to the built-in bitmap font. Real text is anti-aliased (freetype + harfbuzz), `<img
src="...png">` images are decoded with libpng, links are clickable in the window, the wheel and
keyboard scroll, Alt+Left/Right move through history and the title follows `<title>`. **Next
planned milestone: M12 — HTTP proxy** (deprioritized by the user; design sketch in the
[roadmap](docs/roadmap.md)). Committed demos:
[`tests/fixtures/cjk_site/`](tests/fixtures/cjk_site/) for automatic fonts + readable Chinese,
[`tests/fixtures/font_site/`](tests/fixtures/font_site/) for explicit real text + images,
[`tests/fixtures/img_site/`](tests/fixtures/img_site/) for images,
[`tests/fixtures/nav_site/`](tests/fixtures/nav_site/) for navigation and
[`tests/fixtures/https_site/`](tests/fixtures/https_site/) for the HTTPS pipeline. The `--dump-*`
modes remain the primary headless debugging path. See the [roadmap](docs/roadmap.md). 中文版：
[README.zh.md](README.zh.md).

**New team or agent?** Start with [docs/handoff.md](docs/handoff.md) (current state and takeover
guide), then the [documentation index](docs/README.md).

## Build and run

```sh
# Fast browser-only build (tests are opt-in).
cmake -S . -B build
cmake --build build

# Full verification: also build and run the tests.
cmake -S . -B build -DTOYBROWSER_BUILD_TESTS=ON -DTOYBROWSER_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure

./build/toybrowser file:///path/page.html            # window: click links, wheel/keys scroll, Alt+Left/Right
./build/toybrowser                                   # window with the placeholder page
./build/toybrowser --headless --screenshot out.ppm file:///path/page.html  # page, no display
./build/toybrowser --size 320x600 "file://$PWD/tests/fixtures/font_site/index.html" \
    --font tests/fixtures/fonts/toy_test.ttf   # real TTF text + image demo (fixture font)
./build/toybrowser --size 400x200 --font-family "sans-serif" \
    "file://$PWD/tests/fixtures/nav_site/index.html"   # force an installed font family
./build/toybrowser --size 400x240 "file://$PWD/tests/fixtures/cjk_site/index.html"  # automatic fonts + Chinese wrapping
./build/toybrowser --size 800x900 --screenshot /tmp/zh.ppm https://www.gov.cn/  # real Chinese page (system fonts)
./build/toybrowser --size 320x600 "file://$PWD/tests/fixtures/img_site/index.html"   # images demo
./build/toybrowser "file://$PWD/tests/fixtures/nav_site/index.html"  # the committed nav demo site
./build/toybrowser --dump-body https://example.com/   # fetch an https URL (TLS, always verified)
./build/toybrowser --dump-dom  https://example.com/   # fetch + parse a real https page
./build/toybrowser --size 1024x768 --screenshot out.ppm https://example.com/  # paint an https page
./build/toybrowser --dump-body file:///etc/hostname  # fetch a URL, raw bytes to stdout
./build/toybrowser --dump-dom  file:///etc/hostname  # fetch and dump the parsed DOM tree
./build/toybrowser --dump-style file:///etc/hostname # fetch, style, dump computed styles
./build/toybrowser --dump-boxes file:///etc/hostname # fetch, style, lay out, dump the box tree
```

The window backend links libX11, image decoding links libpng, text rendering/shaping links freetype
and harfbuzz, automatic font selection links fontconfig and `https://` links OpenSSL (all approved;
see [dependencies](docs/dependencies.md)), so those development headers are build-time
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

Six third-party packages are linked today (libX11, libpng + zlib, freetype, harfbuzz, fontconfig
and OpenSSL). The browser engine itself has no ready-made components (no HTML parser, CSS engine,
JS engine, layout or rendering engine).

| Component | Purpose | License | Status |
| --- | --- | --- | --- |
| C++20 standard library + POSIX | baseline (containers, sockets, files) | standard / OS | always |
| libX11 | window, input, pixel presentation | MIT/X11 | linked since M1 |
| libpng + zlib | PNG image decoding | libpng license / zlib license | linked since M8a |
| freetype | font metrics and glyph rasterization | FreeType License | linked since M8b |
| harfbuzz | OpenType text shaping | MIT | linked since M8b |
| fontconfig | automatic font discovery/matching, language hints and character fallback | MIT-style | linked since M8b (the automatic selection since M10) |
| OpenSSL | TLS for `https://` | Apache-2.0 | linked since M9 |

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
