// Headless end-to-end test for the M7 browser session: navigation, link click
// hit-testing, history, scrolling and titles. Everything runs through
// toy::shell::Browser against the committed file:// fixture site; no display,
// no network and no font are needed.

#include <cstddef>
#include <cstdio>
#include <string>

#include "support/check.h"
#include "toy/render/surface.h"
#include "toy/shell/browser.h"

namespace {

constexpr int kViewportWidth = 400;
constexpr int kViewportHeight = 200;

// Frozen link geometry of tests/fixtures/nav_site/index.html at --size 400x200,
// taken from a `toybrowser --dump-boxes` run. Clicks use window coordinates that
// land inside the link's text box (rectangles are half-open: x in [x, x+width)
// and y in [y, y+height)). Relevant dump lines:
//
//   <p> block x=8 y=88  ... first paragraph text "This" x=8 y=88 width=32 height=16
//   <p> block x=8 y=152 ... <a> inline ... text "About" x=8 y=152 width=40 height=16
//   <p> block x=8 y=200 ... <a> inline ... text "Docs" x=8 y=200 width=32 height=16
//   <p> block x=8 y=248 ... <a> inline ... text "Missing" x=8 y=248 width=56 height=16
//   <p> block x=8 y=296 ... <a> inline ... text "Secure" x=8 y=296 width=48 height=16
constexpr int kAboutLinkX = 10;
constexpr int kAboutLinkY = 158;
constexpr int kDocsLinkX = 10;
constexpr int kDocsLinkY = 206;
constexpr int kMissingLinkX = 10;
constexpr int kMissingLinkY = 254;
constexpr int kSecureLinkX = 10;
constexpr int kSecureLinkY = 302;
// A word in the first paragraph, not inside a link.
constexpr int kBodyTextX = 10;
constexpr int kBodyTextY = 94;

std::string FileUrl(const std::string& directory, const std::string& name) {
  return "file://" + directory + "/" + name;
}

bool EndsWith(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool PathIs(const toy::net::Url& url, const char* name) {
  return EndsWith(url.path, std::string("/") + name);
}

// A small non-white pixel count proves Browser::Paint runs the layout/paint
// pipeline, not just the session bookkeeping.
int NonWhitePixels(const toy::render::Surface& surface) {
  int count = 0;
  for (int y = 0; y < surface.height(); ++y) {
    for (int x = 0; x < surface.width(); ++x) {
      if (surface.At(x, y) != 0xFFFFFFFFu) {
        ++count;
      }
    }
  }
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <nav_site-directory>\n", argv[0]);
    return 1;
  }
  const std::string site = argv[1];
  const std::string index_url = FileUrl(site, "index.html");
  const std::string about_url = FileUrl(site, "about.html");

  // --- Opening, clicking, history -------------------------------------------
  {
    toy::shell::Browser browser(kViewportWidth, kViewportHeight);
    std::string error;
    toy_test::Check(browser.Open(index_url, &error), "Open(index.html) succeeds");
    toy_test::Check(error.empty(), "a successful Open clears the error");
    toy_test::Check(browser.has_page(), "the index page is current");
    toy_test::Check(browser.title() == "Home", "the title comes from <title>");
    toy_test::Check(PathIs(browser.current_url(), "index.html"),
                    "current_url() is the loaded index page");
    toy_test::Check(browser.scroll_y() == 0, "a fresh page starts at scroll 0");
    toy_test::Check(browser.content_height() > kViewportHeight,
                    "the fixture page is taller than the viewport");
    toy_test::Check(
        browser.viewport_width() == kViewportWidth && browser.viewport_height() == kViewportHeight,
        "the session reports its viewport size");
    toy_test::Check(!browser.CanGoBack() && !browser.CanGoForward(),
                    "the first page has no history around it");

    // The painting path works headless and paints the page content.
    toy::render::Surface surface(kViewportWidth, kViewportHeight);
    browser.Paint(surface);
    toy_test::Check(NonWhitePixels(surface) > 0, "Paint() draws the page content");

    toy_test::Check(browser.Click(kAboutLinkX, kAboutLinkY, &error),
                    "clicking the About link text navigates");
    toy_test::Check(error.empty(), "a successful click leaves no error");
    toy_test::Check(PathIs(browser.current_url(), "about.html"),
                    "the clicked link loaded about.html");
    toy_test::Check(browser.title() == "About", "the title follows the clicked page");
    toy_test::Check(browser.CanGoBack() && !browser.CanGoForward(),
                    "the click added one history entry");
    toy_test::Check(browser.scroll_y() == 0, "a navigation resets the scroll offset");

    toy_test::Check(browser.Back(&error), "Back returns to the previous page");
    toy_test::Check(error.empty(), "a successful Back leaves no error");
    toy_test::Check(PathIs(browser.current_url(), "index.html"),
                    "Back reloads the previous history entry");
    toy_test::Check(browser.title() == "Home", "Back restores the previous title");
    toy_test::Check(!browser.CanGoBack() && browser.CanGoForward(), "Back moves the history index");
    toy_test::Check(!browser.Back(&error), "Back at the first entry is a no-op");
    toy_test::Check(error.empty(), "a no-op Back leaves no error");

    toy_test::Check(browser.Forward(&error), "Forward returns to the next page");
    toy_test::Check(error.empty(), "a successful Forward leaves no error");
    toy_test::Check(PathIs(browser.current_url(), "about.html"),
                    "Forward reloads the next history entry");
    toy_test::Check(browser.title() == "About", "Forward restores the next title");
    toy_test::Check(!browser.Forward(&error), "Forward at the last entry is a no-op");
    toy_test::Check(error.empty(), "a no-op Forward leaves no error");

    // A new load after going back truncates the forward entries.
    toy_test::Check(browser.Back(&error), "Back to the index page again");
    toy_test::Check(browser.CanGoForward(), "the About entry is still forward");
    toy_test::Check(browser.Navigate("docs.html", &error),
                    "Navigate resolves a relative href against the current page");
    toy_test::Check(PathIs(browser.current_url(), "docs.html"),
                    "the relative href opened docs.html");
    toy_test::Check(browser.title() == "Docs", "the navigated page sets its title");
    toy_test::Check(!browser.CanGoForward(), "a new load truncates the forward entries");
  }

  // --- Resize, scrolling, and clicks in a scrolled page ----------------------
  {
    toy::shell::Browser browser(kViewportWidth, kViewportHeight);
    std::string error;
    toy_test::Check(browser.Open(index_url, &error), "Open(index.html) for scrolling");

    browser.SetViewport(320, 240);
    toy_test::Check(browser.viewport_width() == 320 && browser.viewport_height() == 240,
                    "SetViewport updates the reported viewport size");
    toy_test::Check(browser.scroll_y() == 0, "the scroll offset is still clamped to 0");
    browser.SetViewport(kViewportWidth, kViewportHeight);
    toy_test::Check(
        browser.viewport_width() == kViewportWidth && browser.viewport_height() == kViewportHeight,
        "SetViewport restores the frozen viewport");

    const int max_scroll = browser.content_height() - browser.viewport_height();
    toy_test::Check(max_scroll > 0, "the fixture has a positive scroll range");
    toy_test::Check(browser.ScrollBy(100000), "a huge downward scroll changes the offset");
    toy_test::Check(browser.scroll_y() == max_scroll,
                    "a huge downward scroll clamps to content_height - viewport_height");
    toy_test::Check(!browser.ScrollBy(1), "scrolling further down at the bottom is a no-op");

    toy_test::Check(browser.ScrollBy(-100000), "a huge upward scroll changes the offset");
    toy_test::Check(browser.scroll_y() == 0, "a huge upward scroll clamps to 0");
    toy_test::Check(!browser.ScrollBy(-1), "scrolling further up at the top is a no-op");

    toy_test::Check(browser.ScrollToBottom(), "ScrollToBottom changes the offset");
    toy_test::Check(browser.scroll_y() == max_scroll, "ScrollToBottom reaches the maximum");
    toy_test::Check(!browser.ScrollToBottom(), "ScrollToBottom at the bottom is a no-op");
    toy_test::Check(browser.ScrollToTop(), "ScrollToTop changes the offset");
    toy_test::Check(browser.scroll_y() == 0, "ScrollToTop reaches 0");
    toy_test::Check(!browser.ScrollToTop(), "ScrollToTop at the top is a no-op");

    // Clicks use window coordinates: page y = click y + scroll_y.
    toy_test::Check(browser.ScrollBy(100), "scroll 100 px before the scrolled click");
    toy_test::Check(browser.scroll_y() == 100, "the scroll offset is 100 px");
    toy_test::Check(browser.Click(kDocsLinkX, kDocsLinkY - 100, &error),
                    "a click while scrolled converts to page coordinates");
    toy_test::Check(error.empty(), "the scrolled click leaves no error");
    toy_test::Check(PathIs(browser.current_url(), "docs.html"),
                    "the scrolled click followed the Docs link");
    toy_test::Check(browser.title() == "Docs", "the scrolled navigation updates the title");
    toy_test::Check(browser.scroll_y() == 0, "a successful navigation resets scroll to 0");
  }

  // --- Failure and no-op behavior -------------------------------------------
  {
    toy::shell::Browser browser(kViewportWidth, kViewportHeight);
    std::string error;
    toy_test::Check(browser.Open(index_url, &error), "Open(index.html) for the failure cases");

    // A click on body text is not a link: false, no error, no navigation.
    toy_test::Check(!browser.Click(kBodyTextX, kBodyTextY, &error),
                    "clicking non-link text does not navigate");
    toy_test::Check(error.empty(), "a non-link click sets no error");
    toy_test::Check(PathIs(browser.current_url(), "index.html"),
                    "a non-link click keeps the current page");

    // A pushdown of a fetch failure keeps the page and the history unchanged.
    const std::string title_before = browser.title();
    const bool could_go_back = browser.CanGoBack();
    const bool could_go_forward = browser.CanGoForward();
    toy_test::Check(!browser.Click(kMissingLinkX, kMissingLinkY, &error),
                    "clicking a link to a missing page fails");
    toy_test::Check(!error.empty(), "a missing-page click reports an error");
    toy_test::Check(PathIs(browser.current_url(), "index.html") && browser.title() == title_before,
                    "a failed click keeps the current page");
    toy_test::Check(
        browser.CanGoBack() == could_go_back && browser.CanGoForward() == could_go_forward,
        "a failed click keeps the history");

    // An https link resolves to an unsupported scheme: error, page unchanged.
    toy_test::Check(!browser.Click(kSecureLinkX, kSecureLinkY, &error),
                    "clicking the https link fails");
    toy_test::Check(!error.empty(), "the https link reports an error");
    toy_test::Check(PathIs(browser.current_url(), "index.html") && browser.title() == title_before,
                    "the failed https click keeps the current page");

    // An unresolvable URL keeps the page too.
    toy_test::Check(!browser.Open("not a url", &error), "Open() rejects an invalid URL");
    toy_test::Check(!error.empty(), "an invalid URL reports an error");
    toy_test::Check(PathIs(browser.current_url(), "index.html") && browser.title() == title_before,
                    "a failed Open keeps the current page");
    toy_test::Check(
        browser.CanGoBack() == could_go_back && browser.CanGoForward() == could_go_forward,
        "a failed Open keeps the history");

    // Fragment-only and empty references are same-page no-ops: false, no error,
    // and no history change.
    toy_test::Check(!browser.Navigate("", &error), "an empty reference is a no-op");
    toy_test::Check(error.empty(), "an empty reference sets no error");
    toy_test::Check(!browser.Navigate("#section", &error), "a fragment-only reference is a no-op");
    toy_test::Check(error.empty(), "a fragment-only reference sets no error");
    toy_test::Check(PathIs(browser.current_url(), "index.html") && browser.title() == title_before,
                    "same-page references keep the current page");
    toy_test::Check(
        browser.CanGoBack() == could_go_back && browser.CanGoForward() == could_go_forward,
        "same-page references keep the history");

    // A successful load after scrolling resets the offset (Open and Navigate).
    toy_test::Check(browser.ScrollBy(50), "scroll before navigating away");
    toy_test::Check(browser.Navigate("about.html", &error),
                    "Navigate follows a relative href from the index page");
    toy_test::Check(PathIs(browser.current_url(), "about.html"), "Navigate loaded about.html");
    toy_test::Check(browser.title() == "About", "Navigate updates the title");
    toy_test::Check(browser.scroll_y() == 0, "Navigate resets the scroll offset");

    toy::shell::Browser empty(kViewportWidth, kViewportHeight);
    toy_test::Check(!empty.has_page(), "a fresh session has no page");
    toy_test::Check(empty.title().empty(), "a session without a page has an empty title");
    toy_test::Check(!empty.ScrollBy(10), "scrolling without a page is a no-op");
    toy_test::Check(!empty.Click(1, 1, &error), "clicking without a page is a no-op");
    toy_test::Check(error.empty(), "clicking without a page sets no error");
  }

  return toy_test::Finish("shell navigation test");
}
