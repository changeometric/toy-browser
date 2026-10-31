// M13 end-to-end resource loading test: one net::Client per page load (keep-alive
// for the page, its stylesheets and its images) and a per-load image URL cache.
//
// Everything is served by the in-process loopback HTTP server
// (tests/support/http_server.h), which keeps a connection open unless the response
// says `Connection: close`. That makes the server counters the evidence:
// `connection_count()` proves the whole load reused one connection and
// `request_count()` proves image URL deduplication. The page HTML is generated in
// this file; the only fixture read is the committed 16x8 image
// (tests/fixtures/images/flag_16x8.png: columns 0-7 red, columns 8-15 blue).
//
// Frozen geometry (bitmap font path, 320 px viewport, body margin 8, UA p margin
// 16): `<p>` at x=8 y=24, the two 16x8 <img> occurrences at x=8 and x=24 (top
// aligned on the 16 px line) and the trailing text at x=40 y=24 height=16.
//
// No public network, display or installed font is used (the generated empty
// FONTCONFIG_FILE config pins the built-in bitmap font path).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "page.h"
#include "support/check.h"
#include "support/http_server.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/net/url.h"
#include "toy/render/surface.h"
#include "toy/shell/browser.h"

namespace {

constexpr int kViewportWidth = 320;
constexpr int kViewportHeight = 200;

// Painted fixture pixels: flag_16x8.png, opaque; the stylesheet text color of
// #note is #445566 (ARGB with alpha 255).
constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
constexpr std::uint32_t kRed = 0xFFFF0000u;
constexpr std::uint32_t kBlue = 0xFF0000FFu;
constexpr std::uint32_t kNoteColor = 0xFF445566u;

bool ReadFile(const std::string& path, std::string* bytes) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::fprintf(stderr, "  cannot open fixture '%s'\n", path.c_str());
    return false;
  }
  bytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return true;
}

// A complete HTTP/1.1 response that keeps the connection alive: Content-Length
// framing and no `Connection: close`, which is what the loopback server needs to
// serve another request on the same connection.
std::string KeepAliveResponse(int status, std::string_view content_type,
                              std::string_view body) {
  std::string response = "HTTP/1.1 " + std::to_string(status) + " ";
  if (status == 200) {
    response += "OK";
  } else if (status == 404) {
    response += "Not Found";
  } else {
    response += "Status";
  }
  response += "\r\nContent-Type: ";
  response.append(content_type);
  response += "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  response.append(body);
  return response;
}

// The request target (path) of a raw HTTP request: the second token of the
// request line.
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

std::vector<std::string> RequestPaths(const toy_test::HttpServer& server) {
  std::vector<std::string> paths;
  for (const std::string& request : server.requests()) {
    paths.push_back(RequestPath(request));
  }
  return paths;
}

void CheckEqual(int actual, int expected, const char* description) {
  if (actual != expected) {
    std::fprintf(stderr, "  %s: %d, expected %d\n", description, actual, expected);
  }
  toy_test::Check(actual == expected, description);
}

bool StartServer(toy_test::HttpServer* server, toy_test::HttpServer::Handler handler) {
  std::string error;
  const bool started = server->Start(std::move(handler), &error);
  if (!started) {
    std::fprintf(stderr, "  cannot start the test HTTP server: %s\n", error.c_str());
  }
  return started;
}

std::optional<toy::net::Url> ParseUrl(const std::string& url_text) {
  std::string error;
  std::optional<toy::net::Url> url = toy::net::ParseUrl(url_text, &error);
  if (!url) {
    std::fprintf(stderr, "  cannot parse URL '%s': %s\n", url_text.c_str(), error.c_str());
  }
  return url;
}

// Loads `url_text` through the shell-private page loader used by the window and
// --dump-boxes paths, with null fonts (deterministic built-in bitmap metrics).
bool LoadUrl(const std::string& url_text, toy::shell::Page* page) {
  const std::optional<toy::net::Url> url = ParseUrl(url_text);
  if (!url) {
    return false;
  }
  std::string error;
  if (!toy::shell::LoadPage(*url, kViewportWidth, nullptr, page, &error)) {
    std::fprintf(stderr, "  cannot load '%s': %s\n", url_text.c_str(), error.c_str());
    return false;
  }
  return true;
}

// All element node ids with tag name `name`, in document order.
std::vector<toy::dom::NodeId> FindElements(const toy::dom::Document& document,
                                           std::string_view name) {
  std::vector<toy::dom::NodeId> ids;
  std::vector<toy::dom::NodeId> pending;
  pending.push_back(document.root());
  while (!pending.empty()) {
    const toy::dom::NodeId id = pending.back();
    pending.pop_back();
    const toy::dom::Node& node = document.Get(id);
    if (node.type == toy::dom::NodeType::kElement && node.name == name) {
      ids.push_back(id);
    }
    for (toy::dom::NodeId child = node.last_child; child != toy::dom::kInvalidNode;
         child = document.Get(child).prev_sibling) {
      pending.push_back(child);
    }
  }
  return ids;
}

const toy::layout::Box* FindBoxForNode(const toy::layout::BoxTree& boxes, toy::dom::NodeId node,
                                       toy::layout::BoxKind kind) {
  if (boxes.root() == toy::layout::kInvalidBox) {
    return nullptr;
  }
  std::vector<toy::layout::BoxId> pending;
  pending.push_back(boxes.root());
  while (!pending.empty()) {
    const toy::layout::BoxId box_id = pending.back();
    pending.pop_back();
    const toy::layout::Box& box = boxes.Get(box_id);
    if (box.node == node && box.kind == kind) {
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

int RoundPx(float value) { return static_cast<int>(std::lround(value)); }

void CheckBox(const toy::layout::BoxTree& boxes, toy::dom::NodeId node, toy::layout::BoxKind kind,
              int x, int y, int width, int height, const char* description) {
  const toy::layout::Box* box = FindBoxForNode(boxes, node, kind);
  if (box == nullptr) {
    std::fprintf(stderr, "  %s: box not found\n", description);
    toy_test::Check(false, description);
    return;
  }
  const bool matches = RoundPx(box->x) == x && RoundPx(box->y) == y &&
                       RoundPx(box->width) == width && RoundPx(box->height) == height;
  if (!matches) {
    std::fprintf(stderr, "  %s: x=%d y=%d width=%d height=%d, expected %d/%d/%d/%d\n", description,
                 RoundPx(box->x), RoundPx(box->y), RoundPx(box->width), RoundPx(box->height), x, y,
                 width, height);
  }
  toy_test::Check(matches, description);
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

// --- Cases ------------------------------------------------------------------

// Page + one stylesheet + two <img> occurrences of the same URL: the whole load
// runs on one keep-alive connection and the image URL is fetched once and decoded
// once; both occurrences share the same render::Image object.
void TestDuplicateImage(const std::string& png_bytes) {
  toy_test::HttpServer server;
  const std::string page_html =
      "<!doctype html><html><head><title>duplicate images</title>"
      "<link rel=\"stylesheet\" href=\"/same.css\"></head><body><p>"
      "<img src=\"/same.png\"><img src=\"/same.png\"><span id=\"note\">Text</span>"
      "</p></body></html>\n";
  const bool started = StartServer(&server, [&](std::string_view request) -> std::string {
    const std::string path = RequestPath(request);
    if (path == "/page.html") {
      return KeepAliveResponse(200, "text/html", page_html);
    }
    if (path == "/same.css") {
      return KeepAliveResponse(200, "text/css", "#note { color: #445566; }\n");
    }
    if (path == "/same.png") {
      return KeepAliveResponse(200, "image/png", png_bytes);
    }
    return KeepAliveResponse(404, "text/plain", "");
  });
  toy_test::Check(started, "the duplicate-image test server starts");
  if (!started) {
    return;
  }

  toy::shell::Page page;
  toy_test::Check(LoadUrl(server.Url("/page.html"), &page),
                  "a page with a stylesheet and duplicate images loads");
  // One connection for page + stylesheet + image, and exactly one request per
  // unique resource despite the two <img> occurrences.
  CheckEqual(server.connection_count(), 1,
             "page, sheet and image share one keep-alive connection");
  CheckEqual(server.request_count(), 3, "page/sheet/image make three requests in total");
  const std::vector<std::string> paths = RequestPaths(server);
  toy_test::Check(paths == std::vector<std::string>{"/page.html", "/same.css", "/same.png"},
                  "the requests happen in document order on that connection");

  const std::vector<toy::dom::NodeId> images = FindElements(page.document, "img");
  toy_test::Check(images.size() == 2, "the fixture page has two <img> elements");
  if (images.size() != 2) {
    server.Stop();
    return;
  }
  // The stylesheet applied (proves its fetch was used, not just attempted).
  const std::vector<toy::dom::NodeId> notes = FindElements(page.document, "span");
  toy_test::Check(notes.size() == 1, "the fixture page has the #note span");
  if (notes.size() == 1) {
    toy_test::Check(page.styles.Get(notes[0]).color == 0x445566u,
                    "the keep-alive stylesheet fetch reached the cascade");
  }
  // Both decoded images come from one cached object, and every occurrence has its
  // own map entry and its own layout box.
  const auto first = page.images.find(images[0]);
  const auto second = page.images.find(images[1]);
  toy_test::Check(page.images.size() == 2, "both <img> occurrences have a decoded image entry");
  if (first != page.images.end() && second != page.images.end()) {
    toy_test::Check(first->second == second->second,
                    "duplicate image URLs share the same decoded render::Image object");
  }
  toy_test::Check(page.replaced_sizes.size() == 2,
                  "both <img> occurrences have their own replaced size");
  CheckBox(page.boxes, images[0], toy::layout::BoxKind::kInline, 8, 24, 16, 8,
           "the first duplicate <img> has the frozen 16x8 box at x=8");
  CheckBox(page.boxes, images[1], toy::layout::BoxKind::kInline, 24, 24, 16, 8,
           "the second duplicate <img> has the frozen 16x8 box at x=24");

  // Painting: a second load (a new page load and therefore a new Client) through
  // Browser paints the same page; pixels prove the shown images and styles.
  std::string error;
  toy::shell::Browser browser(kViewportWidth, kViewportHeight);
  toy_test::Check(browser.Open(server.Url("/page.html"), &error),
                  "the duplicate-image page loads through Browser for painting");
  toy::render::Surface surface(kViewportWidth, kViewportHeight);
  browser.Paint(surface);
  toy_test::Check(surface.At(8, 24) == kRed, "the first image's red half is painted");
  toy_test::Check(surface.At(16, 24) == kBlue, "the first image's blue half is painted");
  toy_test::Check(surface.At(24, 24) == kRed, "the duplicate image's red half is painted");
  toy_test::Check(surface.At(32, 24) == kBlue, "the duplicate image's blue half is painted");
  toy_test::Check(surface.At(0, 0) == kWhite, "the canvas outside the images stays white");
  toy_test::Check(CountColor(surface, kNoteColor) > 0,
                  "the stylesheet text color is painted after the keep-alive load");
  server.Stop();
}

// The same broken image URL twice: it is fetched once and both occurrences are
// skipped (remembered failure), and the page still loads.
void TestDuplicateMissingImage() {
  toy_test::HttpServer server;
  const std::string page_html =
      "<!doctype html><html><head><title>missing images</title></head><body><p>"
      "<img src=\"/missing.png\"><img src=\"/missing.png\">After"
      "</p></body></html>\n";
  const bool started = StartServer(&server, [&](std::string_view request) -> std::string {
    const std::string path = RequestPath(request);
    if (path == "/page.html") {
      return KeepAliveResponse(200, "text/html", page_html);
    }
    return KeepAliveResponse(404, "text/plain", "");
  });
  toy_test::Check(started, "the missing-image test server starts");
  if (!started) {
    return;
  }

  toy::shell::Page page;
  toy_test::Check(LoadUrl(server.Url("/page.html"), &page),
                  "a page with the same missing image twice still loads");
  // The failed URL is remembered: one request for it, not two.
  CheckEqual(server.connection_count(), 1, "the missing-image load stays on one connection");
  CheckEqual(server.request_count(), 2, "the duplicate missing image is requested once");
  const std::vector<std::string> paths = RequestPaths(server);
  toy_test::Check(paths == std::vector<std::string>{"/page.html", "/missing.png"},
                  "the broken image is attempted once and remembered for the second occurrence");
  toy_test::Check(page.images.empty(), "a failed image adds no decoded image entry");
  toy_test::Check(page.replaced_sizes.empty(), "a failed image reserves no replaced box");
  toy_test::Check(FindElements(page.document, "img").size() == 2,
                  "the two failed <img> elements are still in the DOM");
  // Both occurrences lay out as empty inlines: the trailing text starts at x=8.
  const std::vector<toy::dom::NodeId> texts = FindElements(page.document, "p");
  toy_test::Check(texts.size() == 1, "the fixture page has one <p>");
  if (texts.size() == 1) {
    CheckBox(page.boxes, texts[0], toy::layout::BoxKind::kBlock, 8, 24, 304, 16,
             "the failed images leave one 16 px text line");
  }
  server.Stop();
}

// No over-broad sharing: different image URLs are fetched and decoded separately
// even though the bytes are identical.
void TestDistinctImages(const std::string& png_bytes) {
  toy_test::HttpServer server;
  const std::string page_html =
      "<!doctype html><html><head><title>distinct images</title></head><body><p>"
      "<img src=\"/a.png\"><img src=\"/b.png\">"
      "</p></body></html>\n";
  const bool started = StartServer(&server, [&](std::string_view request) -> std::string {
    const std::string path = RequestPath(request);
    if (path == "/page.html") {
      return KeepAliveResponse(200, "text/html", page_html);
    }
    if (path == "/a.png" || path == "/b.png") {
      return KeepAliveResponse(200, "image/png", png_bytes);
    }
    return KeepAliveResponse(404, "text/plain", "");
  });
  toy_test::Check(started, "the distinct-images test server starts");
  if (!started) {
    return;
  }

  toy::shell::Page page;
  toy_test::Check(LoadUrl(server.Url("/page.html"), &page),
                  "a page with two different image URLs loads");
  CheckEqual(server.connection_count(), 1, "distinct images share the same keep-alive connection");
  CheckEqual(server.request_count(), 3, "two different image URLs make one request each");
  const std::vector<std::string> paths = RequestPaths(server);
  toy_test::Check(paths == std::vector<std::string>{"/page.html", "/a.png", "/b.png"},
                  "both image URLs are fetched in document order");
  const std::vector<toy::dom::NodeId> images = FindElements(page.document, "img");
  toy_test::Check(images.size() == 2, "the distinct-images page has two <img> elements");
  if (images.size() == 2) {
    const auto first = page.images.find(images[0]);
    const auto second = page.images.find(images[1]);
    toy_test::Check(first != page.images.end() && second != page.images.end(),
                    "both distinct images are decoded");
    if (first != page.images.end() && second != page.images.end()) {
      toy_test::Check(first->second != second->second,
                      "different image URLs do not share a decoded image object");
    }
    CheckBox(page.boxes, images[0], toy::layout::BoxKind::kInline, 8, 24, 16, 8,
             "the first distinct <img> has the frozen 16x8 box at x=8");
    CheckBox(page.boxes, images[1], toy::layout::BoxKind::kInline, 24, 24, 16, 8,
             "the second distinct <img> has the frozen 16x8 box at x=24");
  }
  server.Stop();
}

// A server that drops an idle keep-alive connection after its response: the shell
// load path still succeeds because the Client retries a failed pooled connection
// on a fresh one.
void TestForceClosedIdleConnection(const std::string& png_bytes) {
  toy_test::HttpServer server;
  const std::string page_html =
      "<!doctype html><html><head><title>force close</title></head><body><p>"
      "<img src=\"/flag.png\">"
      "</p></body></html>\n";
  const bool started = StartServer(&server, [&](std::string_view request) -> std::string {
    const std::string path = RequestPath(request);
    if (path == "/page.html") {
      return KeepAliveResponse(200, "text/html", page_html);
    }
    if (path == "/flag.png") {
      return KeepAliveResponse(200, "image/png", png_bytes);
    }
    return KeepAliveResponse(404, "text/plain", "");
  });
  toy_test::Check(started, "the force-close test server starts");
  if (!started) {
    return;
  }
  // Close the page connection without announcing it, so the next request hits a
  // dropped idle pooled connection and has to be retried on a fresh one.
  server.SetForceCloseAfterResponse(true);

  toy::shell::Page page;
  toy_test::Check(LoadUrl(server.Url("/page.html"), &page),
                  "a load tolerates a server that force-closes an idle connection");
  CheckEqual(server.connection_count(), 2,
             "the dropped idle connection is replaced by exactly one fresh connection");
  CheckEqual(server.request_count(), 2,
             "the page and the image are requested once each after the retry");
  const std::vector<std::string> paths = RequestPaths(server);
  toy_test::Check(paths == std::vector<std::string>{"/page.html", "/flag.png"},
                  "the page and the image are each requested once after the retry");
  const std::vector<toy::dom::NodeId> images = FindElements(page.document, "img");
  toy_test::Check(images.size() == 1, "the force-close page has one <img>");
  if (images.size() == 1) {
    const auto found = page.images.find(images[0]);
    toy_test::Check(found != page.images.end(),
                    "the image is decoded after the retried keep-alive fetch");
    CheckBox(page.boxes, images[0], toy::layout::BoxKind::kInline, 8, 24, 16, 8,
             "the retried image keeps its frozen 16x8 box at x=8");
  }
  server.Stop();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <images-fixture-directory>\n", argv[0]);
    return 1;
  }
  std::string png_bytes;
  if (!ReadFile(std::string(argv[1]) + "/flag_16x8.png", &png_bytes)) {
    return 1;
  }

  TestDuplicateImage(png_bytes);
  TestDuplicateMissingImage();
  TestDistinctImages(png_bytes);
  TestForceClosedIdleConnection(png_bytes);
  return toy_test::Finish("shell resource loading");
}
