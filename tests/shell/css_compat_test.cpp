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

// The three decoration flags of a computed style.
void CheckDecorationFlags(const ComputedStyle& style, bool underline, bool line_through,
                          bool overline, const char* description) {
  const std::string prefix = std::string(description) + " decoration flags";
  const bool matches = style.text_decoration_underline == underline &&
                       style.text_decoration_line_through == line_through &&
                       style.text_decoration_overline == overline;
  if (!matches) {
    std::fprintf(stderr, "  %s: underline=%d line-through=%d overline=%d\n", prefix.c_str(),
                 style.text_decoration_underline ? 1 : 0,
                 style.text_decoration_line_through ? 1 : 0,
                 style.text_decoration_overline ? 1 : 0);
  }
  toy_test::Check(matches, prefix.c_str());
}

// --- M14b: borders, box-sizing, overflow and decoration lines ---------------

void CheckBorderSide(const toy::style::BorderSide& side, float width_px,
                     toy::style::BorderStyle border_style, const std::string& description) {
  CheckPixelLength(side.width, width_px, description + " width");
  if (side.style != border_style) {
    std::fprintf(stderr, "  %s: wrong border style\n", description.c_str());
  }
  toy_test::Check(side.style == border_style, (description + " style").c_str());
}

void TestBordersComputed(const toy::shell::Page& page) {
  using toy::style::BorderStyle;
  using toy::style::BoxSizing;
  using toy::style::UsedBorderColor;
  using toy::style::UsedBorderWidth;

  if (const ComputedStyle* style = StyleOf(page, "top-only", "one-sided border")) {
    CheckBorderSide(style->border_top, 4.0f, BorderStyle::kSolid, "border-top: 4px solid");
    toy_test::Check(style->border_top.color.has_value(), "the top border color is explicit");
    if (style->border_top.color) {
      toy_test::Check(*style->border_top.color == 0xFFFF0000u,
                      "the top border color is #ff0000");
    }
    toy_test::Check(UsedBorderWidth(style->border_right) == 0.0f,
                    "unset border sides have used width 0");
    toy_test::Check(style->border_right.style == BorderStyle::kNone,
                    "unset border sides stay none");
  }

  if (const ComputedStyle* style = StyleOf(page, "four", "four-sided border")) {
    CheckBorderSide(style->border_top, 2.0f, BorderStyle::kSolid, "four/top");
    CheckBorderSide(style->border_right, 3.0f, BorderStyle::kSolid, "four/right");
    CheckBorderSide(style->border_bottom, 4.0f, BorderStyle::kSolid, "four/bottom");
    CheckBorderSide(style->border_left, 5.0f, BorderStyle::kSolid, "four/left");
    toy_test::Check(style->border_top.color && *style->border_top.color == 0xFFFF0000u,
                    "four/top color");
    toy_test::Check(style->border_right.color && *style->border_right.color == 0xFF00FF00u,
                    "four/right color");
    toy_test::Check(style->border_bottom.color && *style->border_bottom.color == 0xFF0000FFu,
                    "four/bottom color");
    toy_test::Check(style->border_left.color && *style->border_left.color == 0xFFFFFF00u,
                    "four/left color");
  }

  // The border shorthand without a color leaves currentColor, resolved to the
  // element's text color by UsedBorderColor.
  if (const ComputedStyle* style = StyleOf(page, "shorthand", "border shorthand")) {
    CheckBorderSide(style->border_top, 3.0f, BorderStyle::kSolid, "shorthand border");
    toy_test::Check(!style->border_top.color.has_value(),
                    "the shorthand border keeps currentColor");
    toy_test::Check(UsedBorderColor(style->border_top, style->color) == 0xFF123456u,
                    "currentColor resolves to the text color");
    toy_test::Check(UsedBorderColor(style->border_left, style->color) == 0xFF123456u,
                    "currentColor resolves on every side");
  }

  // An explicit rgba() border color keeps its alpha in the computed value.
  if (const ComputedStyle* style = StyleOf(page, "alpha", "alpha border")) {
    CheckBorderSide(style->border_top, 2.0f, BorderStyle::kSolid, "alpha border");
    toy_test::Check(style->border_top.color && *style->border_top.color == 0x80FF0000u,
                    "an rgba() border keeps its 128 alpha");
  }

  // A parsed non-solid style is kept in the computed style (paint draws it solid).
  if (const ComputedStyle* style = StyleOf(page, "dashed", "dashed border")) {
    CheckBorderSide(style->border_top, 2.0f, BorderStyle::kDashed, "dashed border");
    CheckBorderSide(style->border_left, 2.0f, BorderStyle::kDashed, "dashed border side");
  }

  // box-sizing controls whether width/height is the content or border box.
  if (const ComputedStyle* style = StyleOf(page, "content-box", "content-box sizing")) {
    toy_test::Check(style->box_sizing == BoxSizing::kContentBox,
                    "box-sizing: content-box is computed");
  }
  if (const ComputedStyle* style = StyleOf(page, "border-box", "border-box sizing")) {
    toy_test::Check(style->box_sizing == BoxSizing::kBorderBox,
                    "box-sizing: border-box is computed");
  }
}

void TestBordersGeometryAndPixels(const std::string& site, const toy::shell::Page& page) {
  // Border widths belong to the border box: content 100x40 + 5/3/4/2 = 108x46.
  CheckElementBox(page, "top-only", 8, 8, 304, 16, "the top border grows the border box");
  CheckElementBox(page, "four", 8, 24, 108, 46, "four borders grow the border box");
  CheckElementBox(page, "shorthand", 8, 70, 304, 26, "the shorthand border grows the box");
  CheckElementBox(page, "alpha", 8, 96, 304, 24, "the alpha border grows the box");
  CheckElementBox(page, "dashed", 8, 120, 304, 14, "the dashed border grows the box");
  CheckElementBox(page, "content-box", 8, 134, 110, 30,
                  "content-box adds the borders to the 100x20 content");
  CheckElementBox(page, "border-box", 8, 164, 100, 20,
                  "border-box keeps the 100x20 border box");
  CheckElementBox(page, "outer", 8, 184, 304, 24, "the nested outer border box");
  CheckElementBox(page, "inner", 14, 190, 292, 12, "the nested inner border box");
  CheckNearlyEqual(toy::layout::ContentHeight(page.boxes), 216.0f,
                   "the borders page content height is 216 CSS px");

  const toy::render::Surface surface = PaintPage(site, "borders.html", "borders.html paint");
  // Four different side colors on #four.
  CheckPixel(surface, 8, 24, 0xFFFF0000u, "the top border paints red");
  CheckPixel(surface, 114, 40, 0xFF00FF00u, "the right border paints green");
  CheckPixel(surface, 50, 67, 0xFF0000FFu, "the bottom border paints blue");
  CheckPixel(surface, 9, 40, 0xFFFFFF00u, "the left border paints yellow");
  CheckPixel(surface, 50, 40, 0xFFEEEEEEu, "the background fills the border box interior");
  // currentColor resolves to #123456; the background is painted underneath.
  CheckPixel(surface, 8, 70, 0xFF123456u, "the shorthand border uses currentColor");
  CheckPixel(surface, 100, 80, 0xFFDDDDDDu, "the shorthand background is painted");
  // rgba(255,0,0,0.5) over the white canvas blends to (255,127,127).
  CheckPixel(surface, 8, 96, 0xFFFF7F7Fu, "an alpha border blends with the canvas");
  CheckPixel(surface, 100, 106, 0xFFFFFFFFu, "the alpha-border box interior stays white");
  CheckPixel(surface, 8, 120, 0xFF770000u, "a dashed border paints solid in this MVP");
  // box-sizing: content-box has the smaller interior; border-box the larger one.
  CheckPixel(surface, 8, 134, 0xFF333333u, "the content-box border is painted");
  CheckPixel(surface, 50, 145, 0xFFBBBBBBu, "the content-box background is painted");
  CheckPixel(surface, 8, 164, 0xFF444444u, "the border-box border is painted");
  CheckPixel(surface, 50, 175, 0xFFAAAAAAu, "the border-box background is painted");
  // Nested boxes: outer border, padding background, inner border and background.
  CheckPixel(surface, 8, 184, 0xFF555555u, "the outer border is painted");
  CheckPixel(surface, 11, 187, 0xFF999999u, "the outer padding background is painted");
  CheckPixel(surface, 14, 190, 0xFF666666u, "the inner border is painted");
  CheckPixel(surface, 20, 195, 0xFF888888u, "the inner background is painted");
  // Exact color counts freeze the border ring areas (and thus box-sizing geometry).
  toy_test::Check(CountColor(surface, 0xFFFF0000u) == 1432,
                  "the red border areas have the frozen pixel count");
  toy_test::Check(CountColor(surface, 0xFF00FF00u) == 120,
                  "the green right border has the frozen pixel count");
  toy_test::Check(CountColor(surface, 0xFF0000FFu) == 432,
                  "the blue bottom border has the frozen pixel count");
  toy_test::Check(CountColor(surface, 0xFFFFFF00u) == 200,
                  "the yellow left border has the frozen pixel count");
  toy_test::Check(CountColor(surface, 0xFF123456u) == 1944,
                  "the currentColor shorthand border ring has the frozen pixel count");
  toy_test::Check(CountColor(surface, 0xFF333333u) == 1300,
                  "the content-box border ring has the frozen pixel count");
  toy_test::Check(CountColor(surface, 0xFF444444u) == 1100,
                  "the border-box border ring has the frozen pixel count");
  toy_test::Check(CountColor(surface, 0xFFFF7F7Fu) == 1296,
                  "the alpha border ring has the frozen pixel count");
}

void TestOverflowComputed(const toy::shell::Page& page) {
  using toy::style::Overflow;
  if (const ComputedStyle* style = StyleOf(page, "clip", "clipping box")) {
    toy_test::Check(style->overflow_x == Overflow::kHidden,
                    "overflow: hidden computes overflow-x hidden");
    toy_test::Check(style->overflow_y == Overflow::kHidden,
                    "overflow: hidden computes overflow-y hidden");
  }
  if (const ComputedStyle* style = StyleOf(page, "visible", "visible box")) {
    toy_test::Check(style->overflow_x == Overflow::kVisible,
                    "the default overflow stays visible");
  }
  if (const ComputedStyle* style = StyleOf(page, "bordered-clip", "bordered clipping box")) {
    toy_test::Check(style->overflow_x == Overflow::kHidden,
                    "a bordered box can clip too");
    CheckBorderSide(style->border_top, 3.0f, toy::style::BorderStyle::kSolid,
                    "bordered clip border");
  }
}

void TestOverflowGeometryAndPixels(const std::string& site, const toy::shell::Page& page) {
  CheckElementBox(page, "clip", 8, 8, 60, 24, "the clipping box keeps its fixed size");
  CheckElementBox(page, "sibling", 8, 32, 304, 10, "the sibling after the clipping box");
  CheckElementBox(page, "visible", 8, 42, 60, 24, "the visible-overflow box keeps its size");
  CheckElementBox(page, "bordered-clip", 8, 66, 46, 22,
                  "the bordered clipping box includes its 3 px borders");
  CheckElementBox(page, "outside", 8, 88, 304, 16, "the box after the clipping examples");
  // The clipped 60x24 box stays 24 px tall, but ContentHeight includes the
  // overflowing text box (y=104..136): 136 CSS px (M14b limit: clipping is
  // painting only, scrollable content height still sees overflowing boxes).
  CheckNearlyEqual(toy::layout::ContentHeight(page.boxes), 136.0f,
                   "the overflow page content height includes the overflowing text");

  const toy::render::Surface surface = PaintPage(site, "overflow.html", "overflow.html paint");
  // The oversized child background is clipped to the 60x24 padding box.
  CheckPixel(surface, 50, 20, 0xFFFF0000u, "the clipped child paints inside the clip");
  CheckPixel(surface, 67, 31, 0xFFFF0000u, "the clip's bottom-right pixel shows the child");
  CheckPixel(surface, 70, 10, 0xFFFFFFFFu, "nothing paints right of the clip");
  CheckPixel(surface, 50, 40, 0xFF00FF00u, "the sibling after the clip paints normally");
  toy_test::Check(CountColor(surface, 0xFFFF0000u) == 60 * 24,
                  "the clipped child paints exactly the clip area");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 8, 68, 32) == 0,
                  "the clipped text inside the overflow box is not painted");
  // overflow: visible lets the oversized child paint outside its parent.
  CheckPixel(surface, 250, 60, 0xFF0000FFu, "the visible-overflow child paints outside");
  toy_test::Check(CountColor(surface, 0xFF00FF00u) == 304 * 10,
                  "the sibling background is painted before the visible overflow child");
  // The bordered clip keeps its border and clips the child to the padding box.
  CheckPixel(surface, 8, 66, 0xFF00FFFFu, "the clipping box border is painted");
  CheckPixel(surface, 12, 70, 0xFFFFFF00u, "the child paints inside the padding-box clip");
  CheckPixel(surface, 8, 70, 0xFF00FFFFu, "the border stays visible outside the clip");
  toy_test::Check(CountColor(surface, 0xFFFFFF00u) == 40 * 16,
                  "the bordered clip shows exactly the 40x16 padding-box area");
  toy_test::Check(CountColor(surface, 0xFF00FFFFu) == 372,
                  "the clipping box border ring has the frozen pixel count");
  // The next box paints normally after the clipping examples.
  CheckPixel(surface, 250, 100, 0xFFCCCCCCu, "the box after the clipping examples paints");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 88, 320, 104) > 0,
                  "the text after the clipping examples is painted");
}

void TestDecorationsComputed(const toy::shell::Page& page) {
  if (const ComputedStyle* style = StyleOf(page, "underline", "underline decoration")) {
    CheckDecorationFlags(*style, true, false, false, "underline");
  }
  if (const ComputedStyle* style = StyleOf(page, "line-through", "line-through decoration")) {
    CheckDecorationFlags(*style, false, true, false, "line-through");
  }
  if (const ComputedStyle* style = StyleOf(page, "overline", "overline decoration")) {
    CheckDecorationFlags(*style, false, false, true, "overline");
  }
  if (const ComputedStyle* style = StyleOf(page, "combined", "combined decorations")) {
    CheckDecorationFlags(*style, true, true, true, "combined");
  }
  if (const ComputedStyle* style = StyleOf(page, "parent", "parent decoration")) {
    CheckDecorationFlags(*style, true, false, false, "parent");
  }
}

void TestDecorationsGeometryAndPixels(const std::string& site, const toy::shell::Page& page) {
  CheckElementBox(page, "underline", 8, 24, 304, 16, "the underlined block geometry");
  CheckElementBox(page, "line-through", 8, 72, 304, 16, "the line-through block geometry");
  CheckElementBox(page, "overline", 8, 120, 304, 16, "the overline block geometry");
  CheckElementBox(page, "combined", 8, 168, 304, 16, "the combined decoration block geometry");
  CheckNearlyEqual(toy::layout::ContentHeight(page.boxes), 304.0f,
                   "the decorations page content height is 304 CSS px");
  CheckTextBox(page, "Underline", 8, 24, 72, 16, "the underlined text box");
  CheckTextBox(page, "Line", 8, 72, 32, 16, "the line-through text boxes");

  const toy::render::Surface surface =
      PaintPage(site, "decorations.html", "decorations.html paint");
  // Bitmap 16 px metrics: box y + 16 is the baseline, so underline is y+17,
  // line-through is baseline - round(16/3) = y+11 and overline is y.
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 41, 88, 42) == 72,
                  "the underline row spans the 72 px text box width");
  CheckPixel(surface, 8, 41, 0xFF000000u, "the underline starts at the text box x");
  CheckPixel(surface, 88, 41, 0xFFFFFFFFu, "the underline ends with the text box width");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 83, 40, 84) == 32,
                  "line-through covers the first word's 32 px text box");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 40, 83, 48, 84) == 0,
                  "the space between the two text boxes has no line-through");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 120, 72, 121) == 64,
                  "overline covers the 64 px text box at the box top");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 185, 72, 186) == 64,
                  "the combined underline row");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 179, 72, 180) == 64,
                  "the combined line-through row");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 168, 72, 169) == 64,
                  "the combined overline row");
  // Inherited decoration: the plain child inside #parent gets the underline.
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 233, 56, 234) == 48,
                  "the parent text box underline row");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 64, 233, 136, 234) == 72,
                  "the inherited decoration paints the child's 72 px underline row");
  // A child declaration resets the inherited flags: it only gets line-through.
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 8, 281, 48, 282) == 0,
                  "the undecorated text has no underline row");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 56, 275, 96, 276) == 40,
                  "the child's line-through row");
  toy_test::Check(CountColorInRect(surface, 0xFF000000u, 56, 281, 96, 282) == 0,
                  "the child's own declaration dropped the inherited underline");
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

  toy::shell::Page borders;
  toy_test::Check(LoadPageFile(site, "borders.html", &borders),
                  "borders.html loads through the shell page loader");
  TestBordersComputed(borders);
  TestBordersGeometryAndPixels(site, borders);

  toy::shell::Page overflow;
  toy_test::Check(LoadPageFile(site, "overflow.html", &overflow),
                  "overflow.html loads through the shell page loader");
  TestOverflowComputed(overflow);
  TestOverflowGeometryAndPixels(site, overflow);

  toy::shell::Page decorations;
  toy_test::Check(LoadPageFile(site, "decorations.html", &decorations),
                  "decorations.html loads through the shell page loader");
  TestDecorationsComputed(decorations);
  TestDecorationsGeometryAndPixels(site, decorations);

  TestLegacyPage(site);

  return toy_test::Finish("shell css compatibility");
}
