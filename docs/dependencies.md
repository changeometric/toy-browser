# Dependencies

The policy itself is in [AGENTS.md](../AGENTS.md). This file records what is actually allowed and
what is still being requested.

## Currently approved

Approved by the user on 2026-10-31, when M1 started:

- **libX11** — windowing, input and pixel presentation on Linux, used from M1. License: MIT/X11.
  Linked through CMake's `find_package(X11)`; no vendoring.
- **libpng + zlib** — allowed for image decoding, intended for M8. Not linked yet.
- **freetype** — allowed for font rasterization and metrics, intended for M8. Not linked yet.
- **libxcb** — allowed as an alternative windowing library; currently unused.

Nothing else is linked. The C++20 standard library and standard OS/POSIX interfaces remain the
default.

## System interfaces are still dependencies

Anything that talks to the display server (an X11 client library, for example) counts as a
third-party dependency for this project and needs approval. libX11 is now approved for the window;
the headless backend — rendering into memory and writing a PPM file — keeps the whole pipeline
testable without a display.

## Dependency candidates

| Candidate | Purpose | From-scratch alternative | Rough cost | Status |
| --- | --- | --- | --- | --- |
| libX11 | Window, input, presenting pixels | Implement the X11 wire protocol ourselves | Medium; protocol quirks, XShm fallback | **Approved, in use from M1** (plain `XPutImage` first; XShm later if needed) |
| libxcb | Same as above, more modern API | Same | Medium | **Approved, unused** (alternative to libX11) |
| freetype | TTF glyph rasterization and metrics | Own subset TTF loader (cmap/glyf/hmtx, no hinting) | Medium | **Approved for M8, not linked yet** — bitmap font first |
| libpng + zlib | PNG decoding | Own inflate + PNG filters/decoder | ~600–900 lines, very educational | **Approved for M8, not linked yet** |
| OpenSSL | TLS, i.e. `https://` | Not realistic from scratch | High | **Deferred, not approved** — HTTP/file only until decided |
| stb_truetype / stb_image | Drop-in single-header implementations | Own implementations | Low | **Not approved** — conflicts with the project's spirit |
| harfbuzz / fontconfig / icu | Complex shaping, font discovery, Unicode data | Basic shaping and ASCII/Latin first | High | **Not planned** |

Only libX11 is linked today. Any other row needs the process below to be completed first.

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
- Optional features must degrade gracefully: the build and tests pass without any optional
  dependency, and the headless path stays dependency-free.
- Do not assume what fonts, window manager, GPU or packages a machine has.
