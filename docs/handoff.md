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
| M8a | PNG images: `render::Image` + libpng decoder behind `render::DecodePng`, `DrawImage` display-list command/rasterizer, replaced inline `<img>` layout from caller-supplied intrinsic sizes, private shell page loader (fetch/resolve/decode/skip failures), image fixtures and `tests/fixtures/img_site/`, 46 tests |
| Review round | Fixed unbounded `--size`, file body cap, dump indentation; 20 CTest tests |

Not implemented yet: real TTF fonts (M8b), JavaScript (M9), `https`/TLS, proxies, external
stylesheets, borders, anti-aliased text, horizontal scrolling/scrollbars, hover/active link styling
and form controls. PNG images work since M8a (see the M8a known limits in [roadmap.md](roadmap.md)). The window and
`--screenshot` paint a real page when a URL is given and the M1 placeholder page otherwise; with a
page URL the window is also browsable (links, wheel/keyboard scrolling, Alt+Left/Right history,
`<title>`). The `--dump-*` modes remain the primary debugging path.

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

Expected: 46/46 tests pass; building with `-DTOYBROWSER_WERROR=ON` is warning-free on GCC and
Clang; dumps print deterministic trees; screenshots write a binary PPM (the URL form paints the
real page, the URL-less form keeps the placeholder). All headless modes need no display. CMake 3.20
or newer is required; building needs the X11 development headers and the libpng development
headers (both approved dependencies); only windowed mode needs a running display.

```sh
"$BUILD"/toybrowser --size 320x200 --screenshot /tmp/toybrowser-img.ppm \
    "file://$PWD/tests/fixtures/img_site/index.html" && file /tmp/toybrowser-img.ppm
"$BUILD"/toybrowser --size 320x200 --dump-boxes \
    "file://$PWD/tests/fixtures/img_site/index.html" | grep '<img>'
```

Expected: the screenshot renders the committed PNG fixtures (flags, alpha, gray, palette) and the
box dump contains `<img> inline` boxes with their intrinsic/CSS sizes; `missing.png` reserves no
box.

```sh
"$BUILD"/toybrowser --size 400x200 --screenshot /tmp/toybrowser-nav.ppm \
    "file://$PWD/tests/fixtures/nav_site/index.html" && file /tmp/toybrowser-nav.ppm
```

Window demo (needs a display):

```sh
"$BUILD"/toybrowser --size 320x600 "file://$PWD/tests/fixtures/img_site/index.html"
"$BUILD"/toybrowser --size 400x200 "file://$PWD/tests/fixtures/nav_site/index.html"
# img page: check the PNG images are painted; nav page: click "About page", scroll with the
# wheel/arrows/PageUp/PageDown/Home/End, Alt+Left/Right for history; the title follows <title>.
```

## Next round: M8b — real fonts (libraries approved)

- Goal: real TTF text: freetype for metrics and glyph rasterization, harfbuzz for shaping and
  fontconfig for installed-font discovery/matching. The user approved all three libraries on
  2026-10-31 and clarified that "from scratch" applies to browser functionality, not low-level
  codecs/font machinery (recorded in [dependencies.md](dependencies.md) and
  [decisions.md](decisions.md)). Development packages must be installed by the user where missing;
  agents never install anything.
- Preliminary scope: `render` gains a font backend (loading, metrics, shaped advances, anti-aliased
  glyph coverage) replacing or extending the built-in 8x16 bitmap font on the TTF path; `layout`
  keeps consuming text metrics through the existing `MeasureText` callback (baseline information
  may be added to the contract); `paint` places real glyphs and paint emits coverage text; font
  selection/fallback (a default or configured font) is frozen at the start of the round. Bitmap-font
  output for pages that do not opt into a TTF font should stay unchanged.
- Tests must stay deterministic and must not depend on installed fonts: a committed/generated font
  fixture drives the tests; real installed fonts are for manual demos only.
- Done when a page with real TTF text and at least one image renders (the original M8 acceptance),
  with all existing tests (46), GCC/Clang warning-free builds and M8a image behavior green.
- Still out of scope: JavaScript (M9), forms, `https`/TLS, proxies, external CSS.

Backlog after M8b (details in [roadmap.md](roadmap.md)): proxy support, TLS, more image formats/
background images, and test-helper migration.

## Map of the repository

- `src/<module>/` — one CMake library target per module (`base` does not exist yet; see
  [architecture.md](architecture.md) for the module map and dependency direction).
- `src/main.cpp` — CLI parsing only; `shell` orchestrates fetch → parse → style → images → layout →
  paint → dump/present, and `toy/shell/browser.h` owns one browser session (history, scroll, clicks,
  title).
- `src/shell/page.h` — shell-private page loader (final URL, DOM, styles, box tree, decoded images);
  `src/render/include/toy/render/image.h` — `Image` and the libpng-backed `DecodePng`; the display
  list + rasterizer live in `toy/render/paint.h`, and `src/paint/` builds the list from the box tree
  without rasterizing. `src/render/include/toy/render/font.h` — bitmap-font metrics; `layout`
  consumes them through a `shell`-supplied callback and has no `render` dependency.
- `tests/<module>/` — one executable per subject, registered with CTest; new tests use the
  header-only `tests/support/check.h`. Committed fixtures: `tests/fixtures/nav_site/` (three-page
  navigation demo), `tests/fixtures/img_site/` (image pipeline demo), `tests/fixtures/images/`
  (hand-generated PNGs with documented pixel content).
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
- Layout semantics frozen through the layout tests and the M5/M8a known limits: viewport width from
  `--size`, no margin collapsing, top-aligned lines, word-level text boxes, ASCII-only line
  breaking. M8a replaced elements: `LayoutOptions::replaced_sizes` (DOM node id → intrinsic CSS px)
  makes an inline element an atomic inline-level box; CSS `width`/`height` px override the
  intrinsic size, one specified side keeps the intrinsic ratio, the box wraps as a whole, raises
  the line height and is top-aligned; its dump line stays `<tag> inline x= y= width= height=`, it
  has no children, `display:block` replaced elements are unsupported, and margins/padding on
  replaced boxes are ignored. A page without entries keeps its old geometry byte for byte.
- Painting semantics frozen through the M6/M7 tests: display-list commands are integer device pixels
  (`FillRect`, `DrawText`, `DrawImage`, `PushClip`/`PopClip`); fills use the SrcOver formula
  `(src * a + dst * (255 - a) + 127) / 255` over an opaque destination; the initial clip is the
  whole surface and rasterizer work is bounded by the clip; the shell fills the canvas white; paint
  order is box-tree pre-order (background, then image, then children/text); text uses the built-in
  8x16 ASCII glyphs with nearest-neighbour scaling, no anti-aliasing, `?` for non-ASCII, and
  opaque text pixels. M7 painting: a non-empty page is wrapped in exactly one
  `PushClip(0, 0, viewport)`/`PopClip` pair and every command's y is translated by `-scroll_y`
  (integer device pixels, x unchanged); a scroll-0 render must stay byte-identical to M6/M7 when
  the page has no images. M8a images: one `DrawImage` per inline replaced box whose node id is in
  the `paint::ImageMap`, emitted after that box's background; the command holds a
  `shared_ptr<const render::Image>`; destination pixel (dx, dy) samples source
  `((dx - x) * image.width / width, (dy - y) * image.height / height)` with integer floor division
  (nearest-neighbour, no interpolation/AA), composited with the same SrcOver formula (alpha 0 no-op,
  alpha 255 writes `0xFF000000 | rgb`, result opaque); an empty image map reproduces the pre-M8a
  command list exactly.
- Image decoding contract (`render::DecodePng`): PNG only, via the approved libpng dependency behind
  the interface, output straight-alpha ARGB `render::Image`; accepted sizes are each side at most
  8192 px and at most 16,777,216 pixels in total (inclusive maxima), rejected before allocation;
  failures return false with a human-readable error and leave `*out` untouched; no exceptions; no
  libpng type crosses a public header.
- Shell page loader contract (`src/shell/page.h`, private): after fetching/parsing/styling, load
  every `<img>` in document order (iterative walk), take its first `src`, resolve it against the
  page's final URL, fetch it with `net::Fetch` (http/file only), decode it, and keep
  `layout::ReplacedSizes` + `paint::ImageMap` keyed by the img DOM node id. Missing/failed/unsupported
  images are skipped silently (no broken-image UI, no stderr, page load still succeeds, the element
  lays out as an empty inline); there is no image cache (one fetch per occurrence) and images are
  fetched even if the element would not be visible. `--dump-boxes` uses the same loader;
  `--dump-dom`/`--dump-style` do not fetch images.
- Navigation/session contract (`toy/shell/browser.h`): one history entry per successful load,
  failed loads change nothing, every successful load resets the scroll offset to 0, a reference
  that only changes the fragment is a no-op, the title is the first `<title>` text (collapsed,
  falling back to the serialized final URL), and only `http`/`file` links navigate. M8a keeps the
  public methods identical but hides the page (DOM/styles/boxes/images) behind a forward
  declaration, an out-of-line destructor and a `std::unique_ptr`, so the header no longer exposes
  the page data and `Browser` is non-copyable.
- Platform input contract (`toy/platform/window.h`): `kMouseDown` is the primary button in window
  device pixels, `kMouseWheel` carries `wheel_delta` (positive = towards the document end), and
  `kKeyDown` carries the normalized `Key` plus alt/ctrl/shift; `SetTitle` is best effort and never
  fails the window.
- Module boundaries and dependency direction in [architecture.md](architecture.md).
- `--size` is clamped to 1..8192 per side and at most 16M pixels; decoded images use the same
  inclusive bounds (each side at most 8192 px, at most 16,777,216 pixels in total); bodies are
  capped by `FetchOptions::max_body_bytes` (16 MiB by default); dump indentation is capped at 64
  levels.
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
- libpng is a required build dependency now (`find_package(PNG REQUIRED)` in
  `src/render/CMakeLists.txt`); a build without its development headers fails at configure time.
  Do not hardcode versions or paths, and keep `png.h` out of public headers.
- PNG decoding goes through `render::DecodePng` only. Sizes are validated before allocation, the
  output is straight-alpha ARGB, and failures are ordinary errors. The hand-generated fixtures in
  `tests/fixtures/images/` document their exact pixel content; keep the README and the tests in
  sync if a fixture ever changes.
- Image-less dumps and screenshots must stay byte-identical to M7 and image PPMs must be identical
  between GCC and Clang: `cmp` a fresh build against a `git worktree` of the previous commit at the
  end of any round that touches painting. This M7 gate has already caught drift cheaply.
- Layout and paint must receive images keyed by the same DOM node ids (`layout::ReplacedSizes` and
  `paint::ImageMap`); `shell` builds both from one load. `RelayoutPage` reuses the stored sizes and
  decoded images and never refetches on resize.
- A display list image command owns a `shared_ptr<const render::Image>`; a built list may outlive
  the page, so never keep a raw image pointer in a command.
- The image tests (`shell.images*`, and the dump regexes for `tests/fixtures/img_site/`) hardcode
  geometry taken from `toybrowser --size 320x200 --dump-boxes`, the same way the navigation test
  hardcodes link coordinates: changing the fixture, the replaced-element layout or the text metrics
  means updating those constants in the same change.
- `shell.images_screenshot` is a `cmake -P` helper that only checks exit status, the P6 header and
  the file size; the pixel content of the same page is asserted by `shell.images`.
- `--dump-boxes` now loads and decodes the page's images too (that is how `<img>` boxes appear);
  image failures are still skipped, so the dump never fails because of a broken image.
- Image loading has no cache and no lazy loading: every `<img>` occurrence is fetched, even one
  hidden by `display:none` or far below the viewport. That is a documented M8a MVP limit, not a
  bug; only add caching/lazy loading with a contract change.
