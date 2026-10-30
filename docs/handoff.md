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
| M7 | Navigation: headless `shell::Browser` (final URL, history, scroll, title), `net::ResolveUrl`, box hit-testing, scroll-clipped painting, X11 click/wheel/keys + window titles, committed demo site, `shell.navigation` test |
| Review round | Fixed unbounded `--size`, file body cap, dump indentation; 20 CTest tests |

Not implemented yet: images, real TTF fonts (M8), JavaScript (M9), `https`/TLS, proxies, external
stylesheets, borders, anti-aliased text, horizontal scrolling/scrollbars, hover/active link styling
and form controls. The window and `--screenshot` paint a real page when a URL is given and the M1
placeholder page otherwise; with a page URL the window is also browsable (links, wheel/keyboard
scrolling, Alt+Left/Right history, `<title>`). The `--dump-*` modes remain the primary debugging
path.

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

Expected: 40/40 tests pass; building with `-DTOYBROWSER_WERROR=ON` is warning-free on GCC and
Clang; dumps print deterministic trees; screenshots write a binary PPM (the URL form paints the
real page, the URL-less form keeps the placeholder). All headless modes need no display. CMake 3.20
or newer is required; building needs the X11 development headers (approved dependency); only
windowed mode needs a running display.

```sh
"$BUILD"/toybrowser --size 400x200 --screenshot /tmp/toybrowser-nav.ppm \
    "file://$PWD/tests/fixtures/nav_site/index.html" && file /tmp/toybrowser-nav.ppm
```

Window demo (needs a display):

```sh
"$BUILD"/toybrowser --size 400x200 "file://$PWD/tests/fixtures/nav_site/index.html"
# click "About page", scroll with the wheel/arrows/PageUp/PageDown/Home/End, Alt+Left/Right for
# history; the window title follows <title>.
```

## Next round: M8 — real fonts and images (needs approval)

- Goal: real TTF text and `<img>` decoding, either with approved libraries or with our own minimal
  implementations. The choice is open question 3 in [open-questions.md](open-questions.md) and needs
  the user's approval before any dependency or decoder work (see [dependencies.md](dependencies.md)).
- Preliminary scope: `render` gains a font backend (metrics and glyph rasterization replacing or
  extending the built-in 8x16 bitmap font) and an image decoder plus a `DrawImage` display-list
  command; `layout` keeps consuming text metrics only; `paint` emits images; `net`/`shell` add the
  URL/base-fetch path for image sources (external `<link>` CSS stays separate).
- Still out of scope: JavaScript (M9), forms, `https`/TLS, proxies.
- Acceptance sketch: a page with real font text and at least one image renders; determinism/pixel
  tests and all existing tests keep passing under both compilers.
- Do not start M8 without the user's decision on the font/image strategy.

Backlog after M8 (details in [roadmap.md](roadmap.md)): image/font library decisions, proxy support,
TLS, and test-helper migration.

## Map of the repository

- `src/<module>/` — one CMake library target per module (`base` does not exist yet; see
  [architecture.md](architecture.md) for the module map and dependency direction).
- `src/main.cpp` — CLI parsing only; `shell` orchestrates fetch → parse → style → layout → paint →
  dump/present, and `toy/shell/browser.h` owns one browser session (history, scroll, clicks, title).
- `src/render/include/toy/render/font.h` — bitmap-font metrics; `layout` consumes them through a
  `shell`-supplied callback and has no `render` dependency. `src/render/include/toy/render/paint.h`
  is the display list + rasterizer; `src/paint/` builds the list from the box tree and never
  rasterizes.
- `tests/<module>/` — one executable per subject, registered with CTest; new tests use the
  header-only `tests/support/check.h`. `tests/fixtures/nav_site/` is the committed three-page demo
  and navigation-test site.
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
  M7 painting: a non-empty page is wrapped in exactly one `PushClip(0, 0, viewport)`/
  `PopClip` pair and every command's y is translated by `-scroll_y` (integer device pixels, x
  unchanged); a scroll-0 render must stay byte-identical to M6.
- Navigation/session contract (`toy/shell/browser.h`): one history entry per successful load,
  failed loads change nothing, every successful load resets the scroll offset to 0, a reference
  that only changes the fragment is a no-op, the title is the first `<title>` text (collapsed,
  falling back to the serialized final URL), and only `http`/`file` links navigate.
- Platform input contract (`toy/platform/window.h`): `kMouseDown` is the primary button in window
  device pixels, `kMouseWheel` carries `wheel_delta` (positive = towards the document end), and
  `kKeyDown` carries the normalized `Key` plus alt/ctrl/shift; `SetTitle` is best effort and never
  fails the window.
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
- Mouse/keyboard behavior cannot be unit-tested headlessly. The real-window smoke test launches the
  browser on a display, finds the *client* window with `xwininfo -root -tree` (frames and clients
  both carry the title; capture the exact-size client), captures it with ImageMagick
  `import -window <id>`, and injects clicks/wheel/keys with a throwaway `.tmp` XSendEvent helper
  (libX11 only, never committed). Re-run it after touching the event loop.
- Click coordinates are window device pixels: add the scroll offset before `layout::HitTest`.
  Hit-testing returns the last pre-order box, which may be an anonymous or text box, so `shell`
  walks box parents to a DOM origin and then DOM ancestors to find an `href`.
- The navigation test hardcodes the fixture's link coordinates taken from a
  `toybrowser --size 400x200 --dump-boxes` run; changing the fixture or the layout constants means
  updating those coordinates and the expected content height.
- `paint` always wraps a non-empty page in one viewport clip pair now; older statements about M6
  emitting no clip commands are obsolete. The clip does not change scroll-0 pixels.
- Scroll resets to 0 on every successful load and fragment links are deliberate no-ops (documented
  M7 limits); do not 'fix' either without a contract change.
- The window loop rebuilds the layout and display list on expose, resize and scroll. That is
  correct and deliberately simple for the MVP; only optimize it with evidence.
- The built-in 8x16 glyph table is hand-authored and its shapes are reviewed visually (the user's
  preference): there is deliberately no frozen glyph-bitmap/fingerprint test. Changing a glyph is a
  visual change — render a sample and look at it before committing.
