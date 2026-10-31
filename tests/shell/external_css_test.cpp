// M12 end-to-end external stylesheet test: fetch -> parse -> discover/filter/
// fetch linked sheets -> cascade -> layout -> paint through the shell-private
// styled-document path (src/shell/page.h) and toy::shell::Browser. It also
// exercises redirects and absolute/relative URL resolution against an in-process
// loopback HTTP server (tests/support/http_server.h), so no public network, no
// display and no installed font are needed (the empty FONTCONFIG_FILE test config
// keeps the deterministic built-in bitmap font path).
//
// The fixture pages and their exact computed values are documented in
// tests/fixtures/css_site/README.md. The frozen box geometry below is taken from
// a bitmap-font run of
//
//   toybrowser --size 320x200 --dump-boxes
//       "file://$PWD/tests/fixtures/css_site/index.html"
//
// Relevant dump lines (the external .external rule sets font-size/margins):
//
//   <html> block x=0 y=0 width=320 height=202
//     <body> block x=8 y=8 width=304 height=186
//       <p> block x=22 y=14 width=280 height=24
//         text "External" x=22 y=14 width=96 height=24
//         text "rules" x=130 y=14 width=60 height=24
//       <p> block x=8 y=66 width=304 height=16
//         text "Before" x=8 y=66 width=48 height=16
//       <p> block x=8 y=114 width=304 height=16
//         text "After" x=8 y=114 width=40 height=16
//       <p> block x=8 y=162 width=304 height=16
//         text "Inline" x=8 y=162 width=48 height=16
//
// The painted pixels of the same page at 320x200 are frozen too: the external
// #abcdef background fills the 280x24 .external box (6390 pixels after the 330
// #123456 text pixels), and the ordered colors of the three inline/linked
// <style>/link rules reach the canvas (102 pixels of #00ff00, 75 of #0000ff and
// 85 of #00aa00).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "page.h"
#include "support/check.h"
#include "support/http_server.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/net/url.h"
#include "toy/render/surface.h"
#include "toy/shell/browser.h"
#include "toy/shell/fonts.h"
#include "toy/style/computed_style.h"

namespace {

constexpr int kViewportWidth = 320;
constexpr int kViewportHeight = 200;

// Exact fixture colors (straight ARGB for painted pixels, 0xRRGGBB for the
// ComputedStyle::color field).
constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
// ComputedStyle::color stores 0xRRGGBB; painted pixels (and background colors)
// are straight 0xAARRGGBB.
constexpr std::uint32_t kExternalColorRgb = 0x123456u;
constexpr std::uint32_t kExternalColorArgb = 0xFF123456u;
constexpr std::uint32_t kExternalBackground = 0xFFABCDEFu;
constexpr std::uint32_t kBeforeGreen = 0xFF00FF00u;
constexpr std::uint32_t kAfterBlue = 0xFF0000FFu;
constexpr std::uint32_t kInlineGreen = 0xFF00AA00u;

std::string FileUrl(const std::string& directory, const std::string& name) {
  return "file://" + directory + "/" + name;
}

// First element with an `id` attribute equal to `id`, or kInvalidNode.
toy::dom::NodeId FindElementById(const toy::dom::Document& document, std::string_view id) {
  std::vector<toy::dom::NodeId> pending;
  pending.push_back(document.root());
  while (!pending.empty()) {
    const toy::dom::NodeId node_id = pending.back();
    pending.pop_back();
    const toy::dom::Node& node = document.Get(node_id);
    if (node.type == toy::dom::NodeType::kElement) {
      for (const toy::dom::Attribute& attribute : node.attributes) {
        if (attribute.name == "id" && attribute.value == id) {
          return node_id;
        }
      }
    }
    for (toy::dom::NodeId child = node.last_child; child != toy::dom::kInvalidNode;
         child = document.Get(child).prev_sibling) {
      pending.push_back(child);
    }
  }
  return toy::dom::kInvalidNode;
}

std::optional<toy::net::Url> ParseUrl(const std::string& url_text) {
  std::string error;
  std::optional<toy::net::Url> url = toy::net::ParseUrl(url_text, &error);
  if (!url) {
    std::fprintf(stderr, "  cannot parse URL '%s': %s\n", url_text.c_str(), error.c_str());
  }
  return url;
}

// Loads one URL through the shell-private loader used by the window,
// --screenshot and --dump-boxes paths.
bool LoadUrl(const std::string& url_text, const std::shared_ptr<const toy::shell::Fonts>& fonts,
             toy::shell::Page* page) {
  const std::optional<toy::net::Url> url = ParseUrl(url_text);
  if (!url) {
    return false;
  }
  std::string error;
  if (!toy::shell::LoadPage(*url, kViewportWidth, fonts, page, &error)) {
    std::fprintf(stderr, "  cannot load '%s': %s\n", url_text.c_str(), error.c_str());
    return false;
  }
  return true;
}

int RoundPx(float value) { return static_cast<int>(std::lround(value)); }

bool NearlyEqual(float actual, float expected) { return std::fabs(actual - expected) <= 0.01f; }

struct StyleExpectation {
  std::uint32_t color = 0x000000u;
  std::uint32_t background = 0x00000000u;
  float font_size_px = 16.0f;
  float margin_top = 0.0f;
  float margin_right = 0.0f;
  float margin_bottom = 0.0f;
  float margin_left = 0.0f;
};

// The computed style of a plain <p> with no author rule: the initial/inherited
// color and font-size plus the UA `p { margin: 16px 0 }`. The canvas body margin
// is not part of an element's own computed style.
StyleExpectation DefaultParagraphStyle(std::uint32_t color = 0x000000u) {
  return StyleExpectation{color, 0x00000000u, 16.0f, 16.0f, 0.0f, 16.0f, 0.0f};
}

// Asserts the external-sheet-visible computed values of one fixture element.
void CheckElementStyle(const toy::shell::Page& page, std::string_view id,
                       const StyleExpectation& expected, const char* description) {
  const std::string prefix = std::string(description) + " #" + std::string(id);
  const toy::dom::NodeId node = FindElementById(page.document, id);
  if (node == toy::dom::kInvalidNode) {
    std::fprintf(stderr, "  %s: no element with that id\n", prefix.c_str());
    toy_test::Check(false, "the fixture element exists");
    return;
  }
  const toy::style::ComputedStyle& style = page.styles.Get(node);
  const auto check = [&](bool condition, const char* field) {
    if (!condition) {
      std::fprintf(stderr, "  %s: unexpected %s\n", prefix.c_str(), field);
    }
    toy_test::Check(condition, (prefix + ": " + field).c_str());
  };
  check(style.color == expected.color, "color");
  check(style.background_color == expected.background, "background-color");
  check(NearlyEqual(style.font_size_px, expected.font_size_px), "font-size");
  // M14 stores margins as AutoLength; the fixture margins are plain pixels.
  const auto margin_matches = [](const toy::style::AutoLength& margin, float expected_value) {
    return !margin.is_auto && margin.length.unit == toy::style::Length::Unit::kPx &&
           NearlyEqual(margin.length.value, expected_value);
  };
  check(margin_matches(style.margin_top, expected.margin_top), "margin-top");
  check(margin_matches(style.margin_right, expected.margin_right), "margin-right");
  check(margin_matches(style.margin_bottom, expected.margin_bottom), "margin-bottom");
  check(margin_matches(style.margin_left, expected.margin_left), "margin-left");
}

// First block box for a DOM element, or nullptr.
const toy::layout::Box* FindElementBox(const toy::layout::BoxTree& boxes, toy::dom::NodeId node) {
  if (boxes.root() == toy::layout::kInvalidBox) {
    return nullptr;
  }
  std::vector<toy::layout::BoxId> pending;
  pending.push_back(boxes.root());
  while (!pending.empty()) {
    const toy::layout::BoxId box_id = pending.back();
    pending.pop_back();
    const toy::layout::Box& box = boxes.Get(box_id);
    if (box.node == node && box.kind == toy::layout::BoxKind::kBlock) {
      return &box;
    }
    std::vector<toy::layout::BoxId> children;
    for (toy::layout::BoxId child = box.first_child; child != toy::layout::kInvalidBox;
         child = boxes.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(children[index]);
    }
  }
  return nullptr;
}

void CheckElementBox(const toy::shell::Page& page, std::string_view id, int x, int y, int width,
                     int height, const char* description) {
  const std::string prefix = std::string(description) + " #" + std::string(id) + " box";
  const toy::dom::NodeId node = FindElementById(page.document, id);
  const toy::layout::Box* box =
      node == toy::dom::kInvalidNode ? nullptr : FindElementBox(page.boxes, node);
  if (box == nullptr) {
    std::fprintf(stderr, "  %s: no block box\n", prefix.c_str());
    toy_test::Check(false, "the fixture element has a block box");
    return;
  }
  const bool matches = RoundPx(box->x) == x && RoundPx(box->y) == y &&
                       RoundPx(box->width) == width && RoundPx(box->height) == height;
  if (!matches) {
    std::fprintf(stderr, "  %s: x=%d y=%d width=%d height=%d, expected %d/%d/%d/%d\n",
                 prefix.c_str(), RoundPx(box->x), RoundPx(box->y), RoundPx(box->width),
                 RoundPx(box->height), x, y, width, height);
  }
  toy_test::Check(matches, (prefix + ": frozen external-rule geometry").c_str());
}

std::uint32_t CountColor(const toy::render::Surface& surface, std::uint32_t color) {
  std::uint32_t count = 0;
  for (int y = 0; y < surface.height(); ++y) {
    for (int x = 0; x < surface.width(); ++x) {
      if (surface.At(x, y) == color) {
        ++count;
      }
    }
  }
  return count;
}

void CheckColorCount(const toy::render::Surface& surface, std::uint32_t color,
                     std::uint32_t expected, const char* description) {
  const std::uint32_t actual = CountColor(surface, color);
  if (actual != expected) {
    std::fprintf(stderr, "  %s: %u pixel(s), expected %u\n", description, actual, expected);
  }
  toy_test::Check(actual == expected, description);
}

void CheckPixel(const toy::render::Surface& surface, int x, int y, std::uint32_t expected,
                const char* description) {
  if (surface.At(x, y) != expected) {
    std::fprintf(stderr, "  %s: pixel (%d,%d)=0x%08x, expected 0x%08x\n", description, x, y,
                 surface.At(x, y), expected);
  }
  toy_test::Check(surface.At(x, y) == expected, description);
}

// True when two surfaces have identical pixels (same size asserted).
bool SamePixels(const toy::render::Surface& a, const toy::render::Surface& b) {
  if (a.width() != b.width() || a.height() != b.height() || a.stride() != a.width() ||
      b.stride() != b.width()) {
    return false;
  }
  const std::size_t bytes = static_cast<std::size_t>(a.width()) * a.height() *
                            sizeof(toy::render::Argb);
  return std::memcmp(a.pixels(), b.pixels(), bytes) == 0;
}

// --- Loopback HTTP server helpers -------------------------------------------

// The request target (path) of a raw request: the second token of the request line.
std::string RequestPath(std::string_view request) {
  const std::size_t method_end = request.find(' ');
  if (method_end == std::string_view::npos) {
    return {};
  }
  const std::size_t path_begin = method_end + 1;
  const std::size_t path_end = request.find(' ', path_begin);
  if (path_end == std::string_view::npos) {
    return {};
  }
  return std::string(request.substr(path_begin, path_end - path_begin));
}

void CheckRequestPaths(const toy_test::HttpServer& server, const std::vector<std::string>& expected,
                       const char* description) {
  const std::vector<std::string> requests = server.requests();
  std::vector<std::string> paths;
  paths.reserve(requests.size());
  for (const std::string& request : requests) {
    paths.push_back(RequestPath(request));
  }
  bool matches = paths.size() == expected.size();
  if (matches) {
    for (std::size_t index = 0; index < paths.size(); ++index) {
      if (paths[index] != expected[index]) {
        matches = false;
        break;
      }
    }
  }
  if (!matches) {
    std::fprintf(stderr, "  %s: got %zu request(s)", description, paths.size());
    for (const std::string& path : paths) {
      std::fprintf(stderr, " '%s'", path.c_str());
    }
    std::fprintf(stderr, "\n");
  }
  toy_test::Check(matches, description);
}

bool StartServer(toy_test::HttpServer* server, toy_test::HttpServer::Handler handler) {
  std::string error;
  const bool started = server->Start(std::move(handler), &error);
  if (!started) {
    std::fprintf(stderr, "  cannot start the test HTTP server: %s\n", error.c_str());
  }
  return started;
}

std::string HtmlResponse(std::string_view html) {
  return toy_test::RawHttpResponse(200, "text/html", html);
}

std::string CssResponse(std::string_view css) {
  return toy_test::RawHttpResponse(200, "text/css", css);
}

// A 302 response with a Location header (RawHttpResponse has no header slot).
std::string RedirectResponse(std::string_view location) {
  return "HTTP/1.1 302 Found\r\nLocation: " + std::string(location) +
         "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
}

// --- Tests ------------------------------------------------------------------

// index.html: the external sheet applies colors/font-size/margins and is
// interleaved with the inline sheets around it; an inline style declaration still
// wins; the page lays out with the frozen external-rule geometry.
void TestIndexFile(const std::string& site,
                   const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy::shell::Page page;
  std::string error;
  const std::string index_url = FileUrl(site, "index.html");
  const std::optional<toy::net::Url> url = ParseUrl(index_url);
  toy_test::Check(url.has_value(), "index.html URL parses");
  if (!url) {
    return;
  }
  toy_test::Check(toy::shell::LoadPage(*url, kViewportWidth, fonts, &page, &error),
                  "index.html loads through the styled-document path");
  toy_test::Check(error.empty(), "a successful external-CSS load clears the error");
  toy_test::Check(toy::net::SerializeUrl(page.final_url) == toy::net::SerializeUrl(*url),
                  "the final URL of a file page is the requested URL");

  // The external sheet's class rule applies (font-size and all four margins).
  CheckElementStyle(page, "external",
                    StyleExpectation{kExternalColorRgb, kExternalBackground, 24.0f, 6.0f, 10.0f,
                                     12.0f, 14.0f},
                    "the external class rule applies to");
  // Document order around the link: the external sheet comes after the first
  // inline sheet (#before green) and before the second one (#after blue).
  CheckElementStyle(page, "before", DefaultParagraphStyle(0x00FF00u),
                    "the later external rule wins for");
  CheckElementStyle(page, "after", DefaultParagraphStyle(0x0000FFu),
                    "the later inline rule wins for");
  // Inline style attribute beats the external sheet.
  CheckElementStyle(page, "inline", DefaultParagraphStyle(0x00AA00u),
                    "the inline style declaration wins for");

  // Frozen box geometry from the external rule (documented in the file header).
  CheckElementBox(page, "external", 22, 14, 280, 24, "the external font-size/margins lay out");
  toy_test::Check(NearlyEqual(toy::layout::ContentHeight(page.boxes), 202.0f),
                  "the external rule fixes the frozen page content height");
}

// ordering.html: equal-specificity author rules are decided by document order, so
// the linked sheet applies exactly at its <link> position: inline-before < link <
// inline-after.
void TestOrderingFile(const std::string& site,
                      const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy::shell::Page page;
  toy_test::Check(LoadUrl(FileUrl(site, "ordering.html"), fonts, &page),
                  "ordering.html loads");
  CheckElementStyle(page, "first", DefaultParagraphStyle(0x00FF00u),
                    "the linked sheet follows the first inline sheet for");
  CheckElementStyle(page, "second", DefaultParagraphStyle(0x0000FFu),
                    "the second inline sheet follows the link for");
  CheckElementStyle(page, "third", DefaultParagraphStyle(0x0000FFu),
                    "the last inline sheet wins for");
}

// filter.html: print media, text/plain type, disabled, alternate, ftp:, an empty
// href and a missing file are all skipped, while the last good sheet applies.
void TestFilterFile(const std::string& site,
                    const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy::shell::Page page;
  toy_test::Check(LoadUrl(FileUrl(site, "filter.html"), fonts, &page),
                  "filter.html loads although most of its links are skipped");
  CheckElementStyle(page, "good",
                    StyleExpectation{0x336699u, 0xFFCCCCCCu, 18.0f, 4.0f, 0.0f, 16.0f, 5.0f},
                    "only the good external sheet applies to");
  // The conflicting rules of the filtered sheets (red color, 11 px font, 99 px
  // top margin, blue background) left no trace.
  CheckElementStyle(page, "bad", DefaultParagraphStyle(0x000000u),
                    "the filtered sheets contribute nothing to");
  // The same page links one sheet from the body: discovery covers the whole DOM,
  // head or body alike.
  CheckElementStyle(page, "body-sheet",
                    StyleExpectation{0x445566u, 0x00000000u, 14.0f, 16.0f, 0.0f, 16.0f, 0.0f},
                    "the body <link> sheet applies to");
}

// filtered_only.html renders byte-identically to no_sheet.html, and neither page
// makes a stylesheet request (both have no applicable sheet).
void TestFilteredOnlyMatchesNoSheet(const std::string& site,
                                    const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy::shell::Page no_sheet;
  toy::shell::Page filtered_only;
  toy_test::Check(LoadUrl(FileUrl(site, "no_sheet.html"), fonts, &no_sheet),
                  "no_sheet.html loads");
  toy_test::Check(LoadUrl(FileUrl(site, "filtered_only.html"), fonts, &filtered_only),
                  "filtered_only.html loads although all of its links are skipped");
  toy_test::Check(
      toy::layout::DumpBoxTree(no_sheet.document, no_sheet.boxes) ==
          toy::layout::DumpBoxTree(filtered_only.document, filtered_only.boxes),
      "filtered_only.html has the same box tree as no_sheet.html");
  CheckElementStyle(filtered_only, "plain",
                    DefaultParagraphStyle(0x000000u),
                    "the skipped links leave the default style on");

  // Whole-canvas pixels: the two pages must be indistinguishable.
  std::string error;
  toy::shell::Browser no_sheet_browser(kViewportWidth, kViewportHeight, fonts);
  toy::shell::Browser filtered_only_browser(kViewportWidth, kViewportHeight, fonts);
  toy_test::Check(no_sheet_browser.Open(FileUrl(site, "no_sheet.html"), &error),
                  "no_sheet.html loads in a Browser session");
  toy_test::Check(filtered_only_browser.Open(FileUrl(site, "filtered_only.html"), &error),
                  "filtered_only.html loads in a Browser session");
  toy::render::Surface no_sheet_surface(kViewportWidth, kViewportHeight);
  toy::render::Surface filtered_only_surface(kViewportWidth, kViewportHeight);
  no_sheet_browser.Paint(no_sheet_surface);
  filtered_only_browser.Paint(filtered_only_surface);
  toy_test::Check(SamePixels(no_sheet_surface, filtered_only_surface),
                  "filtered_only.html paints the same pixels as no_sheet.html");
}

// duplicate.html: the same sheet URL twice applies stably.
void TestDuplicateFile(const std::string& site,
                       const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy::shell::Page page;
  toy_test::Check(LoadUrl(FileUrl(site, "duplicate.html"), fonts, &page),
                  "duplicate.html loads with the same sheet linked twice");
  CheckElementStyle(page, "dup", StyleExpectation{0x0F0F0Fu, 0x00000000u, 20.0f, 16.0f, 0.0f,
                                                  16.0f, 7.0f},
                    "the duplicated external sheet applies to");
}

// Browser/Paint: the external background-color and the cascade result reach the
// canvas (frozen bitmap-font pixels from the file header).
void TestPixels(const std::string& site, const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  std::string error;
  toy::shell::Browser browser(kViewportWidth, kViewportHeight, fonts);
  toy_test::Check(browser.Open(FileUrl(site, "index.html"), &error),
                  "index.html loads in a Browser session");
  toy::render::Surface surface(kViewportWidth, kViewportHeight);
  browser.Paint(surface);
  // The external #abcdef background fills the .external box (280x24 = 6720 pixels
  // minus the 330 #123456 text pixels) and is visible away from the text.
  CheckPixel(surface, 250, 20, kExternalBackground,
             "the external background-color reaches the canvas");
  CheckPixel(surface, 24, 19, kExternalColorArgb, "the external text color reaches the canvas");
  CheckColorCount(surface, kExternalBackground, 6390,
                  "the external background covers exactly the frozen box area");
  CheckColorCount(surface, kExternalColorArgb, 330,
                  "the external text color paints exactly the frozen ink");
  CheckColorCount(surface, kBeforeGreen, 102, "the later external rule paints #before green");
  CheckColorCount(surface, kAfterBlue, 75, "the later inline rule paints #after blue");
  CheckColorCount(surface, kInlineGreen, 85, "the inline declaration paints #inline green");
  toy_test::Check(surface.At(0, 0) == kWhite, "the canvas outside the page stays white");
}

// Redirecting stylesheet URL: the <link> fetch follows the 302 and applies the
// final sheet; the request sequence proves each hop was fetched once.
void TestRedirectingSheet(const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy_test::HttpServer server;
  const std::string page_html =
      "<!doctype html><html><head><title>redirect</title>"
      "<link rel=\"stylesheet\" href=\"/redirect.css\"></head>"
      "<body><p id=\"r\">Redirected sheet</p></body></html>\n";
  const bool started = StartServer(&server, [&](std::string_view request) -> std::string {
    const std::string path = RequestPath(request);
    if (path == "/page.html") {
      return HtmlResponse(page_html);
    }
    if (path == "/redirect.css") {
      return RedirectResponse("/real.css");
    }
    if (path == "/real.css") {
      return CssResponse("#r { color: #0a0b0c; }\n");
    }
    return toy_test::RawHttpResponse(404, "text/plain", "not found");
  });
  toy_test::Check(started, "the redirecting-stylesheet test server starts");
  if (!started) {
    return;
  }
  toy::shell::Page page;
  toy_test::Check(LoadUrl(server.Url("/page.html"), fonts, &page),
                  "a page whose stylesheet redirects loads");
  CheckElementStyle(page, "r", DefaultParagraphStyle(0x0A0B0C),
                    "the redirected sheet rule applies to");
  CheckRequestPaths(server, {"/page.html", "/redirect.css", "/real.css"},
                    "the redirecting sheet is fetched in page/link/final order");
  server.Stop();
}

// Absolute http:// sheet URL.
void TestAbsoluteSheetUrl(const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy_test::HttpServer server;
  const std::string absolute_path = "/absolute.css";
  const bool started = StartServer(&server, [&](std::string_view request) -> std::string {
    const std::string path = RequestPath(request);
    if (path == "/page.html") {
      return HtmlResponse("<!doctype html><html><head>"
                          "<link rel=\"stylesheet\" href=\"" +
                          server.Url(absolute_path) +
                          "\"></head><body><p id=\"r\">Absolute</p></body></html>\n");
    }
    if (path == absolute_path) {
      return CssResponse("#r { color: #0c0b0a; }\n");
    }
    return toy_test::RawHttpResponse(404, "text/plain", "not found");
  });
  toy_test::Check(started, "the absolute-URL test server starts");
  if (!started) {
    return;
  }
  toy::shell::Page page;
  toy_test::Check(LoadUrl(server.Url("/page.html"), fonts, &page),
                  "a page with an absolute http:// sheet URL loads");
  CheckElementStyle(page, "r", DefaultParagraphStyle(0x0C0B0A),
                    "the absolute-URL sheet rule applies to");
  CheckRequestPaths(server, {"/page.html", absolute_path},
                    "the absolute sheet URL is fetched once");
  server.Stop();
}

// Page redirect + relative sheet: href resolves against the page's FINAL URL.
void TestSheetResolvesAgainstFinalUrl(const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy_test::HttpServer server;
  const bool started = StartServer(&server, [&](std::string_view request) -> std::string {
    const std::string path = RequestPath(request);
    if (path == "/start.html") {
      return RedirectResponse("/sub/page.html");
    }
    if (path == "/sub/page.html") {
      return HtmlResponse("<!doctype html><html><head><title>final url</title>"
                          "<link rel=\"stylesheet\" href=\"relative.css\"></head>"
                          "<body><p id=\"r\">Final URL</p></body></html>\n");
    }
    if (path == "/sub/relative.css") {
      return CssResponse("#r { color: #0d0e0f; }\n");
    }
    return toy_test::RawHttpResponse(404, "text/plain", "not found");
  });
  toy_test::Check(started, "the final-URL test server starts");
  if (!started) {
    return;
  }
  toy::shell::Page page;
  toy_test::Check(LoadUrl(server.Url("/start.html"), fonts, &page),
                  "a redirected page with a relative sheet link loads");
  toy_test::Check(page.final_url.path == "/sub/page.html",
                  "the page's final URL is the redirect target");
  CheckElementStyle(page, "r", DefaultParagraphStyle(0x0D0E0F),
                    "the relative sheet resolves against the final URL for");
  CheckRequestPaths(server, {"/start.html", "/sub/page.html", "/sub/relative.css"},
                    "the relative sheet is requested under the final page URL");
  server.Stop();
}

// A page whose only stylesheet link fails still loads (skip silently).
void TestFailingOnlyLink(const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy_test::HttpServer server;
  const bool started = StartServer(&server, [&](std::string_view request) -> std::string {
    const std::string path = RequestPath(request);
    if (path == "/page.html") {
      return HtmlResponse("<!doctype html><html><head>"
                          "<link rel=\"stylesheet\" href=\"/missing.css\"></head>"
                          "<body><p id=\"r\">Broken sheet</p></body></html>\n");
    }
    return toy_test::RawHttpResponse(404, "text/plain", "not found");
  });
  toy_test::Check(started, "the failing-sheet test server starts");
  if (!started) {
    return;
  }
  toy::shell::Page page;
  toy_test::Check(LoadUrl(server.Url("/page.html"), fonts, &page),
                  "a page whose only stylesheet returns 404 still loads");
  CheckElementStyle(page, "r", DefaultParagraphStyle(0x000000u),
                    "the failed sheet leaves the default style on");
  CheckRequestPaths(server, {"/page.html", "/missing.css"},
                    "the failing sheet is attempted exactly once");
  server.Stop();
}

// A page without any <link> makes exactly one request: no stylesheet fetch.
void TestPageWithoutLinkFetchesOnce(const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy_test::HttpServer server;
  const bool started = StartServer(&server, [&](std::string_view request) -> std::string {
    if (RequestPath(request) == "/plain.html") {
      return HtmlResponse("<!doctype html><html><head><title>plain</title></head>"
                          "<body><p id=\"r\">No link</p></body></html>\n");
    }
    return toy_test::RawHttpResponse(404, "text/plain", "not found");
  });
  toy_test::Check(started, "the no-link test server starts");
  if (!started) {
    return;
  }
  toy::shell::Page page;
  toy_test::Check(LoadUrl(server.Url("/plain.html"), fonts, &page),
                  "a page without <link> loads");
  CheckRequestPaths(server, {"/plain.html"}, "a page without <link> makes exactly one request");
  server.Stop();
}

// No deduplication/cache: the same sheet URL linked twice is fetched twice.
void TestDuplicateLinksAreFetchedTwice(const std::shared_ptr<const toy::shell::Fonts>& fonts) {
  toy_test::HttpServer server;
  const bool started = StartServer(&server, [&](std::string_view request) -> std::string {
    const std::string path = RequestPath(request);
    if (path == "/page.html") {
      return HtmlResponse("<!doctype html><html><head>"
                          "<link rel=\"stylesheet\" href=\"/dup.css\">"
                          "<link rel=\"stylesheet\" href=\"/dup.css\"></head>"
                          "<body><p id=\"r\">Duplicate</p></body></html>\n");
    }
    if (path == "/dup.css") {
      return CssResponse("#r { color: #111213; }\n");
    }
    return toy_test::RawHttpResponse(404, "text/plain", "not found");
  });
  toy_test::Check(started, "the duplicate-link test server starts");
  if (!started) {
    return;
  }
  toy::shell::Page page;
  toy_test::Check(LoadUrl(server.Url("/page.html"), fonts, &page),
                  "a page linking the same sheet twice loads");
  CheckElementStyle(page, "r", DefaultParagraphStyle(0x111213),
                    "the duplicated sheet rule applies to");
  CheckRequestPaths(server, {"/page.html", "/dup.css", "/dup.css"},
                    "the same sheet URL is fetched once per link occurrence");
  server.Stop();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <css_site-directory>\n", argv[0]);
    return 1;
  }
  const std::string site = argv[1];
  // Automatic font selection with the empty FONTCONFIG_FILE test config is the
  // deterministic built-in bitmap font path, exactly like the CLI goldens.
  const std::shared_ptr<const toy::shell::Fonts> fonts = toy::shell::Fonts::SystemDefault();

  TestIndexFile(site, fonts);
  TestOrderingFile(site, fonts);
  TestFilterFile(site, fonts);
  TestFilteredOnlyMatchesNoSheet(site, fonts);
  TestDuplicateFile(site, fonts);
  TestPixels(site, fonts);
  TestRedirectingSheet(fonts);
  TestAbsoluteSheetUrl(fonts);
  TestSheetResolvesAgainstFinalUrl(fonts);
  TestFailingOnlyLink(fonts);
  TestPageWithoutLinkFetchesOnce(fonts);
  TestDuplicateLinksAreFetchedTwice(fonts);
  return toy_test::Finish("shell external css");
}
