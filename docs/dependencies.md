# Dependencies

The policy itself is in [AGENTS.md](../AGENTS.md). This file records what is actually allowed and
what is still being requested.

## Inventory today

**Five third-party packages are linked:**

- **libX11 (MIT/X11)**, used by the X11 window backend (M1).
- **libpng + zlib (libpng license / zlib license)**, used by `render` to decode PNG images (M8a).
  Discovered with CMake's `find_package(PNG REQUIRED)`; zlib is pulled in by libpng.
- **freetype (FreeType License)**, used by `render` for real-font metrics and glyph rasterization
  (M8b). Discovered with `find_package(Freetype REQUIRED)`.
- **harfbuzz (MIT)**, used by `render` for OpenType text shaping (M8b). Discovered with
  `find_package(harfbuzz CONFIG REQUIRED)`.
- **fontconfig (MIT-style)**, used by `shell` for installed-font discovery and matching when a page
  is rendered with `--font-family` (M8b). Discovered with `find_package(Fontconfig REQUIRED)`.

Everything else is the C++20 standard library and standard POSIX interfaces. No HTML parser, CSS
engine, JS engine, layout engine or browser engine is used — those are written in this repository.

## Currently approved

Approved by the user on 2026-10-31, when M1 started:

- **libX11** — windowing, input and pixel presentation on Linux, used from M1. License: MIT/X11.
  Linked through CMake's `find_package(X11)`; no vendoring. Introduced in commit `0572b8b` (M1).
- **libpng + zlib** — image decoding, used from M8a. Licenses: libpng license and zlib license.
  Linked through `find_package(PNG REQUIRED)` in `src/render/CMakeLists.txt`; introduced in the
  M8a round.
- **freetype** — font metrics and glyph rasterization for real TTF text, used from M8b. License:
  FreeType License. Linked through `find_package(Freetype REQUIRED)` in `src/render/CMakeLists.txt`.
- **libxcb** — allowed as an alternative windowing library; currently unused.

Approved by the user on 2026-10-31, before M8a started, and linked in M8b:

- **harfbuzz** — OpenType text shaping (font-aware glyph selection and positioning) for real-font
  text. License: MIT. Linked through `find_package(harfbuzz CONFIG REQUIRED)` in
  `src/render/CMakeLists.txt`.
- **fontconfig** — installed-font discovery and matching, used by the shell to resolve
  `--font-family NAME` to a font file. License: fontconfig's MIT-style license. Linked through
  `find_package(Fontconfig REQUIRED)` in `src/shell/CMakeLists.txt`.

Nothing else is linked. The C++20 standard library and standard OS/POSIX interfaces remain the
default. All approved candidates use permissive licenses compatible with the project's MIT license.

## Scope of "from scratch"

The user clarified on 2026-10-31 that "from scratch" applies to **browser functionality** —
networking, HTML/CSS parsing, layout, painting and JavaScript — not to low-level codecs and font
machinery. Image decoding and font rasterization/shaping/discovery therefore use the approved
libraries above instead of hand-written implementations. The browser engine parts stay ours.

## System interfaces are still dependencies

Anything that talks to the display server (an X11 client library, for example) counts as a
third-party dependency for this project and needs approval. libX11 is approved for the window; the
headless backend — rendering into memory and writing a PPM file — keeps the whole pipeline testable
without a display.

## Dependency candidates

| Candidate | Purpose | From-scratch alternative | Rough cost | Status |
| --- | --- | --- | --- | --- |
| libX11 | Window, input, presenting pixels | Implement the X11 wire protocol ourselves | Medium; protocol quirks, XShm fallback | **Approved, in use from M1** (plain `XPutImage` first; XShm later if needed) |
| libxcb | Same as above, more modern API | Same | Medium | **Approved, unused** (alternative to libX11) |
| libpng + zlib | PNG decoding | Own inflate + PNG filters/decoder | ~600–900 lines | **Approved, linked since M8a** (used by `render::DecodePng`) |
| freetype | TTF glyph rasterization and metrics | Own subset TTF loader (cmap/glyf/hmtx, no hinting) | Medium | **Approved, linked since M8b** (used by `render::Font`) |
| harfbuzz | OpenType shaping (ligatures, glyph selection, positioning) | Basic Latin shaping only | High | **Approved, linked since M8b** (used by `render::Font`) |
| fontconfig | Installed-font discovery and matching | Hardcoded font path / no discovery | Medium | **Approved, linked since M8b** (used by `shell` for `--font-family`) |
| OpenSSL | TLS, i.e. `https://` | Not realistic from scratch | High | **Deferred, not approved** — HTTP/file only until decided |
| stb_truetype / stb_image | Drop-in single-header implementations | Own implementations | Low | **Not approved** — conflicts with the project's spirit |
| icu | Unicode data and full internationalization | Small ASCII/Latin subset first | High | **Not planned** |

Only the five packages listed at the top are linked today. Any other row needs the process below to
be completed first.

## Approval process

1. The agent states: purpose, license, size and maintenance burden, and the from-scratch
   alternative.
2. The user answers yes or no.
3. If yes: record library, package/version, date and the commit that introduced it in this file;
   link it through CMake (`find_package` or `pkg-config`), no vendoring unless the user asks.
4. If the package is missing from the machine, ask the user to install it — agents never install
   anything themselves.
5. If no: implement the from-scratch alternative or defer the feature.

## Rules that keep the tree portable

- No absolute paths, no hardcoded library versions or package locations in code or CMake.
- Libraries are discovered only through CMake find modules or pkg-config, and only after approval.
- Optional features must degrade gracefully: the tests pass without optional dependencies.
  Headless runs need no display, but the current binary still links libX11 (the window backend is
  always built), libpng (image decoding), freetype and harfbuzz (real-font rendering, part of
  `render`) and fontconfig (the shell's `--font-family` path). These are required build
  dependencies; a build needs their development headers.
- Do not assume what fonts, window manager, GPU or packages a machine has. Tests use the committed
  self-authored font fixture (`tests/fixtures/fonts/`), never installed fonts.
