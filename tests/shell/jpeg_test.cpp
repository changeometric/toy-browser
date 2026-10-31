// M17 JPEG end-to-end test: JPEG images load through the same shell page loader
// as PNGs and paint, using the content-based render::DecodeImage decoder.
//
// The fixture page references the committed images in tests/fixtures/images/
// (jpeg_rgb_16x8.jpg, jpeg_gray_16x8.jpg, jpeg_progressive_16x8.jpg, the PNG
// flag_16x8.png and the truncated jpeg_truncated_16x8.jpg) plus a missing
// missing.jpg. It is deterministic: file:// only, no network, display or
// installed font (the tests pin the empty FONTCONFIG_FILE config, so layout uses
// the built-in bitmap font path).
//
// JPEG is lossy, so decoded colors are asserted with the documented tolerance of
// 8 per channel (the fixtures are generated in tests/fixtures/images with quality
// 100 and no chroma subsampling); the PNG pixel is asserted exactly. The fixture
// README documents every expected box and pixel.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "page.h"
#include "support/check.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/net/url.h"
#include "toy/render/surface.h"
#include "toy/shell/browser.h"
#include "toy/style/computed_style.h"

namespace {

constexpr int kViewportWidth = 320;
constexpr int kViewportHeight = 400;
// JPEG fixture tolerance per channel (quality 100, no chroma subsampling).
constexpr int kJpegTolerance = 8;

std::string FileUrl(const std::string& directory, const std::string& name) {
  return "file://" + directory + "/" + name;
}

std::optional<toy::net::Url> ParseUrl(const std::string& url_text) {
  std::string error;
  std::optional<toy::net::Url> url = toy::net::ParseUrl(url_text, &error);
  if (!url) {
    std::fprintf(stderr, "  cannot parse URL '%s': %s\n", url_text.c_str(), error.c_str());
  }
  return url;
}

bool LoadFixtureSite(const std::string& directory, toy::shell::Page* page) {
  const std::optional<toy::net::Url> url = ParseUrl(FileUrl(directory, "index.html"));
  if (!url) {
    return false;
  }
  std::string error;
  if (!toy::shell::LoadPage(*url, kViewportWidth, nullptr, page, &error)) {
    std::fprintf(stderr, "  cannot load the jpeg_site fixture: %s\n", error.c_str());
    return false;
  }
  return true;
}

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

// All <img> element node ids, in document order.
std::vector<toy::dom::NodeId> FindImages(const toy::dom::Document& document) {
  std::vector<toy::dom::NodeId> ids;
  std::vector<toy::dom::NodeId> pending;
  pending.push_back(document.root());
  while (!pending.empty()) {
    const toy::dom::NodeId node_id = pending.back();
    pending.pop_back();
    const toy::dom::Node& node = document.Get(node_id);
    if (node.type == toy::dom::NodeType::kElement && node.name == "img") {
      ids.push_back(node_id);
    }
    for (toy::dom::NodeId child = node.last_child; child != toy::dom::kInvalidNode;
         child = document.Get(child).prev_sibling) {
      pending.push_back(child);
    }
  }
  return ids;
}

std::string ImageSource(const toy::dom::Document& document, toy::dom::NodeId id) {
  for (const toy::dom::Attribute& attribute : document.Get(id).attributes) {
    if (attribute.name == "src") {
      return attribute.value;
    }
  }
  return {};
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

void CheckImageBox(const toy::shell::Page& page, toy::dom::NodeId image, int x, int y, int width,
                   int height, const char* description) {
  const toy::layout::Box* box = FindBoxForNode(page.boxes, image, toy::layout::BoxKind::kInline);
  if (box == nullptr) {
    std::fprintf(stderr, "  %s: no inline image box\n", description);
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

void CheckElementBox(const toy::shell::Page& page, std::string_view id, int x, int y, int width,
                     int height, const char* description) {
  const toy::dom::NodeId node = FindElementById(page.document, id);
  const std::string prefix = std::string(description) + " #" + std::string(id) + " box";
  if (node == toy::dom::kInvalidNode) {
    std::fprintf(stderr, "  %s: missing element\n", prefix.c_str());
    toy_test::Check(false, "the fixture element exists");
    return;
  }
  const toy::layout::Box* box = FindBoxForNode(page.boxes, node, toy::layout::BoxKind::kBlock);
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
  toy_test::Check(matches, (prefix + ": frozen box geometry").c_str());
}

// True when every ARGB channel of `actual` is within `tolerance` of `expected`
// (alpha included: decoded images are opaque).
bool ColorClose(std::uint32_t actual, std::uint32_t expected, int tolerance) {
  for (int shift = 0; shift <= 24; shift += 8) {
    const int a = static_cast<int>((actual >> shift) & 0xFFu);
    const int e = static_cast<int>((expected >> shift) & 0xFFu);
    if (std::abs(a - e) > tolerance) {
      return false;
    }
  }
  return true;
}

void CheckPixelClose(const toy::render::Surface& surface, int x, int y, std::uint32_t expected,
                     const char* description) {
  const std::uint32_t actual = surface.At(x, y);
  if (!ColorClose(actual, expected, kJpegTolerance)) {
    std::fprintf(stderr, "  %s: pixel (%d,%d)=0x%08x, expected 0x%08x +/- %d\n", description, x, y,
                 actual, expected, kJpegTolerance);
  }
  toy_test::Check(ColorClose(actual, expected, kJpegTolerance), description);
}

void CheckPixelExact(const toy::render::Surface& surface, int x, int y, std::uint32_t expected,
                     const char* description) {
  if (surface.At(x, y) != expected) {
    std::fprintf(stderr, "  %s: pixel (%d,%d)=0x%08x, expected 0x%08x\n", description, x, y,
                 surface.At(x, y), expected);
  }
  toy_test::Check(surface.At(x, y) == expected, description);
}

std::uint32_t CountNonWhiteInRect(const toy::render::Surface& surface, int x0, int y0, int x1,
                                  int y1) {
  std::uint32_t count = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      if (surface.At(x, y) != 0xFFFFFFFFu) {
        ++count;
      }
    }
  }
  return count;
}

std::uint32_t CountColorInRect(const toy::render::Surface& surface, std::uint32_t color, int x0,
                               int y0, int x1, int y1) {
  std::uint32_t count = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      if (surface.At(x, y) == color) {
        ++count;
      }
    }
  }
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <jpeg_site-directory>\n", argv[0]);
    return 1;
  }
  const std::string site = argv[1];

  toy::shell::Page page;
  toy_test::Check(LoadFixtureSite(site, &page),
                  "the jpeg_site fixture page loads through the shell page loader");
  toy_test::Check(page.images.size() == 5,
                  "five decodable <img> occurrences have decoded images");

  const std::vector<toy::dom::NodeId> images = FindImages(page.document);
  toy_test::Check(images.size() == 7, "the fixture page has seven <img> elements");
  if (images.size() != 7) {
    return toy_test::Finish("shell jpeg");
  }
  // Document order: rgb jpg, gray jpg, progressive jpg, PNG flag, rgb jpg
  // (mixed row), missing.jpg, truncated jpg.
  const std::vector<std::string> sources = [&] {
    std::vector<std::string> result;
    for (const toy::dom::NodeId id : images) {
      result.push_back(ImageSource(page.document, id));
    }
    return result;
  }();
  const auto ends_with = [](const std::string& text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  toy_test::Check(ends_with(sources[0], "jpeg_rgb_16x8.jpg"), "image 0 is the RGB JPEG");
  toy_test::Check(ends_with(sources[1], "jpeg_gray_16x8.jpg"), "image 1 is the gray JPEG");
  toy_test::Check(ends_with(sources[2], "jpeg_progressive_16x8.jpg"),
                  "image 2 is the progressive JPEG");
  toy_test::Check(ends_with(sources[3], "flag_16x8.png"), "image 3 is a PNG in a mixed page");
  toy_test::Check(ends_with(sources[4], "jpeg_rgb_16x8.jpg"),
                  "image 4 repeats the RGB JPEG in the mixed row");
  toy_test::Check(ends_with(sources[5], "missing.jpg"), "image 5 is the missing JPEG");
  toy_test::Check(ends_with(sources[6], "jpeg_truncated_16x8.jpg"),
                  "image 6 is the truncated JPEG");

  // The four decodable URLs (the mixed row shares the RGB URL) produce image map
  // and replaced-size entries with the intrinsic decoded 16x8 size.
  const std::vector<toy::dom::NodeId> decodable = {images[0], images[1], images[2], images[3],
                                                   images[4]};
  toy_test::Check(page.replaced_sizes.size() == 5,
                  "five <img> occurrences reserve a replaced size");
  for (std::size_t index = 0; index < decodable.size(); ++index) {
    const auto size = page.replaced_sizes.find(decodable[index]);
    toy_test::Check(size != page.replaced_sizes.end(),
                    "a decodable image has a replaced size");
    if (size != page.replaced_sizes.end()) {
      toy_test::Check(std::fabs(size->second.width_px - 16.0f) < 0.01f &&
                          std::fabs(size->second.height_px - 8.0f) < 0.01f,
                      "decoded JPEG/PNG images report their intrinsic 16x8 size");
    }
    toy_test::Check(page.images.find(decodable[index]) != page.images.end(),
                    "a decodable image has an ImageMap entry");
  }
  // The two occurrences of the RGB JPEG URL share one decoded object (M13 cache).
  const auto rgb_image = page.images.find(images[0]);
  const auto mixed_jpeg_image = page.images.find(images[4]);
  toy_test::Check(rgb_image != page.images.end() && mixed_jpeg_image != page.images.end() &&
                      rgb_image->second == mixed_jpeg_image->second,
                  "the repeated RGB JPEG URL shares one decoded image object");

  // Broken JPEGs are skipped silently: no map entries, no layout reservation.
  toy_test::Check(page.images.find(images[5]) == page.images.end(),
                  "the missing JPEG has no decoded image");
  toy_test::Check(page.replaced_sizes.find(images[5]) == page.replaced_sizes.end(),
                  "the missing JPEG reserves no replaced size");
  toy_test::Check(page.images.find(images[6]) == page.images.end(),
                  "the truncated JPEG is skipped, not fatal");
  toy_test::Check(page.replaced_sizes.find(images[6]) == page.replaced_sizes.end(),
                  "the truncated JPEG reserves no replaced size");

  // Frozen layout: JPEG intrinsic sizes drive the replaced boxes exactly like PNG,
  // and the CSS-scaled progressive image keeps the intrinsic 2:1 ratio.
  CheckImageBox(page, images[0], 8, 24, 16, 8, "the RGB JPEG box is 16x8 at (8,24)");
  CheckImageBox(page, images[1], 8, 72, 16, 8, "the gray JPEG box is 16x8 at (8,72)");
  CheckImageBox(page, images[2], 8, 120, 32, 16,
                "the progressive JPEG box is CSS-scaled to 32x16 at (8,120)");
  CheckImageBox(page, images[3], 8, 168, 16, 8, "the mixed-row PNG box is 16x8 at (8,168)");
  CheckImageBox(page, images[4], 24, 168, 16, 8, "the mixed-row JPEG box is 16x8 at (24,168)");
  CheckElementBox(page, "rgb", 8, 24, 304, 16, "the RGB row keeps its 16 px line");
  CheckElementBox(page, "gray", 8, 72, 304, 16, "the gray row keeps its 16 px line");
  CheckElementBox(page, "progressive", 8, 120, 304, 16,
                  "the progressive row keeps its 16 px line");
  CheckElementBox(page, "mixed", 8, 168, 304, 16, "the mixed row keeps its 16 px line");
  CheckElementBox(page, "missing", 8, 216, 304, 16,
                  "the missing-JPEG row lays out as text only");
  CheckElementBox(page, "truncated", 8, 264, 304, 16,
                  "the truncated-JPEG row lays out as text only");
  toy_test::Check(std::fabs(toy::layout::ContentHeight(page.boxes) - 304.0f) < 0.01f,
                  "the fixture page content height is 304 CSS px");

  // Browser pixels: JPEG quadrants within the documented tolerance, the PNG exact.
  toy::shell::Browser browser(kViewportWidth, kViewportHeight);
  std::string error;
  toy_test::Check(browser.Open(FileUrl(site, "index.html"), &error),
                  "the jpeg_site page loads in a Browser session");
  toy::render::Surface surface(kViewportWidth, kViewportHeight);
  browser.Paint(surface);

  CheckPixelClose(surface, 10, 25, 0xFFFF0000u, "the RGB JPEG's red quadrant is painted");
  CheckPixelClose(surface, 20, 25, 0xFF00FF00u, "the RGB JPEG's green quadrant is painted");
  CheckPixelClose(surface, 10, 29, 0xFF0000FFu, "the RGB JPEG's blue quadrant is painted");
  CheckPixelClose(surface, 20, 29, 0xFFFFFFFFu, "the RGB JPEG's white quadrant is painted");

  CheckPixelClose(surface, 10, 73, 0xFF000000u, "the gray JPEG's black quadrant is painted");
  CheckPixelClose(surface, 20, 73, 0xFF404040u, "the gray JPEG's 64 quadrant is painted");
  CheckPixelClose(surface, 10, 77, 0xFF808080u, "the gray JPEG's 128 quadrant is painted");
  CheckPixelClose(surface, 20, 77, 0xFFFFFFFFu, "the gray JPEG's white quadrant is painted");

  CheckPixelClose(surface, 10, 121, 0xFFFF0000u, "the progressive JPEG red quadrant is painted");
  CheckPixelClose(surface, 30, 121, 0xFF00FF00u, "the progressive JPEG green quadrant is painted");
  CheckPixelClose(surface, 10, 131, 0xFF0000FFu, "the progressive JPEG blue quadrant is painted");
  CheckPixelClose(surface, 30, 131, 0xFFFFFFFFu, "the progressive JPEG white quadrant is painted");

  CheckPixelExact(surface, 10, 169, 0xFFFF0000u, "the mixed-row PNG red half is exact");
  CheckPixelExact(surface, 20, 169, 0xFF0000FFu, "the mixed-row PNG blue half is exact");
  CheckPixelClose(surface, 26, 169, 0xFFFF0000u, "the mixed-row JPEG red quadrant is painted");
  CheckPixelClose(surface, 36, 169, 0xFF00FF00u, "the mixed-row JPEG green quadrant is painted");
  CheckPixelClose(surface, 26, 173, 0xFF0000FFu, "the mixed-row JPEG blue quadrant is painted");
  CheckPixelClose(surface, 36, 173, 0xFFFFFFFFu, "the mixed-row JPEG white quadrant is painted");

  // Skipped rows paint no image pixels at all: every non-white pixel in the row
  // is black text ink (a decoded image would add red/green/blue/gray pixels).
  const std::uint32_t missing_non_white = CountNonWhiteInRect(surface, 8, 216, 304, 232);
  toy_test::Check(missing_non_white == CountColorInRect(surface, 0xFF000000u, 8, 216, 304, 232),
                  "the missing JPEG paints no image pixels");
  toy_test::Check(missing_non_white > 0, "the missing-JPEG row still paints its text");
  const std::uint32_t truncated_non_white = CountNonWhiteInRect(surface, 8, 264, 304, 280);
  toy_test::Check(
      truncated_non_white == CountColorInRect(surface, 0xFF000000u, 8, 264, 304, 280),
      "the truncated JPEG paints no image pixels");
  toy_test::Check(truncated_non_white > 0, "the truncated-JPEG row still paints its text");

  return toy_test::Finish("shell jpeg");
}
