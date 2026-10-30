# toybrowser

A toy web browser built from scratch: C++20, CMake, and no ready-made browser components.

**Status: M1 — window, software surface and headless screenshots.** Page loading arrives in later
milestones; see the [roadmap](docs/roadmap.md).

## Build and run

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

./build/toybrowser                                   # window with the placeholder page
./build/toybrowser --headless --screenshot out.ppm   # same frame, no display needed
```

The window backend needs libX11 at build time (approved; see [dependencies](docs/dependencies.md)).
The headless path needs neither a display nor any extra library.

Any C++20 compiler and CMake generator that meets the minimum CMake version works; nothing in the
tree depends on a specific machine's paths, toolchain version or installed package set.

## Documentation

| Document | Content |
| --- | --- |
| [architecture.md](docs/architecture.md) | Module map, data flow, data model, rendering and threading |
| [roadmap.md](docs/roadmap.md) | Milestones, each a minimal vertical slice |
| [dependencies.md](docs/dependencies.md) | Third-party policy and pending approval requests |
| [team-workflow.md](docs/team-workflow.md) | Lead and teammate roles, task board, parallel work rules |
| [conventions.md](docs/conventions.md) | C++, CMake and testing conventions |
| [open-questions.md](docs/open-questions.md) | Decisions waiting for the user |
| [AGENTS.md](AGENTS.md) | Rules for AI agents working on the repository |

## Ground rules

- C++20 and CMake, Linux desktop as the target.
- No ready-made HTML parser, CSS engine, JS engine, layout engine or browser engine: those parts
  are ours to write.
- Low-level libraries need the user's explicit approval before use.
- Every milestone ships the smallest working, verifiable slice — no speculative design.
