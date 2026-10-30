# Open questions

Decisions that need the user. Each one names the milestone where the answer is needed; until then
the headless or own-implementation path is used.

## 1. Windowing backend — needed at M1

**Question:** approve linking an X11 client library (libX11 is the simplest; libxcb is the
alternative), or keep the window out and work headless-only for a while?

**Recommendation:** approve `libX11` at M1. The dependency-free headless backend is written
anyway and stays the base for tests; X11 only adds the visible window.

## 2. HTTPS / TLS — needed at M2

**Question:** should `https://` be supported eventually, and if so with an approved TLS library
(e.g. OpenSSL) or not at all?

**Recommendation:** no TLS for now: support `http://` and `file://` only. TLS from scratch is out
of proportion for a toy project; revisit when real pages become the goal.

## 3. Text and fonts — needed at M8

**Question:** approve `freetype`, or write a minimal TTF loader ourselves?

**Recommendation:** start with the built-in bitmap font (M1–M7), then decide at M8. A minimal
glyph loader (`cmap`, `glyf`, `hmtx`, no hinting, no complex shaping) is feasible and much more in
the spirit of the project.

## 4. Images — needed at M8

**Question:** approve `libpng` + `zlib`, or implement inflate and PNG decoding ourselves?

**Recommendation:** implement our own (inflate + PNG filters). It is a good learning exercise and
keeps the dependency count at zero; `libpng` remains the fallback if it drags on.

## 5. JavaScript scope — needed at M9

**Question:** confirm that a small JavaScript engine is wanted, and at what ambition level.

**Recommendation:** tiny ES5-like subset: values, functions, objects/arrays, `if`/`for`/`while`,
a few built-ins, DOM lookups and event handlers. No async, no modules, no modern syntax.

## 6. External test corpora — optional

**Question:** may we download test data later (e.g. html5lib-tests or WPT subsets) as golden
files? These are test fixtures, not runtime dependencies.

**Recommendation:** not before M3; start with hand-written fixtures, ask before downloading
anything.
