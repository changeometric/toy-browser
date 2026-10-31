#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "toy/net/url.h"
#include "toy/render/surface.h"

namespace toy::shell {

// The loaded page data owned by a Browser: final URL, DOM, styles, box tree and
// the decoded page images. Defined by the shell-private page loader (page.h);
// the session only holds it, so the public header stays free of private types.
struct Page;

// One browser session: the current page (fetched, parsed, styled and laid out),
// the navigation history, the scroll offset and painting, without a window or an
// event loop. The window event loop and --screenshot are thin presenters on top
// of it, so navigation, link hit-testing and scrolling are exercised headlessly.
//
// Units: viewport sizes, click coordinates and scroll offsets are integer device
// pixels; 1 CSS px = 1 device px (the MVP assumption). Loading is synchronous.
//
// History: one entry per successfully loaded URL (the initial page and every
// click/Back/Forward that loads). Loading a new page after going back truncates
// the forward entries. A navigation that fails leaves the current page, the
// scroll offset and the history unchanged. The scroll offset resets to 0 on every
// successful load (restoring a page's previous scroll position is a known MVP
// limit).
class Browser {
 public:
  // Creates an empty session with the given viewport size. Both must be positive
  // (asserted in debug builds).
  Browser(int viewport_width, int viewport_height);
  // Out of line: the page data type is complete only inside the shell module.
  ~Browser();

  // Loads the absolute http:// or file:// URL `url` as the current page and
  // appends a history entry on success; the scroll offset resets to 0. On failure
  // returns false, sets *error (when not null) to a ready-to-print message and
  // leaves the current page, scroll offset and history unchanged.
  bool Open(std::string_view url, std::string* error);

  // Resolves `href` against the current page's final URL with net::ResolveUrl and
  // opens it (same success/failure rules as Open). A resolved URL whose scheme,
  // host, port, path and query equal the current page's (a reference that only
  // changes the fragment) is a no-op: false with an empty *error and no history
  // change. Returns false with *error set when there is no current page or the
  // reference cannot be resolved or loaded.
  bool Navigate(std::string_view href, std::string* error);

  // History navigation. Return false with an empty *error when there is no entry
  // in that direction; on a load failure return false with *error set and leave
  // the current index, page and scroll unchanged.
  bool Back(std::string* error);
  bool Forward(std::string* error);
  bool CanGoBack() const;
  bool CanGoForward() const;

  // Clicks the window point (x, y) in device pixels, top-left origin. The point is
  // converted to page space with the current scroll offset and hit-tested against
  // the box tree; when it falls inside a link (the nearest element ancestor with
  // an href attribute) that URL is resolved and opened. Returns true when the
  // click successfully navigated to another page. A click that does not hit a link
  // returns false with an empty *error; a link whose URL cannot be resolved or
  // loaded returns false with *error set and keeps the current page.
  bool Click(int x, int y, std::string* error);

  // Scrolls by delta_device_px (positive scrolls towards the end of the document)
  // clamped to [0, max(0, content_height - viewport_height)]. Return true when the
  // offset changed; no effect without a page.
  bool ScrollBy(int delta_device_px);
  bool ScrollToTop();
  bool ScrollToBottom();
  // Changes the viewport size, re-lays-out the current page (if any) and clamps
  // the scroll offset. Both sizes must be positive (asserted).
  void SetViewport(int width, int height);

  bool has_page() const;
  // Final URL of the current page (after redirects); a default net::Url when
  // there is no page.
  const net::Url& current_url() const;
  // The current page's title: the text content of the first <title> element with
  // ASCII whitespace collapsed and trimmed; when that is empty, the serialized
  // final URL; an empty string when there is no page.
  std::string title() const;
  // Vertical scroll offset in device pixels (>= 0), the scrollable content height
  // in device pixels (0 without a page) and the current viewport size.
  int scroll_y() const;
  int content_height() const;
  int viewport_width() const;
  int viewport_height() const;

  // Fills `surface` white and paints the current page at the current scroll
  // offset. Without a page it only fills white. The page is laid out at the
  // session's viewport width, which may differ from the surface size.
  void Paint(render::Surface& surface) const;

 private:
  // Fetches, parses, styles, loads the images and lays out `url` and, on
  // success, makes it the current page at scroll 0. Does not touch the history.
  bool Load(const net::Url& url, std::string* error);
  void Relayout();
  void ClampScroll();
  // Extracts the title from the current document (see title()).
  std::string ExtractedTitle() const;

  int viewport_width_ = 0;
  int viewport_height_ = 0;
  int scroll_y_ = 0;
  net::Url current_url_;
  // Current page, or null when nothing has loaded successfully yet.
  std::unique_ptr<Page> page_;
  std::vector<net::Url> history_;
  std::size_t history_index_ = 0;
};

}  // namespace toy::shell
