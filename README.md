# toybrowser

A toy web browser built from scratch: C++20, CMake, and no ready-made browser components.

**Status: M4 — CSS is parsed, cascaded and exposed through the headless `--dump-style` command.**
Page rendering is still the M1 placeholder; layout starts in M5. See the
[roadmap](docs/roadmap.md). 中文版：[README.zh.md](README.zh.md).

## Build and run

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

./build/toybrowser                                   # window with the placeholder page
./build/toybrowser --headless --screenshot out.ppm   # same frame, no display needed
./build/toybrowser --dump-body file:///etc/hostname  # fetch a URL, raw bytes to stdout
./build/toybrowser --dump-dom  file:///etc/hostname  # fetch and dump the parsed DOM tree
./build/toybrowser --dump-style file:///etc/hostname # fetch, style, dump computed styles
```

The window backend links libX11 (approved; see [dependencies](docs/dependencies.md)), so the X11
development headers are a build-time requirement even when only headless modes are used. Headless
runs need no display and are the primary mode for testing and debugging: every rendered result can
be inspected without opening a window.

Any C++20 compiler and CMake generator that meets the minimum CMake version works; nothing in the
tree depends on a specific machine's paths, toolchain version or installed package set.

## Documentation

| Document | Content |
| --- | --- |
| [architecture.md](docs/architecture.md) | Module map, data flow, data model, rendering and threading |
| [roadmap.md](docs/roadmap.md) | Milestones, each a minimal vertical slice |
| [dependencies.md](docs/dependencies.md) | Third-party policy and pending approval requests |
| [team-workflow.md](docs/team-workflow.md) | Round-based team process: roles, phases, quality gates, lessons |
| [conventions.md](docs/conventions.md) | C++, CMake and testing conventions |
| [open-questions.md](docs/open-questions.md) | Decisions waiting for the user |
| [AGENTS.md](AGENTS.md) | Rules for AI agents working on the repository |

## Dependencies

Only one third-party library is linked today; the browser engine itself has no ready-made
components (no HTML parser, CSS engine, JS engine, layout or rendering engine).

| Component | Purpose | License | Status |
| --- | --- | --- | --- |
| C++20 standard library + POSIX | baseline (containers, sockets, files) | standard / OS | always |
| libX11 | window, input, pixel presentation | MIT/X11 | linked since M1 |
| libpng + zlib | image decoding (M8, if used) | libpng license / zlib license | approved, not linked |
| freetype | font rasterization (M8, if used) | FreeType License | approved, not linked |

The policy, the approval log and from-scratch alternatives live in
[dependencies.md](docs/dependencies.md).

## License

MIT — see [LICENSE](LICENSE). All approved dependencies use permissive licenses compatible with it
(libX11 is MIT/X11; libpng/zlib/freetype have their own permissive licenses).

## Ground rules

- C++20 and CMake, Linux desktop as the target.
- No ready-made HTML parser, CSS engine, JS engine, layout engine or browser engine: those parts
  are ours to write.
- Low-level libraries need the user's explicit approval before use.
- Every milestone ships the smallest working, verifiable slice — no speculative design.
