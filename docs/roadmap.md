# Roadmap

Each milestone is a minimal vertical slice: small, buildable, demoable and tested. A milestone is
done when its demo command works, new logic has tests, and the docs are still accurate.

**Current status: M0–M13 are done (M13 = resource loading pipeline). M14 (CSS compatibility batch 1)
is the next planned milestone; M15 (flexbox/position/float subset) follows it. The HTTP proxy (M16)
and the JavaScript subset are deferred.** For a snapshot and takeover guide, read
[handoff.md](handoff.md).

## M0 — Design and skeleton ✅ (done)

- Documentation, buildable CMake skeleton, stub executable, toolchain smoke test.
- Done when `cmake --build` and `ctest` pass and the user has accepted the docs.

## M1 — Window, event loop, first pixels ✅ (done)

- `render`: `Surface`, solid rectangle fill, PPM writer.
- `platform`: X11 window backend (open/close, resize, expose, key events, present an ARGB surface),
  graceful failure without a display.
- `shell` + CLI: placeholder page, window event loop, `--headless --screenshot`, `--size`.
- Acceptance reached: window shows the placeholder page, the same frame can be dumped headless
  with no display, and `render.surface`, `render.ppm`, `platform.window_requires_display`,
  `shell.headless_screenshot` all pass under GCC and Clang with warnings as errors.

## M2 — Fetch a URL ✅ (done)

- URL parsing for absolute `http` and `file` URLs: percent decoding, default ports, no base
  resolution yet.
- `net` module: blocking HTTP/1.1 GET over POSIX sockets, one request per connection, connect and
  read timeouts, `Content-Length` and chunked bodies, redirects (301/302/303/307/308), a 16 MiB
  body cap; `file://` reads the local file.
- CLI: `--dump-body <url>` writes the raw body to stdout. Everything is headless; no display needed.
- Acceptance reached: byte-identical dumps for `file://` and for a loopback HTTP server; error paths
  exit non-zero; `net.url`, `net.http`, `net.file`, `shell.dump_body_file` and
  `shell.dump_body_missing_file` pass under GCC and Clang with warnings as errors. No TLS at M2
  (HTTPS is planned in M9).
- Known M2 limits: a non-2xx response is still a successful fetch (the status is exposed in the API;
  error pages and navigation arrive later), and the shell-level end-to-end test covers `file://`
  while HTTP is covered by the in-process-server tests.

## M3 — HTML to DOM ✅ (done)

- `dom` module: self-written tokenizer and stack-based tree builder over an arena DOM, with error
  tolerance (malformed input never fails), a deterministic dump format, and roughly 40 golden dump
  cases plus entity tests.
- `--dump-dom <url>`: fetch through `net`, parse, dump the DOM to stdout; fully headless.
- Acceptance reached: `dom.parser`, `dom.entities`, `shell.dump_dom_file` and
  `shell.dump_dom_missing_file` pass under GCC and Clang with warnings as errors; a local HTTP page
  round-trips fetch → parse → dump.
- Known M3 limits (deliberate MVP subset): no HTML5 insertion modes (no implied end tags except at
  EOF, no foster parenting, no adoption agency), doctype only before `<html>`, no implicit head
  content routing (a `<title>` without an explicit `<head>` lands in body), comments are kept
  verbatim (so a source comment's spaces show up in the dump), whitespace text nodes are preserved
  with simple routing, entities are a small named set plus numeric forms (semicolon required,
  lowercase `x` only), no `<template>`, SVG or MathML namespaces, and input is assumed to be UTF-8.

## M4 — CSS and computed style ✅ (done)

- `css` module: own tokenizer/parser for rules, declarations and simple selectors (type, class, id,
  universal, descendant/child combinators, comma groups), skipping at-rules and unsupported
  syntax, tolerant of malformed input.
- `style` module: embedded UA stylesheet parsed by our own CSS parser, selector matching,
  UA/author/inline cascade with specificity and `!important`, inheritance, and the supported
  property set (display, color, background-color, font-size px/em, font-weight, font-style,
  margin/padding longhands and 1-4 value shorthands, width/height auto or px/em).
- `--dump-style <url>`: deterministic per-element computed-style tree on stdout, fully headless.
- Acceptance reached: `css.parser`, `style.cascade`, `style.dump`, `shell.dump_style_file` and
  `shell.dump_style_missing_file` pass under GCC and Clang with warnings as errors (20 tests
  total).
- Known M4 limits: no external `<link>` stylesheets (planned as M12), no
  at-rule contents, no pseudo-classes/elements, no attribute selectors, no `rgb()`/`hsl()`/`calc()`,
  `inherit`/`initial` keywords or CSS variables, no `%`/`pt`/`rem` units, no `margin: auto`, and
  named colors use the standard values (e.g. green = #008000).

## M5 — Layout ✅ (done)

- `layout` module: arena `BoxTree` with absolute CSS-pixel geometry (border-box rectangles; `float`
  geometry, integers in dumps), block and inline boxes, anonymous blocks, block width/margins/
  padding, auto and fixed heights, line breaking with a caller-supplied measurement callback,
  inline-element fragmentation, `<br>`, and a deterministic box dump (indent capped at 64 levels).
- `render`: built-in monospace bitmap-font metrics (`toy/render/font.h`) on an 8x16 design grid at
  a 16 px em. M5 ships metrics only; glyph painting arrives with M6.
- `shell`/CLI: `--dump-boxes <url>` fetches, parses, styles, lays out and dumps; the viewport width
  is `--size`'s width (default 800). Fully headless.
- Acceptance reached: `render.font_metrics`, `layout.block`, `layout.inline`, `layout.line_break`,
  `layout.dump`, `layout.deep_nesting`, `shell.dump_boxes_file`, `shell.dump_boxes_size`,
  `shell.dump_boxes_missing_file`, `shell.dump_boxes_missing_url` and `shell.dump_boxes_conflict`
  pass under GCC and Clang with warnings as errors (31 tests total, deterministic and headless).
- Known M5 limits (MVP simplifications): no margin collapsing (a parent's auto height includes its
  in-flow children's margins); no baseline alignment (boxes on one line are top-aligned and share
  the line height); horizontal inline margins/padding only (vertical inline margins/padding and
  inline width/height are ignored); no `text-align`/`line-height`/`vertical-align`/`white-space`
  support, so `<pre>` collapses whitespace like everything else; line breaks happen only at ASCII
  whitespace (non-ASCII text is one word: no Unicode line breaking, hyphenation or emergency
  breaking); no percentage units; and layout paints nothing yet.

## M6 — Paint: first real page ✅ (done)

- `render`: flat display list (`FillRect`, `DrawText`, `PushClip`/`PopClip`) and a deterministic
  software rasterizer — SrcOver rectangle fill, integer clip stack, and a hand-authored 8x16 ASCII
  bitmap font drawn with nearest-neighbour cell scaling (no anti-aliasing, no font dependency).
- `paint` module: turns the box tree into a display list — element backgrounds first (pre-order),
  then one text draw per text box using the computed color/font size. It does not rasterize, and
  `render` still knows nothing about HTML/CSS.
- `shell`/CLI: optional positional page URL (`toybrowser [options] [url]`) used by window mode and
  `--screenshot`; the page is fetched, parsed, styled, laid out at the surface width, painted and
  presented/written as a PPM. Without a URL both modes keep the M1 placeholder page.
- Acceptance reached: `render.paint`, `paint.display_list`, `shell.screenshot_page` and the
  positional-URL CLI tests pass under GCC and Clang with warnings as errors (37 tests total); a
  pixel end-to-end test proves the red heading/black paragraph geometry on a 200x100 surface, and
  GCC/Clang render byte-identical PPMs; the window shows the same page.
- Known M6 limits: all painting is integer device pixels with no anti-aliasing or subpixel layout
  (1 CSS px = 1 device px); no borders (images arrived in M8a, real fonts are opt-in since M8b);
  the canvas is always filled white by the shell, and `html`/`body` `background-color` paints
  only its element box (no canvas background propagation); paint order is pre-order with no stacking
  contexts/z-index; text is ASCII bitmap glyphs only (other code points render `?`, and links are
  colored but not underlined because `text-decoration` is not in the property set); overflow is
  clipped by the surface only — scrolling and navigation arrive in M7; the rasterizer is bounded by
  the clip area, including for adversarial font sizes.

## M7 — Navigation ✅ (done)

- `shell::Browser`: one headless browser session owning the current page (final URL, DOM, styles,
  box tree), the navigation history, the vertical scroll offset and painting, so clicks, history,
  scrolling and titles are testable without a window.
- `net::ResolveUrl`: RFC 3986 resolution of relative references against the current page's final
  URL (http/file subset; file results drop the query/fragment this MVP cannot carry).
- `layout`: `HitTest` (the box painted last that contains a point) and `ContentHeight` (scrollable
  extent); `paint` wraps the page in a viewport `PushClip`/`PopClip` pair and translates every
  command by the scroll offset.
- `platform`: normalized input events (left click in window pixels, wheel notches, mapped keys with
  modifiers) and `SetTitle`; the X11 backend maps buttons/keysyms and publishes
  `_NET_WM_NAME`/`WM_NAME`.
- `shell` event loop: link clicks navigate, the wheel/arrow/PageUp/PageDown/Home/End keys scroll,
  Alt+Left/Right navigate history, and the window title follows `<title>` (falling back to the
  URL). `--screenshot` renders one frame through the same session at scroll 0; the `--dump-*` modes
  are unchanged and headless.
- A committed three-page fixture site (`tests/fixtures/nav_site/`) is browsable in the window and
  drives the headless `shell.navigation` test.
- Acceptance reached: `net.url_resolution`, `layout.hit_test`, the updated `paint.display_list`,
  `shell.navigation` and every pre-existing test pass under GCC and Clang with warnings as errors
  (40 tests total); scroll-0 screenshots and all existing renders are byte-identical to M6; a local
  multi-page site can be browsed in the window (click, back/forward, scroll, title).
- Known M7 limits: vertical scrolling only (no horizontal scroll, no scrollbars); a wheel notch is
  a fixed 3 lines (48 px) and scrolling is integer device pixels; navigation reloads the page (no
  cache), so Back/Forward reset the scroll offset to 0 instead of restoring it; fragment navigation
  does not scroll to anchors (fragment-only links are no-ops); only `http`/`file` links navigate
  (unsupported schemes report an error on stderr); a failed load keeps the current page and prints
  an error on stderr; no hover/active link styling, cursor changes, forms or in-page error pages.

## M8a — PNG images ✅ (done)

- `render`: `Image` (straight-alpha ARGB, size caps) and `DecodePng()` wrapping the approved libpng
  dependency behind a frozen interface (no libpng types leave the module; failures are ordinary
  errors; sizes are rejected before allocation). The display list gains `DrawImage` with shared
  image ownership, a frozen nearest-neighbour integer source mapping and the existing SrcOver/clip
  semantics; image-less pages produce exactly the pre-M8a command list.
- `layout`: inline elements with a caller-supplied intrinsic size (`LayoutOptions::replaced_sizes`,
  keyed by DOM node id) become atomic inline replaced boxes: CSS `width`/`height` (px) override the
  intrinsic size, one specified side keeps the intrinsic ratio, the box wraps as a whole, raises
  the line height and is top-aligned. `layout` still has no render dependency and pages without
  entries keep their old geometry and `<tag> inline` dump format.
- `paint`: emits one `DrawImage` per inline replaced box whose node id has a decoded image, after
  its background and before its children; empty image maps keep the M7 list.
- `shell`: a private page loader (final URL, DOM, styles, boxes, replaced sizes and image map)
  resolves each `<img src>` against the page's final URL, fetches it with the existing
  `net::Fetch` (http/file at M8a; https since M9), decodes it with `render::DecodePng`, and skips failed/missing
  images silently (no broken-image UI, no error output, no image cache). `shell::Browser` owns the
  page and its images, paints them, and re-lays-out on resize without refetching; `--dump-boxes`
  uses the same loader, so reserved image boxes appear in the box dump. The CLI is unchanged.
- Fixtures: hand-generated PNG files with documented pixel content
  (`tests/fixtures/images/`) and a committed `tests/fixtures/img_site/` demo page.
- Acceptance reached: `render.image`, `layout.replaced`, `shell.images`, the image CLI checks and
  every pre-existing test pass under GCC and Clang with warnings as errors (46 tests total);
  image-less dumps and screenshots are byte-identical to M7; GCC and Clang produce byte-identical
  image PPMs; the window shows the image page.
- Known M8a limits: PNG only (whatever libpng converts to 8-bit RGBA; no JPEG/GIF/WebP/SVG), no
  `background-image`, `srcset`, `alt` text or broken-image indicator, no image cache (a duplicate
  URL is fetched once per `<img>`), HTML `width`/`height` attributes are ignored (CSS px only),
  `display:block` replaced elements and margins/padding on replaced elements are unsupported,
  replaced boxes are top-aligned (no baseline alignment, as in M5), scaling is nearest-neighbour
  without anti-aliasing, images are fetched even when their element is not visible (no lazy
  loading), only `http`/`file` image URLs work at M8a (https arrived in M9), and
  `--dump-dom`/`--dump-style` do not fetch images.

## M8b — Real fonts ✅ (done)

- `render`: `Font` loads a TTF/OpenType face through the approved freetype dependency behind a
  frozen pimpl interface (no freetype/harfbuzz type in a public header) and exposes design-unit
  metrics plus harfbuzz-shaped advance widths. The display list gains `DrawTextWithFont`: real-font
  text is anti-aliased with 8-bit freetype coverage, placed on the command's baseline with a frozen
  per-glyph integer placement formula, composited with the existing SrcOver formula and bounded by
  the clip. The effective real-font size is clamped to `kMaxRealFontSizePx` (4096 px) for metrics
  and painting, so adversarial CSS cannot allocate unbounded glyph bitmaps. The built-in bitmap
  `DrawText` path is untouched.
- `paint`: a text box becomes `DrawTextWithFont` with baseline `round(box.y) - scroll_y +
  round(font.ascent)` when the session selected a real font; with no font it emits exactly the
  pre-M8b bitmap command.
- `shell`: `--font PATH` loads a font file, `--font-family NAME` resolves a family through the
  approved fontconfig dependency (best match; unknown families fall back), and the resolved font is
  loaded once and shared by layout metrics (through the existing `MeasureText` callback) and
  painting. The flags are mutually exclusive, rejected with `--dump-body`/`--dump-dom`/
  `--dump-style`, and a font that cannot be loaded is a runtime error (exit 1). One font applies to
  the whole session; without a flag the built-in bitmap font is still used, keeping all pre-M8b
  output byte-identical.
- Fixtures: a committed self-authored minimal TTF (`tests/fixtures/fonts/toy_test.ttf`, documented
  metrics/family) and a real-font demo page (`tests/fixtures/font_site/`). Tests never depend on
  installed fonts; the fontconfig path is tested with a test-local `FONTCONFIG_FILE` pointing only
  at the fixture fonts.
- Acceptance reached: a page with real TTF text and an image renders (headless screenshot and real
  window); `render.font_ttf`, the extended `render.paint`, `shell.fonts` and seven CLI/fontconfig
  checks plus every pre-existing test pass under GCC and Clang with warnings as errors (55 tests
  total); without a font flag every pre-M8b dump and screenshot is byte-identical to M8a; GCC and
  Clang produce byte-identical PPMs on both the bitmap and real-font paths.
- Known M8b limits: real fonts are opt-in (the default remains the ASCII bitmap font), one font
  face for the whole session (no per-element CSS `font-family`/`font-weight`/`font-style` mapping,
  no fallback chains, no `@font-face`/font downloads), no text shaping features beyond harfbuzz's
  default buffer shaping (no bidi reordering, vertical text or complex-script segmentation work),
  fontconfig always returns its best match so unknown families silently fall back, face 0 only
  (TTC/face index ignored), missing code points render the font's `.notdef` glyph, real-font sizes
  above 4096 px behave like 4096 px, and anti-aliased subpixel positioning/kerning beyond the
  shaped advances is not implemented. The bitmap fallback stays ASCII-only.

## M9 — Most-basic HTTPS ✅ (done)

- Goal met: fetch absolute `https://` URLs so pages and images from HTTPS sites render through the
  existing pipeline. The MVP bar stayed "connect and issue the request".
- `net` URL layer: `https` scheme with default port 443 and the same RFC 3986 authority/port/path
  rules as http; `ParseUrl`/`ResolveUrl`/`SerializeUrl` accept and round-trip it; scheme-relative
  references inherit the base scheme and explicit http<->https references are allowed.
- `net` transport: OpenSSL behind the unchanged blocking `Fetch()` (`find_package(OpenSSL REQUIRED)`
  linked PRIVATE to `toy_net`), sharing the HTTP/1.1 request/response code with plain HTTP through a
  private byte-stream abstraction (`Stream`, plain `SocketStream`, `TlsStream`) so body caps,
  header/chunked parsing and timeouts live in one place. One process-wide `SSL_CTX` is created
  lazily on the first https fetch from OpenSSL's default verify paths; `SSL_VERIFY_PEER` is always
  on, SNI is sent for DNS hosts (not for IPv4 literals), chain + hostname verification cover DNS
  names and IPv4 literals, and there is no insecure mode. TCP connect uses `connect_timeout_ms`;
  the TLS handshake and all TLS I/O use `read_timeout_ms`; a clean and an unclean EOF are both end
  of stream (truncation is still caught by Content-Length/chunked); the TLS path ignores `SIGPIPE`
  once process-wide so a dead peer cannot terminate the browser.
- Redirects: `Location` is resolved with `net::ResolveUrl` (absolute, relative, root-relative,
  protocol-relative, scheme-relative); http<->https targets are allowed, `file` targets are
  rejected, and `max_redirects` is unchanged.
- `shell`: no API or CLI change — page fetches, link navigation (https links now navigate),
  redirects and image loading all reuse the existing `net` calls; the page loader, `--dump-*` modes,
  `--screenshot`, window mode and navigation accept `https://` automatically.
- Tests: committed test-only TLS fixtures (`tests/fixtures/tls/`: CA, `localhost` leaf, unrelated
  untrusted leaf; README documents regeneration) and a header-only in-process TLS loopback server
  (`tests/support/tls_server.h`). `net.tls` covers a successful GET, untrusted certificate,
  hostname mismatch, handshake timeout, https->https / http->https / https->http redirects and the
  body cap; `net.http` gains relative and protocol-relative redirect coverage and drops the obsolete
  "https redirect is an error" case; `net.url`/`net.url_resolution` cover https parsing,
  serialization and resolution; `shell.https` serves the committed HTML+PNG page
  (`tests/fixtures/https_site/`) over TLS and asserts URL/title/content height, text and image
  pixels, the request sequence and the untrusted-certificate failure. The M7 nav fixture's
  "Unsupported page" link is now deliberately `ftp://` so no test can reach the public network.
- Acceptance reached: all 55 pre-M9 tests plus the new URL/TLS/https tests pass under GCC and Clang
  with warnings as errors (57 tests total); a local HTTPS page with an image renders headlessly; the
  certificate, hostname and timeout failures are explicit errors; every pre-M9 `http`/`file` dump
  and screenshot is byte-identical to the M8b baseline and GCC/Clang PPMs are identical; manual
  public-site checks (raw body, DOM dump, screenshot) work. Real-site checking is manual and
  network-dependent; restricted networks still need the M16 proxy.
- Known M9 limits: HTTP/1.1 only (no ALPN/HTTP/2), no compression (no `Accept-Encoding` is sent), no
  cookies/sessions, HSTS, mixed-content blocking, client certificates, TLS session reuse or HTTP
  caching; no proxy (M16); IPv6 literal https URLs are still unsupported by the URL parser; the
  trust policy is exactly OpenSSL's default store plus the standard env overrides (no pinning, no
  insecure mode); the TLS context is created lazily once per process; real-world rendering stays
  limited by the existing HTML/CSS subset; public-network details are never committed and real-site
  checks stay manual.

## M10 — Automatic font selection ✅ (done)

- Goal met: with no font flags, text is rendered with fonts the browser picks itself, like a normal
  browser. The computed CSS `font-family` list, `font-weight` and `font-style` are matched through
  the approved fontconfig library; the initial family is `serif`; a text box is split into runs by
  character coverage so scripts missing from the preferred face (Han text on a Latin page, or a
  Latin word on a Han-heavy page) fall back to an installed font whose character set covers them;
  the page language (`<html lang>`) is passed to fontconfig as a language hint.
- `style`: `font-family` is a parsed, inherited computed property (comma lists, quoted names,
  generic families normalized); the UA sheet adds `b/strong` bold, `i/em` italic and
  `code/kbd/samp/pre` monospace; `--dump-style` gains a `font-family=` field.
- Layout and paint share one per-session `Fonts` object, so the font chosen for a text box is the
  same for measurement and painting: `TextMetrics` has `ascent_px`; a measure callback receives
  the text's computed style; text boxes carry `baseline_offset_px` (the line's ascent, so mixed
  fonts on one line share a baseline); paint takes a `FontRunsProvider` returning per-run real
  fonts, or nothing for the built-in bitmap path.
- Chinese/readable-text support: layout now segments text into break units: ASCII/U+3000
  whitespace separates words as before, non-CJK text stays word-grouped, CJK characters become
  individual break opportunities (Han, Kana, Hangul, Bopomofo, CJK punctuation, fullwidth forms),
  with simplified prohibitions (closing punctuation never starts a line, opening punctuation never
  ends one). Chinese paragraphs without spaces therefore wrap and are readable. Invalid UTF-8 is
  lenient and never crashes.
- `--font PATH` (one exact face, no fallback) and `--font-family NAME` (preferred family that now
  also follows the computed weight/style, with character fallback) remain explicit overrides. With
  no usable system font, or a fontconfig failure, the browser degrades to the built-in bitmap
  font; a page load never fails because fonts are missing.
- Tests stay deterministic: committed self-authored fixture fonts (`tests/fixtures/fonts/`: the
  M8b regular face plus new bold and italic faces and a tiny Han face, all documented) and
  generated test fontconfig configs. The empty fontconfig config reproduces every pre-M10 bitmap
  golden byte for byte; the fixture config exercises automatic selection (default serif, generic
  families, weight/style, unknown-family fallback) and the Han fallback, `shell.cjk_*` serve a
  committed Chinese page and assert its per-unit boxes, wrapping and painted ink, and
  `layout.cjk_line_break` covers the tokenizer and punctuation rules.
- Acceptance reached: all pre-M10 tests plus the new style/layout/paint/shell tests pass under GCC
  and Clang with warnings as errors (66 tests); a page with no font flags renders English with
  system serif/bold/italic fonts and Chinese with a Han fallback face plus wrapping; the bitmap
  path (empty fontconfig) is byte-identical to the pre-M10 baseline and GCC/Clang PPMs are
  identical on both paths; manual checks render Latin and Chinese pages with system fonts.
- Known limits: no `@font-face`/webfont downloads (only installed fonts); no cross-text-node
  punctuation gluing (`中<b>文</b>。` may start a line with `。`); one text box per CJK break unit
  (fine for the MVP, scales with the 16 MiB body cap); simplified UAX#14 subset (no full line
  breaking, no dictionary breaking for Thai/Khmer/Lao, no hyphenation); no bidi/RTL layout
  (Arabic/Hebrew render in logical order, not visually reordered) and no complex-script shaping
  beyond harfbuzz's default per-run shaping; other scripts are best-effort through generic
  fontconfig character coverage (English and Chinese are the explicitly supported languages);
  no synthetic bold/italic (a missing bold/italic face falls back to regular); TTC face 0 only;
  no `font-stretch`/`font-variant`/`font-feature-settings`; language comes only from the root
  `<html lang>`; output with system fonts is machine-dependent by design (tests pin
  `FONTCONFIG_FILE`).

## M11 — Performance ✅ (done)

- Goal: make large pages responsive without changing any rendered pixel. Measured start point
  (80x600, automatic system fonts, stress pages: a 4000-paragraph page with 81k text boxes and a
  1200-paragraph Chinese page with 48k Han boxes): paint 1.3 s (10k words) to 10.6 s (81k boxes)
  per frame, layout 0.27–2.1 s, scrolling/resizing re-doing all of it every frame; only 0.4% of the
  text boxes intersect the viewport at any scroll position. Bitmap pages were 3–7x faster than
  needed because the default build was unoptimised; the window loop also relaid out the page on
  every Expose and once per resize event without coalescing X11 event bursts.
- P0 build: the default build type is now optimised (RelWithDebInfo) with `-ffp-contract=off` for
  deterministic floating point, so layout geometry stays byte-identical across build types and
  compilers. This alone cut bitmap-path page load/截图 times by 3–7x.
- P1 text caches (render + shell): reuse one harfbuzz font/face per font/size instead of creating
  one per shaped run (the fresh hb_face defeated harfbuzz's shape-plan cache), reuse buffers, add
  bounded LRU caches for shaped runs and glyph coverage bitmaps, and in the shell add a cheap
  resolved-style fast path plus bounded LRU caches for text metrics and font runs. All caches are
  private, single-threaded and eviction never changes output.
- P2 frame/窗口: viewport culling in `paint` (conservative text margin so pixels stay identical;
  backgrounds/images cull by intersection), and window-loop fixes: Expose no longer triggers a
  relayout, resize relayouts only when the size actually changed, consecutive Expose/ConfigureNotify
  events are coalesced, and the paint surface is reused instead of allocated per frame.
- Acceptance reached: all 66 pre-M11 tests and the byte-compat/cross-compiler gates stay green;
  fixture-font and bitmap frames are byte-identical to the pre-M11 baseline at 33 scrolled reference
  frames; the window opens with a single layout. Measured on the 81k-text-box stress page (800x600,
  automatic system fonts, the development machine): frame paint **10.6 s -> 3.5 ms**, scroll **11.0 s ->
  3.1 ms**, resize frame **11.0 s -> 3.0 ms**, layout **2.1 s -> 31 ms**, page load **2.2 s ->
  69 ms**; on a 10k-word page paint **1.38 s -> 0.89 ms**, layout **267 ms -> 3.3 ms**, load
  **290 ms -> 30 ms**; the Chinese stress page paint **3.2 s -> 2.3 ms**, load 1.3 s -> 142 ms
  (first-use CJK font loading dominates the remaining load time). The bitmap path also improved
  (culling removes offscreen commands) with no layout regression. Real-window smoke on X11 passed:
  no stale frame after resize, 40-event configure burst coalesced into 2 relayouts, single layout
  on first expose, clean close.
- Known limits after this round: the first frame still pays cold cache costs; culling still walks
  the whole box tree per frame (cheap, but no spatial index yet); resize is still a full reflow
  (fast measurement caches, but very large pages may not be perfectly smooth while dragging; no
  incremental layout); rendering stays single-threaded (no background layout/paint, no parallel
  resource loading); image loading and real-site network/HTTP performance (keep-alive, gzip,
  parallel fetch, image cache/lazy loading) stay out of this round; the window still presents the
  whole surface per frame (XShm/dirty-rect later, needs a new dependency approval); GPU rendering is
  deliberately not used (measurements show fill rate is not the bottleneck; the costs are
  algorithmic CPU work).

## M12 — External stylesheets ✅ (done)

- Goal met: pages that reference their CSS with `<link rel="stylesheet" href="...">` render with
  those rules applied, in the same cascade as inline `<style>` and `style` attributes. This was the
  single largest functional gap between the engine and real pages: most real sites ship their
  styling as external files, and a manual check against `https://www.python.org/` now applies the
  site's typography and dark color scheme instead of rendering unstyled text.
- `style`: public `LinkedStyleSheet { dom::NodeId link_node; std::string text; }` plus a two-arg
  `ComputeStyles(document, linked_sheets)`. One pre-order walk merges the document's `<style>`
  elements and each caller-supplied external sheet at its `<link>` element's position, so
  document-order ties resolve exactly like browser cascade order; specificity, `!important`,
  inheritance and inline `style` declarations are unchanged. The one-arg overload delegates with an
  empty list and keeps pre-M12 output and per-page cost; `style` stays free of I/O and network code.
- `shell`: a new private `LoadStyledDocument()` shares one fetch → parse → discover/filter/fetch
  sheets → style path between normal page loading (window, `--screenshot`, `--dump-boxes`) and
  `--dump-style`; `--dump-dom`/`--dump-body` are unchanged and fetch no sheets. One iterative
  pre-order DOM walk discovers `<link>` elements in head or body; the exact MVP filter is: `rel`
  token list containing `stylesheet` without `alternate`, no `disabled` attribute, `media` empty/
  `all`/`screen`, `type` empty/`text/css`, and a non-empty `href` resolved with `net::ResolveUrl`
  against the page's final URL after redirects. Every applicable link is fetched once per
  occurrence (no deduplication/cache), `net::Fetch` defaults apply (16 MiB body cap, connect/read
  timeouts), only 200..299 bodies are parsed, and every filtered/unresolvable/failed/non-2xx sheet
  is skipped silently (no stderr, page load still succeeds), exactly like missing images since M8a.
- Fixtures/tests: committed `tests/fixtures/css_site/` (relative subdirectory sheet, document-order
  interleaving, filter, failed/disabled/alternate/media/type/ftp/empty-href links, duplicate links,
  head and body links, no-sheet and filtered-only pages) with documented expected computed values,
  geometry and colors; `style.external_sheets`, `shell.external_css` (including in-process loopback
  HTTP redirect, absolute URL, relative resolution against the final URL, failure and no-dedup
  request-count cases via the new header-only `tests/support/http_server.h`) and the CLI
  `--dump-style` checks; 70 tests total.
- Acceptance reached: all 66 pre-M12 tests plus the 4 new tests pass under GCC and Clang with
  warnings as errors (70 tests); pages without `<link>` are byte-identical to pre-M12 in dumps,
  exit status and PPM pixels (20-case byte-compat gate); GCC and Clang output is byte-identical; a
  manual real-site check (`https://www.python.org/`) shows external typography and colors applied;
  no new dependency.
- Known limits after this round: no `@import`, media queries (including comma lists such as
  `media="not print, ..."`, which real sites use and this MVP skips), alternate/disabled sheet
  selection, parameterised `type` values, `url(...)` resources, CSS `@charset` or cross-origin
  rules; sheets are still parsed by the M4 CSS subset, so many real rules are ignored (M14/M15);
  all sheet loads are serial with the default timeouts (M13 keep-alive reduces connection overhead);
  each occurrence is fetched separately and there is no stylesheet cache yet.

## M13 — Resource loading pipeline ✅ (done)

- Goal met: pages with many assets now load in time proportional to bytes instead of paying a fresh
  connection (and TLS handshake) per resource. The measured start point was ~20 s wall with ~0.1 s
  CPU on image-heavy real pages because every `<img>` — and since M12 every stylesheet — was fetched
  serially on a new connection, uncompressed and without any cache.
- `net`: new public `net::Client` (pimpl, single-threaded) that keeps at most one idle HTTP/1.1
  connection per origin (scheme, host, port) and reuses it across requests. It frames responses
  incrementally: a bounded header read, then exactly Content-Length bytes or the full chunked body
  (including trailers), with read-ahead bytes carried on the pooled connection for the next
  request. Persistence follows HTTP/1.1 rules (no `Connection: close`) or HTTP/1.0
  (`Connection: keep-alive`), requires a length delimiter (a persistent response without one is a
  readable error), and closes on `Connection: close`. It advertises `Accept-Encoding: gzip` and
  transparently inflates gzip bodies with the already-approved zlib (`identity` passes through,
  other codings are errors; both the raw and the decoded body stay bounded; headers are
  preserved). One retry on a fresh connection handles a stale pooled connection that fails before
  any response byte arrives — timeouts, fresh-connection failures and anything after received
  response bytes are never retried. Redirects use the same pool (same-origin hops reuse,
  cross-origin hops keep their own idle connection) and `file://` works like the one-shot fetch.
  The public one-shot `net::Fetch()` keeps its exact observable behavior (same request bytes and
  `Connection: close`, no compression negotiation, same body/timeout/redirect/error semantics) and
  remains the convenience API.
- `shell`: `LoadPage()` keeps its signature and creates one `net::Client` for the whole load; the
  page, all external stylesheets and all images are fetched through it, so a page and its resources
  reuse one connection per origin. A per-load image cache keyed by the serialized resolved image URL
  fetches and decodes each unique URL at most once: duplicate `<img>` occurrences share one decoded
  `render::Image` and intrinsic size, and a failed fetch/decode is remembered for the rest of the
  load so duplicate broken images are not retried. Stylesheet semantics stay the M12 ones (every
  applicable `<link>` occurrence is fetched once; no stylesheet dedup/cache), `RelayoutPage` never
  refetches, and there is no cross-load or session cache.
- Tests (all offline, deterministic, no wall-time assertions): `net.keep_alive` covers single-
  connection reuse with Content-Length and chunked bodies, `Connection: close`, two origins, stale-
  connection retry, fresh-connection failure, `CloseIdleConnections`/destructor, `file://`,
  same-origin redirects, HTTP/1.0 persistence rules, the delimiter error and the raw body cap;
  `net.gzip` covers gzip with Content-Length and chunked transfer encoding, identity, corrupt gzip,
  an unsupported coding, the decoded-size cap, and proves the one-shot `Fetch` still sends no
  `Accept-Encoding` and does not decode; `net.tls_keep_alive` reuses one TLS connection for two
  requests; `shell.resource_loading` serves a page, a stylesheet and duplicate images from a
  loopback server and asserts the exact connection/request counts (one connection, one fetch per
  unique image URL), the shared decoded image object and pixels, one fetch for a duplicate missing
  image, no over-broad sharing of distinct URLs, and recovery after a force-closed idle connection.
  The shared loopback HTTP/TLS test servers gained persistent-connection modes and
  connection/request counters; 74 tests total.
- Acceptance reached: all 70 pre-M13 tests plus the 4 new tests pass under GCC and Clang with
  warnings as errors (74 tests); every pre-M13 dump, exit status and PPM is byte-identical to the
  M12 baseline (23-case byte-compat gate, including the M12 `css_site` pages); GCC and Clang outputs
  are identical; measured on the same machine/network, `https://www.python.org/` at 800x600 dropped
  from 2.18/2.07/2.02 s (three M12 runs) to 0.99/1.28/0.99 s (three M13 runs) and
  `https://www.w3.org/` from 3.88 s to 1.79 s, with byte-identical screenshots before/after; no new
  dependency (zlib was already approved and is now also linked directly by `net`).
- Known limits after this round: HTTP/1.1 only (no HTTP/2/ALPN), no HTTP caching headers, cookies,
  HSTS or proxy (M16); no parallel/threaded prefetch — resources are still serial, only connection
  setup and transfer are amortized (limited parallelism is a later decision from measurements);
  stylesheet URLs are not deduplicated (each applicable link occurrence is still fetched; keep-alive
  only removes the per-request connection cost); image deduplication is per page load, not across
  loads or a browser session; a persistent response without a length delimiter is an error (statuses
  such as 204/304-without-length are not specially handled); gzip is the only supported content
  coding; idle pooled connections are not proactively health-checked (a stale one is detected and
  retried once, at most one retry) and have no idle timeout (they live until the Client is
  destroyed); `Connection: keep-alive` is sent explicitly.

## M14 — CSS compatibility batch 1 (planned; next milestone)

- Goal: apply the high-frequency CSS that real pages rely on once M12 makes their stylesheets
  available. Representative real stylesheets currently exercise (and we ignore): `line-height`,
  `text-align`, `text-decoration`, `border` and `border-*` colors, `background` shorthand (the
  color part), `max-width`/`min-width`, `margin: auto`, `box-sizing`, logical properties
  (`margin-block-*`, `inline-size`, ...), `overflow: hidden` clipping, `rgb()`/`hsl()`, `%`/`rem`
  units, and `display: none`/`visibility`.
- Expected shape: extend the property parser, computed style and — where required — layout/paint in
  small, separately tested slices; keep the cascade/specificity/inheritance/`!important` invariants;
  pages that do not use the new properties keep byte-identical output.
- Acceptance: fixture pages for each property, dump/screenshot tests, no regressions, and a manual
  real-site visual check. If the batch grows too large it is split (for example borders first,
  logical properties and units as a follow-up).
- Non-goals: flexbox/grid/position/float (M15), CSS variables, other at-rules, webfonts.

## M15 — Flexbox / position / float subset (planned; scope re-confirmed before starting)

- Goal: support the layout mechanisms modern static pages use most: a practical subset of flexbox
  (row/column direction, `justify-content`, `align-items`, `gap`, `flex-grow`/`shrink`/`basis`,
  wrapping) and/or basic `position`/`float`. The exact split is decided from the M14 measurements;
  flexbox alone is large enough to be its own vertical slice.
- Acceptance: fixture pages for the supported subset plus real-site visual checks; deterministic box
  dumps and screenshots; no regressions.
- Non-goals until re-scoped: grid, tables, forms, animations/transforms, full spec compliance.

## M16 — HTTP proxy (planned; deferred by the user after M9)

- Fetch `http://` through an HTTP proxy with absolute-form request targets, and `https://` through
  a proxy `CONNECT` tunnel followed by the TLS handshake.
- CLI `--proxy host:port` plus the standard `http_proxy`/`https_proxy` environment fallback;
  in-process fake-proxy tests. This is what makes real-site testing possible on networks where
  direct outbound traffic is restricted.
- Scope and contracts are frozen at the start of that round; no new dependency is expected.

## Deferred — JavaScript subset

- The user shelved the JavaScript subset on 2026-10-31; it is not the next milestone. It remains a
  large optional milestone (lexer, parser, small interpreter, DOM bindings and event handlers);
  do not start it without an explicit user priority decision.

## M17+ — Opportunistic

More CSS (additional flex/grid, tables), forms, more image formats, better text handling,
conformance suites and further performance. Each item starts only after the previous slice is clean
and the maintainer agrees on priority.

## Backlog — planned follow-ups

- **More image formats and background images** — JPEG/GIF/WebP, `background-image`,
  `srcset`, an image cache and lazy loading; scheduled after M8a on demand, blocking no milestone.
- **Web fonts and richer text** — `@font-face`/font downloads, synthetic bold/italic, variable
  font instances, bidi/RTL and full Unicode line breaking (UAX#14), dictionary breaking for Thai/
  Khmer/Lao and complex-script refinements; M10 deliberately covers English and Chinese first,
  other scripts are best-effort through fontconfig character coverage.
- **Test helper consolidation** — the per-file `Check`/`g_failures` test harness is still
  duplicated in the older test executables. The header-only `tests/support/check.h` (interface
  target `toy_test_support`) was introduced in M5 and is used by the new tests; migrate the older
  files only when they are touched. No framework dependency.

## Rules

- Do not start a milestone before the previous milestone's demo works.
- Never block a milestone on a pending dependency approval: ship the headless or
  own-implementation slice, then upgrade when approval arrives.
- Any milestone can be re-scoped by the user. MVP beats completeness, always.
- Headless-first: any visible or observable result must also be inspectable without a window (a
  dump command or a screenshot), so the project can be developed and tested entirely headless.
