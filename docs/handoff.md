# Handoff — current state and takeover guide

Start here if you are a new team or agent taking over this repository. This is the snapshot; the
hard rules are in [AGENTS.md](../AGENTS.md), the reading order in [docs/README.md](README.md).

## Where the project is

Milestones done (details and acceptance in [roadmap.md](roadmap.md)):

| Stage | Delivered |
| --- | --- |
| M0 | Design docs, CMake skeleton, stub CLI, toolchain smoke test |
| M1 | X11 window, software `Surface`, headless PPM screenshots (`--headless --screenshot`) |
| M2 | URL parsing, HTTP/1.1 GET, `file://` fetching (`--dump-body`) |
| M3 | HTML tokenizer/tree builder, arena DOM, deterministic dump (`--dump-dom`) |
| M4 | CSS parser/selectors, UA + author cascade, computed styles (`--dump-style`) |
| Review round | Fixed unbounded `--size`, file body cap, dump indentation; 20 CTest tests |

Not implemented yet: layout, painted pages, real text/fonts, images, JavaScript, `https`/TLS,
proxies, external stylesheets, interaction and navigation. Window mode shows the M1 placeholder
page; everything useful today goes through headless CLI modes.

## Verify the current state (no context needed)

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure

./build/toybrowser --help
./build/toybrowser --dump-dom   file:///etc/hostname | head
./build/toybrowser --dump-style file:///etc/hostname | head
./build/toybrowser --headless --screenshot out.ppm && file out.ppm
```

Expected: 20/20 tests pass; building with `-DTOYBROWSER_WERROR=ON` is warning-free on GCC and
Clang; dumps print deterministic trees; the screenshot writes a binary PPM. All headless modes
need no display. Building needs the X11 development headers (approved dependency); only windowed
mode needs a running display.

## Next round: M5 — layout (not started)

- Goal: turn the style tree into a box tree with geometry, dumped by `--dump-boxes`.
- Preliminary scope: block and inline boxes, anonymous boxes, width/margins/padding, line breaking.
- Open design question: line breaking needs text metrics. The plan is a small built-in bitmap font
  (ASCII first) so layout can be tested headless; real TTF text stays at M8. Confirm during the M5
  intake instead of assuming.
- Acceptance sketch: fixture pages produce expected box dumps; all existing tests keep passing;
  warning-free under both compilers; fully headless.

Backlog after M5 (details in [roadmap.md](roadmap.md)): image/font library decisions at M8, proxy
support, TLS, and test-helper consolidation.

## Map of the repository

- `src/<module>/` — one CMake library target per module (`base` does not exist yet; see
  [architecture.md](architecture.md) for the module map and dependency direction).
- `src/main.cpp` — CLI parsing only; `shell` orchestrates fetch → parse → style → dump/present.
- `tests/<module>/` — one executable per subject, registered with CTest.
- `docs/` — architecture, roadmap, conventions, team workflow, decisions, dependencies, open
  questions (this file is the snapshot).
- `README.md` / `README.zh.md` and `docs/zh/` — user-facing entry points (English authoritative).

## Frozen contracts (do not change casually)

- CLI modes, flags and exit codes: `0` success, `1` runtime error, `2` usage error.
- Dump formats (DOM, style, later boxes): deterministic, line-oriented, asserted by tests; any
  change needs tests and docs in the same round.
- Module boundaries and dependency direction in [architecture.md](architecture.md).
- `--size` is clamped to 1..8192 per side and at most 16M pixels; bodies are capped by
  `FetchOptions::max_body_bytes` (16 MiB by default); dump indentation is capped at 64 levels.
- HTML/CSS/JS support is deliberately partial: read the known-limits entries in `roadmap.md`
  instead of assuming spec behavior.

## How a new team starts working

1. Read this file, [docs/README.md](README.md), then `AGENTS.md`.
2. Run the verification commands above; if they differ from this snapshot, treat that as a bug
   and fix the docs or the code.
3. Pick the next milestone from `roadmap.md`; run the round bookkeeping from
   [team-workflow.md](team-workflow.md): intake → contracts → kickoff → parallel work →
   integration → delivery → retro.
4. The Lead owns docs, shared CMake files and commits; implementers claim one task, stay inside
   its write scope, verify locally and report.
5. Ask the user before adding any dependency, downloading code/test data, or changing a locked
   decision in [decisions.md](decisions.md).

## Gotchas that cost the previous team time

- Don't test with a display when a `--dump-*` mode can prove the same thing; everything is
  designed to be headless-first.
- Deeply nested input used to make dumps quadratic; keep any new dump depth-capped and tested.
- Build artifacts and object files must never be committed; use isolated build directories.
- No `https` yet: most real-world sites cannot be fetched; `http://` and `file://` work.
- The HTML/CSS/URL parsers are error-tolerant by design; they never fail on malformed input, so
  "it didn't error" is not evidence of correctness — compare dumps with the golden tests.
