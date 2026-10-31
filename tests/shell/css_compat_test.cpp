// M14a end-to-end CSS-compatibility test: the committed css_compat_site fixture
// site is loaded through the shell-private page loader and laid out with the
// deterministic built-in bitmap font path (empty FONTCONFIG_FILE test config), so
// the computed values, the frozen box geometry and the painted pixels are all
// machine-independent.
//
// It covers the M14a batch end to end: %/rem lengths, rgb()/rgba()/hsl() colors,
// the background shorthand, logical properties, min/max-width, auto margins,
// line-height, text-align, text-decoration: underline and visibility
// (hidden + visible child). The style module owns the computed values and layout
// owns the geometry; this test asserts both through the real pipeline and then
// checks that the painted pixels show the visible effects.

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
// Tall enough to paint the whole fixture page without scrolling.
constexpr int kViewportHeight = 400;

using toy::style::AutoLength;
using toy::style::ComputedStyle;
using toy::style::Length;

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

// Loads one fixture page through the shell-private styled-document/page loader
// with the deterministic built-in bitmap font metrics.
bool LoadPageFile(const std::string& directory, const std::string& name, toy::shell::Page* page) {
  const std::optional<toy::net::Url> url = ParseUrl(FileUrl(directory, name));
  if (!url) {
    return false;
  }
  std::string error;
  if (!toy::shell::LoadPage(*url, kViewportWidth, nullptr, page, &error)) {
    std::fprintf(stderr, "  cannot load '%s': %s\n", name.c_str(), error.c_str());
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

const ComputedStyle* StyleOf(const toy::shell::Page& page, std::string_view id,
                             const char* description) {
  const std::string prefix = std::string(description) + " #" + std::string(id);
  const toy::dom::NodeId node = FindElementById(page.document, id);
  if (node == toy::dom::kInvalidNode) {
    std::fprintf(stderr, "  %s: missing fixture element\n", prefix.c_str());
    toy_test::Check(false, "the fixture element exists");
    return nullptr;
  }
  return &page.styles.Get(node);
}

void CheckNearlyEqual(float actual, float expected, const std::string& description) {
  if (std::fabs(actual - expected) > 0.01f) {
    std::fprintf(stderr, "  %s: %.4f, expected %.4f\n", description.c_str(),
                 static_cast<double>(actual), static_cast<double>(expected));
  }
  toy_test::Check(std::fabs(actual - expected) <= 0.01f, description.c_str());
}

void CheckLength(const Length& length, float value, Length::Unit unit,
                 const std::string& description) {
  CheckNearlyEqual(length.value, value, description + " value");
  if (length.unit != unit) {
    std::fprintf(stderr, "  %s: wrong length unit\n", description.c_str());
  }
  toy_test::Check(length.unit == unit, (description + " unit").c_str());
}

void CheckPixelLength(const Length& length, float px, const std::string& description) {
  CheckLength(length, px, Length::Unit::kPx, description);
}

void CheckPercentLength(const Length& length, float percent, const std::string& description) {
  CheckLength(length, percent, Length::Unit::kPercent, description);
}

void CheckMargin(const AutoLength& margin, float value, const std::string& description) {
  if (margin.is_auto) {
    std::fprintf(stderr, "  %s: unexpected auto\n", description.c_str());
  }
  toy_test::Check(!margin.is_auto, (description + " is not auto").c_str());
  CheckPixelLength(margin.length, value, description);
}

void CheckMarginAuto(const AutoLength& margin, const std::string& description) {
  toy_test::Check(margin.is_auto, (description + " is auto").c_str());
}

// First box of `kind` for a DOM node, or nullptr.
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

void CheckElementBox(const toy::shell::Page& page, std::string_view id, int x, int y, int width,
                     int height, const char* description) {
  const std::string prefix = std::string(description) + " #" + std::string(id) + " box";
  const toy::dom::NodeId node = FindElementById(page.document, id);
  const toy::layout::Box* box =
      node == toy::dom::kInvalidNode ? nullptr : FindBoxForNode(page.boxes, node,
                                                                toy::layout::BoxKind::kBlock);
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

void CheckPixel(const toy::render::Surface& surface, int x, int y, std::uint32_t expected,
                const char* description) {
  if (surface.At(x, y) != expected) {
    std::fprintf(stderr, "  %s: pixel (%d,%d)=0x%08x, expected 0x%08x\n", description, x, y,
                 surface.At(x, y), expected);
  }
  toy_test::Check(surface.At(x, y) == expected, description);
}

// Paints one fixture page through a Browser session (bitmap font path) at the
// frozen test viewport.
toy::render::Surface PaintPage(const std::string& directory, const std::string& name,
                               const char* description) {
  toy::render::Surface surface(kViewportWidth, kViewportHeight);
  toy::shell::Browser browser(kViewportWidth, kViewportHeight);
  std::string error;
  if (!browser.Open(FileUrl(directory, name), &error)) {
    std::fprintf(stderr, "  %s: cannot open '%s': %s\n", description, name.c_str(),
                 error.c_str());
    toy_test::Check(false, "the fixture page paints through Browser");
  }
  browser.Paint(surface);
  return surface;
}

// --- Computed values --------------------------------------------------------

void TestValuesComputed(const toy::shell::Page& page) {
  // Color functions: rgb() text, rgba() alpha background, hsl() background and
  // the background shorthand's color part.
  if (const ComputedStyle* style = StyleOf(page, "rgb", "rgb() text color")) {
    toy_test::Check(style->color == 0xFF0000u, "rgb(255, 0, 0) computes to #ff0000");
  }
  if (const ComputedStyle* style = StyleOf(page, "rgba", "rgba() background")) {
    toy_test::Check(style->background_color == 0x800000FFu,
                    "rgba(0, 0, 255, 0.5) keeps a 128 alpha background");
  }
  if (const ComputedStyle* style = StyleOf(page, "hsl", "hsl() background")) {
    toy_test::Check(style->background_color == 0xFF008000u,
                    "hsl(120, 100%, 25%) computes to opaque #008000");
  }
  if (const ComputedStyle* style = StyleOf(page, "shorthand", "background shorthand")) {
    toy_test::Check(style->background_color == 0xFF123456u,
                    "the background shorthand applies its color");
  }

  // Percentages stay percentages in the computed style; layout resolves them.
  if (const ComputedStyle* style = StyleOf(page, "percent", "percentage lengths")) {
    toy_test::Check(style->width.has_value(), "the percentage box has a width");
    if (style->width) {
      CheckPercentLength(*style->width, 50.0f, "width: 50%");
    }
    CheckPercentLength(style->margin_left.length, 10.0f, "margin-left: 10%");
  }

  // rem resolves against the root (initial 16 px) font size: 1.5rem = 24 px.
  if (const ComputedStyle* style = StyleOf(page, "rem", "rem lengths")) {
    CheckPixelLength(style->padding_top, 24.0f, "padding: 1.5rem top");
    CheckPixelLength(style->padding_left, 24.0f, "padding: 1.5rem left");
    CheckMargin(style->margin_top, 8.0f, "margin-top: 0.5rem");
  }

  // Logical properties map to the physical sides (horizontal writing mode).
  if (const ComputedStyle* style = StyleOf(page, "logical", "logical properties")) {
    CheckMargin(style->margin_left, 10.0f, "margin-inline start");
    CheckMargin(style->margin_right, 20.0f, "margin-inline end");
    CheckPixelLength(style->padding_top, 4.0f, "padding-block start");
    CheckPixelLength(style->padding_bottom, 6.0f, "padding-block end");
    toy_test::Check(style->width.has_value(), "inline-size computes a width");
    if (style->width) {
      CheckPixelLength(*style->width, 120.0f, "inline-size: 120px");
    }
  }

  // inline-size + max-inline-size, then width + min/max-width.
  if (const ComputedStyle* style = StyleOf(page, "maxlogical", "max-inline-size")) {
    toy_test::Check(style->max_width.has_value(), "max-inline-size computes max-width");
    if (style->max_width) {
      CheckPixelLength(*style->max_width, 200.0f, "max-inline-size: 200px");
    }
  }
  if (const ComputedStyle* style = StyleOf(page, "minmax", "min/max-width")) {
    toy_test::Check(style->min_width.has_value(), "min-width is stored");
    if (style->min_width) {
      CheckPixelLength(*style->min_width, 150.0f, "min-width: 150px");
    }
    toy_test::Check(style->max_width.has_value(), "max-width is stored");
    if (style->max_width) {
      CheckPixelLength(*style->max_width, 200.0f, "max-width: 200px");
    }
  }
}

void TestTypographyComputed(const toy::shell::Page& page) {
  using toy::style::LineHeight;
  using toy::style::TextAlign;
  using toy::style::Visibility;

  if (const ComputedStyle* style = StyleOf(page, "lh-number", "unitless line-height")) {
    toy_test::Check(style->line_height.kind == LineHeight::Kind::kNumber,
                    "line-height: 1.5 computes a number");
    CheckNearlyEqual(style->line_height.value, 1.5f, "line-height: 1.5 keeps its multiplier");
  }
  if (const ComputedStyle* style = StyleOf(page, "lh-px", "px line-height")) {
    toy_test::Check(style->line_height.kind == LineHeight::Kind::kPx,
                    "line-height: 24px computes pixels");
    CheckNearlyEqual(style->line_height.value, 24.0f, "line-height: 24px keeps its value");
  }
  if (const ComputedStyle* style = StyleOf(page, "lh-normal", "normal line-height")) {
    toy_test::Check(style->line_height.kind == LineHeight::Kind::kNormal,
                    "line-height: normal stays normal");
  }
  if (const ComputedStyle* style = StyleOf(page, "center", "text-align center")) {
    toy_test::Check(style->text_align == TextAlign::kCenter, "text-align: center computes center");
  }
  if (const ComputedStyle* style = StyleOf(page, "right", "text-align right")) {
    toy_test::Check(style->text_align == TextAlign::kRight, "text-align: right computes right");
  }
  if (const ComputedStyle* style = StyleOf(page, "underline", "text-decoration underline")) {
    toy_test::Check(style->text_decoration_underline,
                    "text-decoration: underline computes the flag");
  }
  // visibility: hidden on a block, visible again on its child.
  if (const ComputedStyle* style = StyleOf(page, "hidden", "visibility hidden")) {
    toy_test::Check(style->visibility == Visibility::kHidden,
                    "visibility: hidden computes hidden");
  }
  if (const ComputedStyle* style = StyleOf(page, "hidden-child", "visible child")) {
    toy_test::Check(style->visibility == Visibility::kVisible,
                    "a visible child overrides the inherited hidden visibility");
  }
  if (const ComputedStyle* style = StyleOf(page, "outside", "outside text")) {
    toy_test::Check(style->visibility == Visibility::kVisible,
                    "a sibling outside the hidden block stays visible");
  }
}

void TestBoxesComputed(const toy::shell::Page& page) {
  // margin: 0 auto computes auto horizontal margins (layout centers the block).
  if (const ComputedStyle* style = StyleOf(page, "auto-center", "auto margins")) {
    CheckMarginAuto(style->margin_left, "margin-left auto");
    CheckMarginAuto(style->margin_right, "margin-right auto");
    CheckMargin(style->margin_top, 0.0f, "margin-top 0");
    toy_test::Check(style->width.has_value(), "the centered block has a width");
    if (style->width) {
      CheckPixelLength(*style->width, 120.0f, "width: 120px");
    }
  }
  if (const ComputedStyle* style = StyleOf(page, "pct-width", "percentage width")) {
    toy_test::Check(style->width.has_value(), "the percentage-width block has a width");
    if (style->width) {
      CheckPercentLength(*style->width, 25.0f, "width: 25%");
    }
  }
  if (const ComputedStyle* style = StyleOf(page, "min-width", "min-width")) {
    toy_test::Check(style->min_width.has_value(), "min-width is stored");
    if (style->min_width) {
      CheckPixelLength(*style->min_width, 150.0f, "min-width: 150px");
    }
  }
  if (const ComputedStyle* style = StyleOf(page, "max-width", "max-width")) {
    toy_test::Check(style->max_width.has_value(), "max-width is stored");
    if (style->max_width) {
      CheckPixelLength(*style->max_width, 200.0f, "max-width: 200px");
    }
  }
}


// Non-transparent counts inside a half-open rectangle, for text/glyph ink checks.
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

// First text box with exactly `text`, in box-tree pre-order, or nullptr.
const toy::layout::Box* FindTextBox(const toy::shell::Page& page, std::string_view text) {
  if (page.boxes.root() == toy::layout::kInvalidBox) {
    return nullptr;
  }
  std::vector<toy::layout::BoxId> pending;
  pending.push_back(page.boxes.root());
  while (!pending.empty()) {
    const toy::layout::BoxId box_id = pending.back();
    pending.pop_back();
    const toy::layout::Box& box = page.boxes.Get(box_id);
    if (box.kind == toy::layout::BoxKind::kText && box.text == text) {
      return &box;
    }
    std::vector<toy::layout::BoxId> children;
    for (toy::layout::BoxId child = box.first_child; child != toy::layout::kInvalidBox;
         child = page.boxes.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(children[index]);
    }
  }
  return nullptr;
}

void CheckTextBox(const toy::shell::Page& page, std::string_view text, int x, int y, int width,
                  int height, const char* description) {
  const toy::layout::Box* box = FindTextBox(page, text);
  if (box == nullptr) {
    std::fprintf(stderr, "  %s: text box \"%s\" not found\n", description,
                 std::string(text).c_str());
    toy_test::Check(false, description);
    return;
  }
  const bool matches = RoundPx(box->x) == x && RoundPx(box->y) == y &&
                       RoundPx(box->width) == width && RoundPx(box->height) == height;
  if (!matches) {
    std::fprintf(stderr, "  %s: text \"%s\" x=%d y=%d width=%d height=%d, expected %d/%d/%d/%d\n",
                 description, std::string(text).c_str(), RoundPx(box->x), RoundPx(box->y),
                 RoundPx(box->width), RoundPx(box->height), x, y, width, height);
  }
  toy_test::Check(matches, description);
}

// --- values.html: frozen geometry and painted pixels ------------------------

void TestValuesGeometryAndPixels(const std::string& site, const toy::shell::Page& page) {
  // 50%/10% resolve against the 304 px body content width (152 px and 30.4 px).
  CheckElementBox(page, "percent", 38, 182, 152, 20,
                  "percentage width/margin resolve against the containing block");
  // 1.5rem padding = 24 px (root font size 16 px) and 0.5rem top margin = 8 px.
  CheckElementBox(page, "rem", 8, 210, 304, 64, "rem lengths resolve to 24 px padding");
  // margin-inline 10/20, padding-block 4/6, inline-size 120.
  CheckElementBox(page, "logical", 18, 274, 120, 26,
                  "logical properties map to the physical margin/padding/size");
  // max-inline-size caps a 300 px inline-size at 200 px.
  CheckElementBox(page, "maxlogical", 8, 300, 200, 10, "max-inline-size caps the box width");
  // min-width 150 wins over the 50 px width.
  CheckElementBox(page, "minmax", 8, 310, 150, 10, "min-width overrides a smaller width");
  CheckNearlyEqual(toy::layout::ContentHeight(page.boxes), 328.0f,
                   "the values page content height is 328 CSS px");

  const toy::render::Surface surface = PaintPage(site, "values.html", "values.html paint");
  // rgba(0, 0, 255, 0.5) over the white canvas: SrcOver gives (127, 127, 255).
  CheckPixel(surface, 200, 74, 0xFF7F7FFF,
             "an rgba() background is alpha-blended over the canvas");
  CheckPixel(surface, 200, 116, 0xFF008000u, "an hsl() background is painted opaque");
  CheckPixel(surface, 200, 158, 0xFF123456u, "the background shorthand color is painted");
  CheckPixel(surface, 100, 190, 0xFFEEEEEEu, "the 50% width box paints its background");
  CheckPixel(surface, 20, 190, 0xFFFFFFFFu, "the 10% margin area stays white");
  CheckPixel(surface, 300, 250, 0xFFDDDDDDu, "the rem padding box paints its background");
  CheckPixel(surface, 100, 280, 0xFFCCDDCCu, "the logical box paints its background");
  CheckPixel(surface, 10, 280, 0xFFFFFFFFu, "the logical margin-inline area stays white");
  CheckPixel(surface, 150, 304, 0xFFCCCCDDu, "the max-inline-size box paints its background");
  CheckPixel(surface, 140, 314, 0xFFDDCCDDu, "the min-width box paints its background");
  toy_test::Check(CountColorInRect(surface, 0xFFFF0000u, 8, 24, 120, 40) > 0,
                  "rgb() text is painted in its computed color");
}

// --- typography.html: alignment, line-height, underline, visibility ---------

void TestTypographyGeometryAndPixels(const std::string& site, const toy::shell::Page& page) {
  CheckElementBox(page, "lh-number", 8, 24, 304, 30,
                  "a unitless line-height 1.5 at 20 px font size gives a 30 px line");
  CheckElementBox(page, "lh-px", 8, 86, 304, 24, "a 24 px line-height gives a 24 px line");
  CheckElementBox(page, "lh-normal", 8, 142, 304, 16,
                  "line-height normal keeps the font's own 16 px line metrics");
  CheckElementBox(page, "center", 8, 190, 304, 16, "the centered block keeps its box");
  CheckElementBox(page, "right", 8, 238, 304, 16, "the right-aligned block keeps its box");
  CheckElementBox(page, "underline", 8, 286, 304, 16, "the underlined block keeps its box");
  CheckElementBox(page, "hidden", 8, 318, 304, 16,
                  "a visibility:hidden block keeps its layout space");
  CheckElementBox(page, "outside", 8, 350, 304, 16,
                  "the next block follows the hidden block's kept space");
  // text-align moves only the line boxes: center = 8 + (304 - 64) / 2, right = 8 + 304 - 40.
  CheckTextBox(page, "Centered", 128, 190, 64, 16, "text-align: center centers the line");
  CheckTextBox(page, "Right", 272, 238, 40, 16, "text-align: right aligns the line to the right");
  CheckNearlyEqual(toy::layout::ContentHeight(page.boxes), 390.0f,
                   "the typography page content height is 390 CSS px");

  const toy::render::Surface surface =
      PaintPage(site, "typography.html", "typography.html paint");
  // The centered line has ink only from x=128 on; the right-aligned line only near x=272.
  toy_test::Check(CountNonWhiteInRect(surface, 120, 190, 200, 206) > 0,
                  "the centered line paints inside the centered region");
  toy_test::Check(CountNonWhiteInRect(surface, 8, 190, 120, 206) == 0,
                  "the centered line leaves the left part of the block empty");
  toy_test::Check(CountNonWhiteInRect(surface, 250, 238, 312, 254) > 0,
                  "the right-aligned line paints in the right region");
  toy_test::Check(CountNonWhiteInRect(surface, 8, 238, 250, 254) == 0,
                  "the right-aligned line leaves the left part of the block empty");
  // The underline is exactly one black row at round(box.y) + round(baseline) + 1 =
  // 286 + 16 + 1 = 303, spanning the 80 px text box width in the text color.
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 303, 88, 304) == 80,
                  "the underline is one 80 px row under the underlined text");
  CheckPixel(surface, 8, 303, 0xFF000000u, "the underline starts at the text box x");
  CheckPixel(surface, 88, 303, 0xFFFFFFFFu, "the underline ends with the text box width");
  // visibility:hidden: the block background is not painted, the visible child is.
  toy_test::Check(CountColor(surface, 0xFF123456u) == 0,
                  "the hidden block's background is not painted anywhere");
  toy_test::Check(CountColor(surface, 0xFFABCDEFu) > 0,
                  "the visible child inside the hidden block paints its background");
  CheckPixel(surface, 100, 320, 0xFFABCDEFu,
             "a pixel inside the visible child shows its background");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 318, 112, 334) > 0,
                  "the visible child's text is painted inside the hidden block");
  toy_test::Check(CountNonWhiteInRect(surface, 8, 350, 120, 366) > 0,
                  "the text outside the hidden block is painted");
}

// --- boxes.html: auto margins, constraints, percentage height, replaced box -

void TestBoxesGeometryAndPixels(const std::string& site, const toy::shell::Page& page) {
  // margin: 0 auto centers the 120 px block in the 304 px content width.
  CheckElementBox(page, "auto-center", 100, 8, 120, 12,
                  "auto horizontal margins center the block");
  CheckElementBox(page, "pct-width", 8, 20, 76, 12, "a 25% width resolves to 76 px");
  CheckElementBox(page, "min-width", 8, 32, 150, 12, "min-width overrides the 50 px width");
  CheckElementBox(page, "max-width", 8, 44, 200, 12, "max-width caps the 500 px width");
  CheckElementBox(page, "height-pct", 8, 56, 304, 16,
                  "a percentage height is treated as auto by layout");
  // The replaced box: CSS width 32 px keeps the intrinsic 2:1 ratio (32x16).
  const toy::dom::NodeId image = FindElementById(page.document, "replaced-image");
  toy_test::Check(image != toy::dom::kInvalidNode, "the replaced image element exists");
  if (image != toy::dom::kInvalidNode) {
    const toy::layout::Box* box =
        FindBoxForNode(page.boxes, image, toy::layout::BoxKind::kInline);
    if (box == nullptr) {
      toy_test::Check(false, "the replaced image has an inline box");
    } else {
      const bool matches = RoundPx(box->x) == 8 && RoundPx(box->y) == 88 &&
                           RoundPx(box->width) == 32 && RoundPx(box->height) == 16;
      if (!matches) {
        std::fprintf(stderr, "  replaced image box: x=%d y=%d width=%d height=%d\n",
                     RoundPx(box->x), RoundPx(box->y), RoundPx(box->width),
                     RoundPx(box->height));
      }
      toy_test::Check(matches, "the replaced image keeps its intrinsic ratio at width: 32px");
    }
  }
  CheckNearlyEqual(toy::layout::ContentHeight(page.boxes), 128.0f,
                   "the boxes page content height is 128 CSS px");

  const toy::render::Surface surface = PaintPage(site, "boxes.html", "boxes.html paint");
  CheckPixel(surface, 150, 10, 0xFFE0E0E0u, "the auto-margin block paints at its centered x");
  CheckPixel(surface, 50, 10, 0xFFFFFFFFu, "the area left of the centered block stays white");
  CheckPixel(surface, 50, 25, 0xFFD0D0D0u, "the percentage-width box paints its background");
  CheckPixel(surface, 90, 25, 0xFFFFFFFFu, "the percentage-width box does not overflow");
  CheckPixel(surface, 150, 35, 0xFFC0C0C0u, "the min-width box paints its 150 px background");
  CheckPixel(surface, 200, 50, 0xFFB0B0B0u, "the max-width box paints its 200 px background");
  CheckPixel(surface, 210, 50, 0xFFFFFFFFu, "the max-width box does not overflow");
  CheckPixel(surface, 200, 60, 0xFFA0A0A0u,
             "the percentage-height box uses its content height and paints");
  // The fixture image is a 16x8 flag scaled to 32x16: red columns 8-23, blue 24-39.
  CheckPixel(surface, 8, 88, 0xFFFF0000u, "the replaced image's red half is painted");
  CheckPixel(surface, 23, 88, 0xFFFF0000u, "the replaced image's red half ends at x=23");
  CheckPixel(surface, 24, 88, 0xFF0000FFu, "the replaced image's blue half starts at x=24");
  CheckPixel(surface, 39, 88, 0xFF0000FFu, "the replaced image's blue half ends at x=39");
}

// --- legacy.html: only pre-M14 CSS ------------------------------------------

void TestLegacyPage(const std::string& site) {
  toy::shell::Page page;
  toy_test::Check(LoadPageFile(site, "legacy.html", &page), "legacy.html loads");
  if (const ComputedStyle* style = StyleOf(page, "box", "legacy box")) {
    toy_test::Check(style->background_color == 0xFF112233u,
                    "legacy background-color stays as before");
    toy_test::Check(style->width.has_value(), "legacy width is stored");
    if (style->width) {
      CheckPixelLength(*style->width, 100.0f, "legacy width: 100px");
    }
    CheckPixelLength(style->height.value(), 20.0f, "legacy height: 20px");
  }
  if (const ComputedStyle* style = StyleOf(page, "text", "legacy text")) {
    toy_test::Check(style->color == 0xFF0000u, "legacy text color stays as before");
    CheckNearlyEqual(style->font_size_px, 24.0f, "legacy font-size stays as before");
    CheckMargin(style->margin_top, 4.0f, "legacy margin-top");
    CheckPixelLength(style->padding_top, 2.0f, "legacy padding-top");
  }
  CheckElementBox(page, "box", 8, 8, 100, 20, "legacy box geometry");
  CheckElementBox(page, "text", 16, 32, 288, 28, "legacy text block geometry");
  CheckTextBox(page, "Legacy", 20, 34, 72, 24, "legacy text geometry");
  CheckNearlyEqual(toy::layout::ContentHeight(page.boxes), 72.0f,
                   "the legacy page content height is 72 CSS px");

  const toy::render::Surface surface = PaintPage(site, "legacy.html", "legacy.html paint");
  CheckPixel(surface, 10, 10, 0xFF112233u, "the legacy background is painted");
  toy_test::Check(CountColorInRect(surface, 0xFFFF0000u, 16, 32, 304, 60) > 0,
                  "the legacy red text is painted");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <css_compat_site-directory>\n", argv[0]);
    return 1;
  }
  const std::string site = argv[1];

  toy::shell::Page values;
  toy_test::Check(LoadPageFile(site, "values.html", &values),
                  "values.html loads through the shell page loader");
  TestValuesComputed(values);

  toy::shell::Page typography;
  toy_test::Check(LoadPageFile(site, "typography.html", &typography),
                  "typography.html loads through the shell page loader");
  TestTypographyComputed(typography);

  toy::shell::Page boxes;
  toy_test::Check(LoadPageFile(site, "boxes.html", &boxes),
                  "boxes.html loads through the shell page loader");
  TestBoxesComputed(boxes);

  TestValuesGeometryAndPixels(site, values);
  TestTypographyGeometryAndPixels(site, typography);
  TestBoxesGeometryAndPixels(site, boxes);
  TestLegacyPage(site);

  return toy_test::Finish("shell css compatibility");
}
