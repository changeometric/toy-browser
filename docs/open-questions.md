# Open questions

Decisions that still need the user. Everything approved so far is recorded in
[dependencies.md](dependencies.md) and [roadmap.md](roadmap.md).

The HTTPS/TLS question was resolved on 2026-10-31: the user approved OpenSSL, and M9 (most-basic
HTTPS) shipped with it (see [dependencies.md](dependencies.md) and [roadmap.md](roadmap.md)).
M12 (external stylesheets), M13 (resource loading pipeline), M14 (CSS compatibility batch 1,
delivered as M14a values & typography and M14b box decorations & constraints) and M17 (JPEG image
support) shipped without an open decision. M13 links the already-approved zlib directly; M17 added
libjpeg-turbo, explicitly approved by the user on 2026-10-31 and recorded in
[dependencies.md](dependencies.md). The next planned milestone is M15 (flexbox/position/float
subset), whose scope must be re-confirmed with the maintainer before starting; it needs no new
dependency. The HTTP proxy (M16) is deprioritized by the user.

## 1. External test corpora — optional, not needed so far

**Question:** may we download test data later (e.g. html5lib-tests or WPT subsets) as golden
files? These are test fixtures, not runtime dependencies.

**Recommendation:** start with hand-written fixtures; ask before downloading anything.
