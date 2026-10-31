// M9 end-to-end HTTPS test: fetch -> parse -> style -> image fetch/decode ->
// layout -> paint through toy::shell::Browser against a committed page served by
// the in-process TLS loopback server (tests/support/tls_server.h). No display and
// no public network are used; the test-only CA in tests/fixtures/tls/ is trusted
// through the CTest ENVIRONMENT property SSL_CERT_FILE=<tls>/test_ca.pem, so no
// trust override happens in code and certificate verification stays enabled.
//
// The served fixture is tests/fixtures/https_site/index.html plus its relative
// image flag.png. The test reads both files into memory and the server answers
// exactly the two request targets the page needs; the fixture README documents
// the served content and the frozen geometry/pixels asserted below.
//
// Expected geometry is frozen from a `toybrowser --size 320x200 --dump-boxes`
// run on the same fixture (1 CSS px = 1 device px), so the TLS transport changes
// no layout. Relevant dump lines:
//
//   <body> block x=8 y=8 width=304 height=168
//     <h1> block x=8 y=24 width=304 height=32
//       text "Secure" x=8 y=24 width=96 height=32
//     <p> block x=8 y=88 width=304 height=24
//       text "HTTPS" x=8 y=88 width=40 height=24
//       text "text" x=56 y=88 width=32 height=24
//       <img> inline x=96 y=88 width=48 height=24
//       text "after" x=152 y=88 width=40 height=24
//     <p> block x=8 y=144 width=304 height=16
//       text "Second" x=8 y=144 width=48 height=16
//       text "paragraph" x=64 y=144 width=72 height=16
//       text "line." x=144 y=144 width=40 height=16
//
// The content height is 184 device px, so the whole page fits the 320x200
// viewport at scroll 0 and the image is fully visible.

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/check.h"
#include "support/tls_server.h"
#include "toy/render/surface.h"
#include "toy/shell/browser.h"

namespace {

constexpr int kViewportWidth = 320;
constexpr int kViewportHeight = 200;
constexpr int kContentHeight = 184;

// Frozen image box: the 16x8 flag_16x8 content scaled by CSS to 48x24. Nearest
// neighbour maps destination columns 0-23 to source columns 0-7 (red) and
// destination columns 24-47 to source columns 8-15 (blue), all 24 rows identical.
constexpr int kImageX = 96;
constexpr int kImageY = 88;
constexpr int kImageWidth = 48;
constexpr int kImageHeight = 24;

// Frozen text regions (half-open rectangles). Only the presence of ink is
// asserted: the hand-authored bitmap glyph shapes are deliberately not frozen.
constexpr int kHeadingX0 = 8;
constexpr int kHeadingY0 = 24;
constexpr int kHeadingX1 = 104;
constexpr int kHeadingY1 = 56;
constexpr int kTextBeforeX0 = 8;
constexpr int kTextBeforeY0 = 88;
constexpr int kTextBeforeX1 = 96;
constexpr int kTextBeforeY1 = 112;
constexpr int kTextAfterX0 = 152;
constexpr int kTextAfterY0 = 88;
constexpr int kTextAfterX1 = 192;
constexpr int kTextAfterY1 = 112;
constexpr int kSecondParagraphX0 = 8;
constexpr int kSecondParagraphY0 = 144;
constexpr int kSecondParagraphX1 = 184;
constexpr int kSecondParagraphY1 = 160;

constexpr toy::render::Argb kWhite = 0xFFFFFFFFu;
constexpr toy::render::Argb kRed = 0xFFFF0000u;
constexpr toy::render::Argb kBlue = 0xFF0000FFu;

// Reads a whole file as bytes (the fixtures are served from memory) so the test
// never depends on the server reading from disk.
bool ReadFile(const std::string& path, std::string* out, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    *error = "cannot open '" + path + "'";
    return false;
  }
  out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  if (in.bad()) {
    *error = "cannot read '" + path + "'";
    return false;
  }
  return true;
}

// The fixture files plus a log of the request targets the server answered, so
// the test can prove both the page and its image were actually fetched over TLS.
struct ServedContent {
  std::vector<std::pair<std::string, std::string>> files;  // target -> body
  std::mutex mutex;
  std::vector<std::string> requested;

  const std::string* Find(std::string_view target) const {
    for (const auto& [name, body] : files) {
      if (name == target) {
        return &body;
      }
    }
    return nullptr;
  }
};

toy_test::TlsServer::Handler MakeHandler(const std::shared_ptr<ServedContent>& content) {
  return [content](std::string_view request_target) {
    {
      const std::lock_guard<std::mutex> lock(content->mutex);
      content->requested.emplace_back(request_target);
    }
    const std::string* body = content->Find(request_target);
    if (body == nullptr) {
      return toy_test::HttpResponse{404, {}, "not found"};
    }
    toy_test::HttpResponse response;
    response.status = 200;
    response.body = *body;
    return response;
  };
}

bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle) {
  const auto lower = [](char c) {
    return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  };
  for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
    std::size_t index = 0;
    while (index < needle.size() && lower(haystack[start + index]) == lower(needle[index])) {
      ++index;
    }
    if (index == needle.size()) {
      return true;
    }
  }
  return false;
}

int CountColor(const toy::render::Surface& surface, toy::render::Argb color) {
  int count = 0;
  for (int y = 0; y < surface.height(); ++y) {
    for (int x = 0; x < surface.width(); ++x) {
      if (surface.At(x, y) == color) {
        ++count;
      }
    }
  }
  return count;
}

// Exact counts prove scale, blend and clipping together; a wrong scale, skipped
// image or clipped viewport changes them.
void CheckCount(const toy::render::Surface& surface, toy::render::Argb color, int expected,
                const char* description) {
  const int actual = CountColor(surface, color);
  if (actual != expected) {
    std::fprintf(stderr, "  %s: %d pixel(s), expected %d\n", description, actual, expected);
  }
  toy_test::Check(actual == expected, description);
}

int CountNonWhiteInRect(const toy::render::Surface& surface, int x0, int y0, int x1, int y1) {
  int count = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      if (surface.At(x, y) != kWhite) {
        ++count;
      }
    }
  }
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <https_site-directory> <tls-fixtures-directory>\n", argv[0]);
    return 1;
  }
  const std::string site = argv[1];
  const std::string tls = argv[2];

  std::string index_html;
  std::string flag_png;
  std::string read_error;
  if (!ReadFile(site + "/index.html", &index_html, &read_error) ||
      !ReadFile(site + "/flag.png", &flag_png, &read_error)) {
    std::fprintf(stderr, "cannot read the HTTPS fixture: %s\n", read_error.c_str());
    return 1;
  }

  auto content = std::make_shared<ServedContent>();
  content->files.emplace_back("/index.html", std::move(index_html));
  content->files.emplace_back("/flag.png", std::move(flag_png));

  // --- Trusted server: the committed fixture page renders end to end ----------
  toy_test::TlsServer server;
  std::string server_error;
  if (!server.Start(tls + "/localhost_cert.pem", tls + "/localhost_key.pem", MakeHandler(content),
                    &server_error)) {
    std::fprintf(stderr, "cannot start the trusted TLS test server: %s\n", server_error.c_str());
    return 1;
  }

  {
    toy::shell::Browser browser(kViewportWidth, kViewportHeight);
    std::string error;
    if (!browser.Open(server.Url("/index.html"), &error)) {
      // The most likely cause is a missing SSL_CERT_FILE: the CTest registration
      // sets it to the committed test CA and the test itself never overrides trust.
      std::fprintf(stderr,
                   "Open(https://localhost:.../index.html) failed: %s\n"
                   "  (the CTest ENVIRONMENT property must point SSL_CERT_FILE at the test CA)\n",
                   error.c_str());
    }
    toy_test::Check(browser.has_page(), "the trusted HTTPS page load succeeded");
    toy_test::Check(error.empty(), "a successful HTTPS load clears the error");

    // --- URL: https is accepted and the loopback port is kept ----------------
    toy_test::Check(browser.current_url().scheme == "https", "current_url().scheme is https");
    toy_test::Check(browser.current_url().host == "localhost", "current_url().host is localhost");
    toy_test::Check(browser.current_url().port == server.port(),
                    "current_url().port is the TLS server's ephemeral port");
    toy_test::Check(browser.current_url().path == "/index.html",
                    "current_url().path is the fetched page");
    toy_test::Check(browser.current_url().query.empty() && browser.current_url().fragment.empty(),
                    "the fixture URL has no query or fragment");
    toy_test::Check(browser.title() == "HTTPS fixture page",
                    "the title comes from the served <title>");
    toy_test::Check(browser.scroll_y() == 0, "a fresh HTTPS page starts at scroll 0");
    toy_test::Check(browser.content_height() == kContentHeight,
                    "the content height matches the frozen box dump");

    // --- Paint: text regions and the image decoded over TLS ------------------
    toy::render::Surface surface(kViewportWidth, kViewportHeight);
    browser.Paint(surface);

    // Text regions: the heading, the words before and after the image and the
    // second paragraph all paint ink from the fetched-and-parsed document.
    toy_test::Check(
        CountNonWhiteInRect(surface, kHeadingX0, kHeadingY0, kHeadingX1, kHeadingY1) > 0,
        "the h1 text region is painted");
    toy_test::Check(CountNonWhiteInRect(surface, kTextBeforeX0, kTextBeforeY0, kTextBeforeX1,
                                        kTextBeforeY1) > 0,
                    "the paragraph text before the image is painted");
    toy_test::Check(
        CountNonWhiteInRect(surface, kTextAfterX0, kTextAfterY0, kTextAfterX1, kTextAfterY1) > 0,
        "the paragraph text after the image is painted");
    toy_test::Check(CountNonWhiteInRect(surface, kSecondParagraphX0, kSecondParagraphY0,
                                        kSecondParagraphX1, kSecondParagraphY1) > 0,
                    "the second paragraph is painted");

    // The image is the only red/blue content on the page, so exact counts over
    // the whole surface prove that the PNG was decoded and painted over HTTPS at
    // the frozen 48x24 nearest-neighbour scale: 24 red columns * 24 rows and
    // 24 blue columns * 24 rows.
    toy_test::Check(surface.At(kImageX, kImageY) == kRed, "the image's top-left pixel is red");
    toy_test::Check(surface.At(kImageX + 23, kImageY) == kRed, "the last red scaled column is red");
    toy_test::Check(surface.At(kImageX + 24, kImageY) == kBlue,
                    "the first blue scaled column is blue");
    toy_test::Check(surface.At(kImageX + kImageWidth - 1, kImageY + kImageHeight - 1) == kBlue,
                    "the image's bottom-right pixel is blue");
    toy_test::Check(surface.At(kImageX, kImageY - 1) == kWhite,
                    "the row above the image stays white");
    toy_test::Check(surface.At(kImageX + kImageWidth, kImageY) == kWhite,
                    "the column after the image stays white");
    CheckCount(surface, kRed, 24 * 24, "exact red pixel count proves the 48x24 image scale");
    CheckCount(surface, kBlue, 24 * 24, "exact blue pixel count proves the 48x24 image scale");

    // The whole page fits the viewport at scroll 0; below the content the canvas
    // stays white (no stray or clipped painting).
    toy_test::Check(
        CountNonWhiteInRect(surface, 0, kContentHeight, kViewportWidth, kViewportHeight) == 0,
        "the area below the page content stays white");

    // The page and its relative image were actually requested over TLS. The
    // fetch order is deterministic: the document first, then its <img>.
    {
      const std::lock_guard<std::mutex> lock(content->mutex);
      toy_test::Check(content->requested.size() == 2,
                      "the TLS server answered exactly the page and its image request");
      if (content->requested.size() == 2) {
        toy_test::Check(content->requested[0] == "/index.html",
                        "the first TLS request is the page");
        toy_test::Check(content->requested[1] == "/flag.png",
                        "the second TLS request is the page-relative image");
      }
    }
  }

  server.Stop();

  // --- Untrusted certificate: the load fails and leaves no page ---------------
  {
    toy_test::TlsServer untrusted;
    std::string untrusted_error;
    if (!untrusted.Start(tls + "/untrusted_cert.pem", tls + "/untrusted_key.pem",
                         MakeHandler(content), &untrusted_error)) {
      std::fprintf(stderr, "cannot start the untrusted TLS test server: %s\n",
                   untrusted_error.c_str());
      return 1;
    }

    toy::shell::Browser browser(kViewportWidth, kViewportHeight);
    std::string error;
    toy_test::Check(!browser.Open(untrusted.Url("/index.html"), &error),
                    "Open() fails for a certificate that does not chain to the trusted CA");
    toy_test::Check(!error.empty(), "the certificate failure reports an error");
    toy_test::Check(ContainsCaseInsensitive(error, "certificate"),
                    "the certificate failure error mentions the certificate");
    toy_test::Check(!browser.has_page(), "a failed certificate load leaves no page");
    toy_test::Check(browser.content_height() == 0,
                    "a failed certificate load has no content height");
    toy_test::Check(browser.title().empty(), "a failed certificate load has no title");
    toy_test::Check(browser.current_url().scheme.empty(),
                    "a failed certificate load keeps the default current URL");
  }

  return toy_test::Finish("shell https test");
}
