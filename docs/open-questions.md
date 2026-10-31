# Open questions

Decisions that still need the user. Everything approved so far is recorded in
[dependencies.md](dependencies.md) and [roadmap.md](roadmap.md).

The HTTPS/TLS question was resolved on 2026-10-31: the user approved OpenSSL, and M9 (most-basic
HTTPS) shipped with it (see [dependencies.md](dependencies.md) and [roadmap.md](roadmap.md)). The
next milestone is M11 (HTTP proxy, deprioritized by the user), which does not need a new
dependency.

## 1. External test corpora — optional, not needed so far

**Question:** may we download test data later (e.g. html5lib-tests or WPT subsets) as golden
files? These are test fixtures, not runtime dependencies.

**Recommendation:** start with hand-written fixtures; ask before downloading anything.
