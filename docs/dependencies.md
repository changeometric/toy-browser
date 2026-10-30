# Dependencies

The policy itself is in [AGENTS.md](../AGENTS.md). This file records what is actually allowed and
what is still being requested.

## Currently approved

- **None.** Only the C++20 standard library and standard OS/POSIX interfaces are in use.

## System interfaces are still dependencies

Anything that talks to the display server (an X11 client library, for example) counts as a
third-party dependency for this project and needs approval. Until then, the headless backend —
rendering into memory and writing a PPM file — keeps the whole pipeline testable without a display.

## Pending candidates

| Candidate | Purpose | From-scratch alternative | Rough cost | Status |
| --- | --- | --- | --- | --- |
| libX11 | Window, input, presenting pixels | Implement the X11 wire protocol ourselves | Medium; protocol quirks, XShm fallback | **Awaiting approval** (needed for M1's window) |
| libxcb | Same as above, more modern API | Same | Medium | **Awaiting approval** (alternative to libX11) |
| freetype | TTF glyph rasterization and metrics | Own subset TTF loader (cmap/glyf/hmtx, no hinting) | Medium | **Not now** — bitmap font first, decide at M8 |
| libpng + zlib | PNG decoding | Own inflate + PNG filters/decoder | ~600–900 lines, very educational | **Not now** — decide at M8 |
| OpenSSL | TLS, i.e. `https://` | Not realistic from scratch | High | **Deferred** — HTTP/file only until decided |
| stb_truetype / stb_image | Drop-in single-header implementations | Own implementations | Low | **Not approved** — conflicts with the project's spirit |
| harfbuzz / fontconfig / icu | Complex shaping, font discovery, Unicode data | Basic shaping and ASCII/Latin first | High | **Not planned** |

Nothing in this table is linked or `find_package`d yet. Adding any row to the build requires the
process below to be completed first.

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
