# Handoff — current state and takeover guide

This is the snapshot for a team taking over the repository. The canonical reading order is in
[docs/README.md](README.md); the hard rules are in [AGENTS.md](../AGENTS.md).

## Where the project is

Milestones done (details and acceptance in [roadmap.md](roadmap.md)):

| Stage | Delivered |
| --- | --- |
| M0 | Design docs, CMake skeleton, stub CLI, toolchain smoke test |
| M1 | X11 window, software `Surface`, headless PPM screenshots (`--headless --screenshot`) |
| M2 | URL parsing, HTTP/1.1 GET, `file://` fetching (`--dump-body`) |
| M3 | HTML tokenizer/tree builder, arena DOM, deterministic dump (`--dump-dom`) |
| M4 | CSS parser/selectors, UA + author cascade, computed styles (`--dump-style`) |
| M5 | `layout` box tree with block/inline/anonymous boxes and line breaking, bitmap-font metrics in `render`, deterministic box dump (`--dump-boxes`) |
| M6 | `render` display list + software rasterizer + hand-authored 8x16 ASCII glyphs, `paint` box-tree-to-display-list module, window/`--screenshot` painting with an optional positional page URL |
| Review round | Fixed unbounded `--size`, file body cap, dump indentation; 20 CTest tests |

Not implemented yet: navigation, scrolling and interaction, images, real TTF fonts, JavaScript,
`https`/TLS, proxies, external stylesheets, borders, anti-aliased text. The window and `--screenshot`
paint a real page when a URL is given and the M1 placeholder page otherwise; the `--dump-*` modes
remain the primary debugging path.

## Verify the current state (no context needed)

Run the following from the repository root:

```sh
BUILD=build/onboard-check
cmake -S . -B "$BUILD" -DTOYBROWSER_WERROR=ON
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure

"$BUILD"/toybrowser --help
"$BUILD"/toybrowser --dump-dom    "file://$PWD/README.md" | head
"$BUILD"/toybrowser --dump-style  "file://$PWD/README.md" | head
"$BUILD"/toybrowser --dump-boxes  "file://$PWD/README.md" | head
"$BUILD"/toybrowser --headless --screenshot /tmp/toybrowser.ppm && file /tmp/toybrowser.ppm
"$BUILD"/toybrowser --size 640x400 --screenshot /tmp/toybrowser-page.ppm \
    "file://$PWD/README.md" && file /tmp/toybrowser-page.ppm
```

Expected: 37/37 tests pass; building with `-DTOYBROWSER_WERROR=ON` is warning-free on GCC and
Clang; dumps print deterministic trees; screenshots write a binary PPM (the URL form paints the
real page, the URL-less form keeps the placeholder). All headless modes need no display. CMake 3.20
or newer is required; building needs the X11 development headers (approved dependency); only
windowed mode needs a running display.

## Next round: M7 — navigation (not started)

- Goal: make the painted page interactive and navigable. Hit-test clicks against the box tree to
  follow links, add scrolling (a viewport offset plus clip commands in the display list), a
  back/forward history, and set the window title from `<title>`.
- Preliminary scope: `shell` owns navigation state (current URL, history, scroll offset) and the
  window event loop forwards clicks/scrolls/keys; `paint`/`render` gain the clip and offset
  plumbing already sketched in the architecture; `net` supplies the URL of the clicked page
  (`href` resolution against the current URL; base-URL resolution and external `<link>` CSS stay
  later). The `--dump-*` modes must stay headless and unchanged.
- Still out of scope: images, real TTF fonts (M8), forms, JavaScript (M9), `https`/TLS.
- Acceptance sketch: a small local multi-page site can be browsed in the window (link click,
  back/forward, scroll, title), with at least one headless regression test for URL/hit-test/scroll
  state; all existing tests keep passing; warning-free under both compilers.
- Navigation decisions (history granularity, scroll unit, how clicks map to boxes) are the M7
  intake's job — do not guess them in code.

Backlog after M7 (details in [roadmap.md](roadmap.md)): image/font library decisions at M8, proxy
support, TLS, and test-helper migration.

## Map of the repository

- `src/<module>/` — one CMake library target per module (`base` does not exist yet; see
  [architecture.md](architecture.md) for the module map and dependency direction).
- `src/main.cpp` — CLI parsing only; `shell` orchestrates fetch → parse → style → layout → paint →
  dump/present.
- `src/render/include/toy/render/font.h` — bitmap-font metrics; `layout` consumes them through a
  `shell`-supplied callback and has no `render` dependency. `src/render/include/toy/render/paint.h`
  is the display list + rasterizer; `src/paint/` builds the list from the box tree and never
  rasterizes.
- `tests/<module>/` — one executable per subject, registered with CTest; new tests use the
  header-only `tests/support/check.h`.
- `docs/` — architecture, roadmap, conventions, team workflow, decisions, dependencies, open
  questions (this file is the snapshot).
- Shared task board — provided by the agent runtime (task create/claim/update with revision); if
  the runtime has none, use `docs/tasks/<milestone>.md` with the same fields. See
  [team-workflow.md](team-workflow.md).
- `README.md` / `README.zh.md` and `docs/zh/` — user-facing entry points (English authoritative).
- `.tmp/` — gitignored scratch space for local notes, fixtures, downloads and test output.

## Frozen contracts (do not change casually)

- CLI modes, flags and exit codes: `0` success, `1` runtime error, `2` usage error. The optional
  positional page URL (`toybrowser [options] [url]`) is used only by window mode and
  `--screenshot`; at most one URL, it must not be combined with a `--dump-*` mode, and no URL keeps
  the M1 placeholder page.
- Dump formats (DOM, style, boxes): deterministic, line-oriented, asserted by tests; any change
  needs tests and docs in the same round. The box dump is pre-order lines of
  `<tag> block|inline`, `anonymous block` or `text "..."` followed by `x= y= width= height=`, all
  integers (`std::lround`) of absolute CSS pixels; indent is two spaces per box depth, capped at 64
  levels.
- Layout semantics frozen through the layout tests and the M5 known limits: viewport width from
  `--size`, no margin collapsing, top-aligned lines, word-level text boxes, ASCII-only line
  breaking.
- Painting semantics frozen through the M6 tests: display-list commands are integer device pixels
  (`FillRect`, `DrawText`, `PushClip`/`PopClip`); fills use the SrcOver formula
  `(src * a + dst * (255 - a) + 127) / 255` over an opaque destination; the initial clip is the
  whole surface and rasterizer work is bounded by the clip; the shell fills the canvas white; paint
  order is box-tree pre-order (background, then children); text uses the built-in 8x16 ASCII glyphs
  with nearest-neighbour scaling, no anti-aliasing, `?` for non-ASCII, and opaque text pixels.
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
  Layout is deliberately iterative (10,000 nested divs are a test): do not turn it back into DOM
  recursion.
- Build artifacts and object files must never be committed; use isolated build directories.
- No `https` yet: most real-world sites cannot be fetched; `http://` and `file://` work.
- The HTML/CSS/URL parsers are error-tolerant by design; they never fail on malformed input, so
  "it didn't error" is not evidence of correctness — compare dumps with the golden tests.
- `--dump-boxes` uses `--size`'s width as the viewport and ignores the height; heights come from
  the content. `layout` must never include a `render` header — text metrics only arrive through the
  callback.
- Rendering is integer-pixel and anti-aliasing-free on purpose: changing rounding, glyph scaling or
  paint order will change golden pixel tests. GCC and Clang must produce byte-identical PPMs —
  `cmp` two builds' screenshots as a cheap determinism gate.
- Paint is documented as "background, then children" in pre-order; there are no stacking contexts
  yet, so do not add alpha/overlap semantics without a contract change.
- The canvas is filled white by the shell; `html`/`body` background-color paints its element box
  only (no background propagation to the canvas) — a deliberate M6 limit, not a bug.
- Rasterizer work must stay bounded by the clip/surface area: adversarial font sizes used to hang
  or overflow the old glyph loop; keep the finite-size guard, the visible-range iteration and the
  bounded cell math when touching `render::DrawDisplayList`.
- Scratch files (fixtures, screenshots, notes, downloads) go in the gitignored `.tmp/` directory at
  the repository root, never scattered through the tree.
