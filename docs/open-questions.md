# Open questions

Decisions that still need the user. Everything approved so far is recorded in
[dependencies.md](dependencies.md) and [roadmap.md](roadmap.md).

The HTTPS/TLS question was resolved on 2026-10-31: the user approved OpenSSL for the planned M9
HTTPS milestone (see [dependencies.md](dependencies.md) and [roadmap.md](roadmap.md)); the HTTP
proxy is planned as M10 and does not need a new dependency.

## 1. External test corpora — optional, not needed so far

**Question:** may we download test data later (e.g. html5lib-tests or WPT subsets) as golden
files? These are test fixtures, not runtime dependencies.

**Recommendation:** start with hand-written fixtures; ask before downloading anything.
