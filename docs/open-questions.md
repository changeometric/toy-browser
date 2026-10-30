# Open questions

Decisions that still need the user. Everything approved so far is recorded in
[dependencies.md](dependencies.md) and [roadmap.md](roadmap.md).

## 1. HTTPS / TLS — needed at M2

**Question:** should `https://` be supported eventually, and if so with an approved TLS library
(e.g. OpenSSL) or not at all?

**Recommendation:** no TLS for now: support `http://` and `file://` only. TLS from scratch is out
of proportion for a toy project; revisit when real pages become the goal. OpenSSL is currently not
approved. The user deferred TLS (and proxy support) on 2026-10-31; note that proxy support does not
remove this decision: sites such as Google are only reachable over `https`.

## 2. External test corpora — optional, from M3

**Question:** may we download test data later (e.g. html5lib-tests or WPT subsets) as golden
files? These are test fixtures, not runtime dependencies.

**Recommendation:** start with hand-written fixtures; ask before downloading anything.

## 3. Image / font strategy — needed at M8

**Question:** the libraries (libpng/zlib, freetype) are approved, but should we use them or write
our own minimal PNG/TTF support first?

**Recommendation:** implement a minimal PNG decoder (own inflate) and a minimal TTF loader first,
because that is the educational core of the project; fall back to the approved libraries if it
drags on.
