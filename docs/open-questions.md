# Open questions

Decisions that still need the user. Everything approved so far is recorded in
[dependencies.md](dependencies.md) and [roadmap.md](roadmap.md).

## 1. HTTPS / TLS — deferred by the user

**Question:** should `https://` be supported eventually, and if so with an approved TLS library
(e.g. OpenSSL) or not at all?

**Recommendation:** no TLS for now: support `http://` and `file://` only. TLS from scratch is out
of proportion for a toy project; revisit when real pages become the goal. OpenSSL is currently not
approved. The user deferred TLS (and proxy support) on 2026-10-31; note that proxy support does not
remove this decision: sites such as Google are only reachable over `https`.

## 2. External test corpora — optional, not needed so far

**Question:** may we download test data later (e.g. html5lib-tests or WPT subsets) as golden
files? These are test fixtures, not runtime dependencies.

**Recommendation:** start with hand-written fixtures; ask before downloading anything.
