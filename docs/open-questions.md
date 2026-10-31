# Open questions

Decisions that still need the user. Everything approved so far is recorded in
[dependencies.md](dependencies.md) and [roadmap.md](roadmap.md).

The HTTPS/TLS question was resolved on 2026-10-31: the user approved OpenSSL, and M9 (most-basic
HTTPS) shipped with it (see [dependencies.md](dependencies.md) and [roadmap.md](roadmap.md)).
M12 (external stylesheets) and M13 (resource loading pipeline) both shipped without a new
dependency or an open decision (M13 links the already-approved zlib directly). The next planned
milestone is M14 (CSS compatibility batch 1), followed by M15 (flexbox/position/float subset). The
HTTP proxy (M16) is deprioritized by the user; none of the planned milestones needs a new
dependency.

## 1. External test corpora — optional, not needed so far

**Question:** may we download test data later (e.g. html5lib-tests or WPT subsets) as golden
files? These are test fixtures, not runtime dependencies.

**Recommendation:** start with hand-written fixtures; ask before downloading anything.
