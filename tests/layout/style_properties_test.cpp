// M14 style-driven layout: percentage resolution, min/max width and height
// constraints, auto-margin centering, replaced percentage widths, line-height
// (normal/number/px/em, half-leading baselines, <br> empty lines) and
// text-align (left/right/center/justify, overlong lines). All metrics come from
// the deterministic fake measurer, so every expected number is exact.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fake_measure.h"
#include "support/check.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/style/computed_style.h"

namespace {

using toy::dom::Document;
using toy::dom::NodeId;
using toy::layout::Box;
using toy::layout::BoxId;
using toy::layout::BoxKind;
using toy::layout::BoxTree;
using toy::layout::DumpBoxTree;
using toy::layout::kInvalidBox;
using toy::layout::LayoutDocument;
using toy::layout::LayoutOptions;
using toy::layout::ReplacedSize;
using toy::layout::ReplacedSizes;

constexpr float kViewport = 800.0f;
// UA geometry at the default viewport: `body { margin: 8px }`, so direct body
// children start at x = 8 inside a 784 px wide containing content box.
constexpr float kTolerance = 1e-3f;

// A laid-out page. The DOM and style trees stay alive next to the box tree.
struct Page {
  Document document;
  toy::style::StyleTree styles;
  BoxTree boxes;
};

Page LayoutPage(const std::string& html, float viewport_width = kViewport,
                std::optional<ReplacedSize> image_size = std::nullopt) {
  Page page;
  page.document = toy::dom::ParseDocument(html);
  page.styles = toy::style::ComputeStyles(page.document);
  ReplacedSizes sizes;
  if (image_size) {
    for (std::size_t index = 0; index < page.document.size(); ++index) {
      const NodeId id = static_cast<NodeId>(index);
      const toy::dom::Node& node = page.document.Get(id);
      if (node.type == toy::dom::NodeType::kElement && node.name == "img") {
        sizes[id] = *image_size;
      }
    }
  }
  LayoutOptions options;
  options.viewport_width_px = viewport_width;
  options.replaced_sizes = &sizes;
  page.boxes =
      LayoutDocument(page.document, page.styles, options, toy::layout::test::MeasureFakeText);
  return page;
}

std::vector<BoxId> AllBoxes(const BoxTree& boxes) {
  std::vector<BoxId> result;
  if (boxes.root() == kInvalidBox) {
    return result;
  }
  std::vector<BoxId> pending;
  pending.push_back(boxes.root());
  while (!pending.empty()) {
    const BoxId id = pending.back();
    pending.pop_back();
    result.push_back(id);
    std::vector<BoxId> children;
    for (BoxId child = boxes.Get(id).first_child; child != kInvalidBox;
         child = boxes.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(children[index]);
    }
  }
  return result;
}

// Every element box with this tag, in box-tree pre-order.
std::vector<const Box*> FindBoxes(const Page& page, std::string_view tag) {
  std::vector<const Box*> result;
  for (const BoxId id : AllBoxes(page.boxes)) {
    const Box& box = page.boxes.Get(id);
    if (box.node == toy::dom::kInvalidNode) {
      continue;
    }
    const toy::dom::Node& node = page.document.Get(box.node);
    if (node.type == toy::dom::NodeType::kElement && node.name == tag) {
      result.push_back(&box);
    }
  }
  return result;
}

const Box* BoxByTag(const Page& page, std::string_view tag, std::size_t index = 0) {
  const std::vector<const Box*> boxes = FindBoxes(page, tag);
  return index < boxes.size() ? boxes[index] : nullptr;
}

const Box* TextBox(const Page& page, std::string_view text) {
  for (const BoxId id : AllBoxes(page.boxes)) {
    const Box& box = page.boxes.Get(id);
    if (box.kind == BoxKind::kText && box.text == text) {
      return &box;
    }
  }
  return nullptr;
}

void ExpectBoxGeometry(const Box* box, float x, float y, float width, float height,
                       const char* description) {
  if (box == nullptr) {
    std::fprintf(stderr, "style properties test: %s (box not found)\n", description);
    toy_test::Check(false, description);
    return;
  }
  const bool matches = std::fabs(box->x - x) <= kTolerance &&
                       std::fabs(box->y - y) <= kTolerance &&
                       std::fabs(box->width - width) <= kTolerance &&
                       std::fabs(box->height - height) <= kTolerance;
  if (!matches) {
    std::fprintf(stderr,
                 "style properties test: %s\n  expected x=%g y=%g width=%g height=%g\n"
                 "  actual   x=%g y=%g width=%g height=%g\n",
                 description, static_cast<double>(x), static_cast<double>(y),
                 static_cast<double>(width), static_cast<double>(height),
                 static_cast<double>(box->x), static_cast<double>(box->y),
                 static_cast<double>(box->width), static_cast<double>(box->height));
  }
  toy_test::Check(matches, description);
}

void ExpectBaseline(const Box* box, float expected, const char* description) {
  if (box == nullptr) {
    std::fprintf(stderr, "style properties test: %s (text box not found)\n", description);
    toy_test::Check(false, description);
    return;
  }
  if (std::fabs(box->baseline_offset_px - expected) > kTolerance) {
    std::fprintf(stderr, "style properties test: %s (expected %g, got %g)\n", description,
                 static_cast<double>(expected), static_cast<double>(box->baseline_offset_px));
  }
  toy_test::Check(std::fabs(box->baseline_offset_px - expected) <= kTolerance, description);
}

// ---- Percentage resolution -------------------------------------------------

// A specified width plus all four horizontal percentage margins/padding. The
// basis is the containing block content width (784 px), which is also CSS's
// basis for the vertical sides.
void TestPercentHorizontalBox() {
  const Page page = LayoutPage(
      "<div style=\"width:50%; margin-left:10%; margin-right:5%; padding-left:10%;"
      " padding-right:5%; height:20px\"></div>");
  // 50% of 784 = 392; 10% = 78.4; 5% = 39.2. x = 8 + 78.4, border width is
  // content + padding = 392 + 78.4 + 39.2.
  ExpectBoxGeometry(BoxByTag(page, "div"), 86.4f, 8.0f, 509.6f, 20.0f,
                    "percentage width/margins/padding resolve against the containing width");
}

// Vertical margin/padding percentages use the containing block WIDTH too, not
// the height.
void TestPercentVerticalUsesWidthBasis() {
  const Page page = LayoutPage(
      "<div style=\"margin-top:10%; margin-bottom:5%; padding-top:10%;"
      " padding-bottom:5%; height:20px\"></div>");
  // margin-top 78.4 -> y = 8 + 78.4; height = 20 + padding 78.4 + 39.2; the body
  // consumes margin-top + height + margin-bottom = 255.2.
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 86.4f, 784.0f, 137.6f,
                    "vertical percentage margins/padding use the containing block width");
  ExpectBoxGeometry(BoxByTag(page, "body"), 8.0f, 8.0f, 784.0f, 255.2f,
                    "the body height follows the percentage vertical geometry");
}

// Negative percentage margins are allowed; negative padding is clamped to 0.
void TestNegativePercentageMarginAndPadding() {
  const Page page =
      LayoutPage("<div style=\"margin-left:-10%; padding:-10%; width:50%; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), -70.4f, 8.0f, 392.0f, 10.0f,
                    "negative percentage margins apply while negative padding clamps to 0");
}

// The percentage basis of a nested box is its own containing block's content
// width, not the viewport.
void TestNestedPercentageBasis() {
  const Page page = LayoutPage(
      "<div style=\"width:200px; padding:10px\">"
      "<div style=\"width:50%; height:5px\"></div></div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 2, "the nested fixture has two div boxes");
  if (divs.size() != 2) {
    return;
  }
  ExpectBoxGeometry(divs[0], 8.0f, 8.0f, 220.0f, 25.0f,
                    "the outer block keeps its specified width plus padding");
  ExpectBoxGeometry(divs[1], 18.0f, 18.0f, 100.0f, 5.0f,
                    "a percentage width resolves against the parent content width");
}

// Percentage padding shrinks an auto content width so the border box still
// fills the containing block, and it also adds to the box height.
void TestPercentPaddingAutoWidth() {
  const Page page = LayoutPage("<div style=\"padding:10%; height:10px\"></div>");
  // Horizontal padding 78.4 per side: content 784 - 156.8 = 627.2, border 784.
  // The vertical percentage padding adds 156.8 to the fixed 10 px content height.
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 784.0f, 166.8f,
                    "percentage padding shrinks the auto content width");
}

// ---- min/max-width ---------------------------------------------------------

void TestMaxWidthClampsAutoWidth() {
  const Page page = LayoutPage("<div style=\"max-width:300px; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 300.0f, 10.0f,
                    "max-width clamps an auto width");
}

void TestMinWidthClampsAutoWidth() {
  const Page page = LayoutPage("<div style=\"min-width:1000px; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 1000.0f, 10.0f,
                    "min-width can make an auto-width block overflow its containing block");
}

void TestSpecifiedWidthClamped() {
  const Page page = LayoutPage(
      "<div style=\"width:100px; min-width:200px; height:10px\"></div>"
      "<div style=\"width:300px; max-width:200px; height:10px\"></div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 2, "the clamp fixture has two div boxes");
  if (divs.size() != 2) {
    return;
  }
  ExpectBoxGeometry(divs[0], 8.0f, 8.0f, 200.0f, 10.0f, "min-width widens a specified width");
  ExpectBoxGeometry(divs[1], 8.0f, 18.0f, 200.0f, 10.0f,
                    "max-width narrows a specified width in flow");
}

void TestMinThenMaxWidth() {
  // min first, then max: max(100, 300) = 300 -> min(300, 200) = 200.
  const Page page =
      LayoutPage("<div style=\"width:100px; min-width:300px; max-width:200px; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 200.0f, 10.0f,
                    "min-width is applied before max-width");
}

void TestPercentMaxWidth() {
  const Page page = LayoutPage("<div style=\"max-width:50%; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 392.0f, 10.0f,
                    "a percentage max-width resolves against the containing width");
}

// ---- auto margins ----------------------------------------------------------

// `margin: auto` centers a definite width in the containing block.
void TestAutoMarginsCenterPercentWidth() {
  // 50% of 784 = 392, remaining 392 -> 196 per side; x = 8 + 196.
  const Page page =
      LayoutPage("<div style=\"width:50%; margin-left:auto; margin-right:auto; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 204.0f, 8.0f, 392.0f, 10.0f,
                    "auto horizontal margins center a percentage width");
}

// The contract example: 50% + auto margins inside a 400 px containing block.
void TestAutoMarginsCenterInsideNestedBlock() {
  const Page page = LayoutPage(
      "<div style=\"width:400px\">"
      "<div style=\"width:50%; margin:0 auto; height:10px\"></div></div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 2, "the nested centering fixture has two div boxes");
  if (divs.size() != 2) {
    return;
  }
  ExpectBoxGeometry(divs[1], 108.0f, 8.0f, 200.0f, 10.0f,
                    "width 50% + auto margins inside 400 px -> x=100 (absolute x=108)");
}

// An odd remainder is shared in float arithmetic: left = remaining / 2.
void TestAutoMarginsOddRemainder() {
  const Page page = LayoutPage(
      "<div style=\"width:401px\">"
      "<div style=\"width:200px; margin:0 auto; height:10px\"></div></div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 2, "the odd-remainder fixture has two div boxes");
  if (divs.size() != 2) {
    return;
  }
  // Remaining 201 -> 100.5 per side; x = 8 + 100.5.
  ExpectBoxGeometry(divs[1], 108.5f, 8.0f, 200.0f, 10.0f,
                    "an odd auto-margin remainder is split without rounding");
}

// A single auto margin absorbs all remaining space.
void TestSingleAutoMargin() {
  const Page page = LayoutPage(
      "<div style=\"width:400px\">"
      "<div style=\"width:200px; margin-left:auto; margin-right:0; height:10px\"></div></div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 2, "the single-auto-margin fixture has two div boxes");
  if (divs.size() != 2) {
    return;
  }
  ExpectBoxGeometry(divs[1], 208.0f, 8.0f, 200.0f, 10.0f,
                    "a single auto margin receives the whole remainder");
}

// An auto horizontal margin with an auto width behaves as 0 (the box fills).
void TestAutoMarginWithAutoWidthIsZero() {
  const Page page =
      LayoutPage("<div style=\"margin-left:auto; margin-right:auto; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 784.0f, 10.0f,
                    "auto widths make auto horizontal margins 0");
}

// A width made definite by the max-width clamp participates in centering, like
// the common max-width + margin: auto page wrapper.
void TestClampedWidthCenters() {
  const Page page = LayoutPage("<div style=\"max-width:100px; margin:0 auto; height:10px\"></div>");
  // Tentative 784 -> clamped 100 -> remaining 684 -> 342 per side; x = 8 + 342.
  ExpectBoxGeometry(BoxByTag(page, "div"), 350.0f, 8.0f, 100.0f, 10.0f,
                    "a width clamped by max-width is centered by auto margins");
}

// Over-constrained auto margins fall back to 0 (CSS 10.3.3), so the box
// overflows to the right instead of moving left.
void TestOverconstrainedAutoMargins() {
  const Page page = LayoutPage("<div style=\"width:900px; margin:0 auto; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 900.0f, 10.0f,
                    "over-constrained auto margins resolve to 0");
}

// Vertical auto margins behave as 0 in this MVP.
void TestVerticalAutoMarginsAreZero() {
  const Page page =
      LayoutPage("<div style=\"margin-top:auto; margin-bottom:auto; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 784.0f, 10.0f,
                    "vertical auto margins behave as 0");
  ExpectBoxGeometry(BoxByTag(page, "body"), 8.0f, 8.0f, 784.0f, 10.0f,
                    "vertical auto margins consume no flow space");
}

// ---- min/max-height --------------------------------------------------------

void TestMinMaxHeightFixed() {
  const Page min_page = LayoutPage("<div style=\"height:50px; min-height:80px\"></div>");
  ExpectBoxGeometry(BoxByTag(min_page, "div"), 8.0f, 8.0f, 784.0f, 80.0f,
                    "min-height grows a fixed height");
  const Page max_page = LayoutPage("<div style=\"height:100px; max-height:40px\"></div>");
  ExpectBoxGeometry(BoxByTag(max_page, "div"), 8.0f, 8.0f, 784.0f, 40.0f,
                    "max-height shrinks a fixed height");
}

void TestMinMaxHeightContent() {
  const Page min_page = LayoutPage("<div style=\"min-height:40px\">x</div>");
  ExpectBoxGeometry(BoxByTag(min_page, "div"), 8.0f, 8.0f, 784.0f, 40.0f,
                    "min-height grows a content height");
  const Page max_page = LayoutPage("<div style=\"max-height:10px\">x</div>");
  ExpectBoxGeometry(BoxByTag(max_page, "div"), 8.0f, 8.0f, 784.0f, 10.0f,
                    "max-height clamps a content height");
}

void TestMinThenMaxHeight() {
  // min first, then max: max(50, 80) = 80 -> min(80, 60) = 60.
  const Page page =
      LayoutPage("<div style=\"height:50px; min-height:80px; max-height:60px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 784.0f, 60.0f,
                    "min-height is applied before max-height");
}

void TestHeightConstraintWithPadding() {
  const Page page =
      LayoutPage("<div style=\"height:20px; padding:5px; min-height:40px\"></div>");
  // The content height clamps to 40, then the 5 px vertical padding is added
  // (height 50); the 5 px horizontal padding shrinks the auto width back to 784.
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 784.0f, 50.0f,
                    "min-height clamps the content height before padding is added");
}

// Percentage heights and min/max-heights are unsupported in M14: they behave as
// auto / no constraint.
void TestPercentageHeightIsAuto() {
  const Page page =
      LayoutPage("<p style=\"height:50%; min-height:200%; max-height:300%\">x</p>");
  ExpectBoxGeometry(BoxByTag(page, "p"), 8.0f, 24.0f, 784.0f, 16.0f,
                    "percentage height constraints are ignored (behave as auto)");
}

// ---- replaced elements -----------------------------------------------------

void TestReplacedPercentageWidth() {
  const std::optional<ReplacedSize> intrinsic = ReplacedSize{16.0f, 8.0f};
  // 50% of the p content width (784) = 392; width-only keeps the ratio 2:1.
  const Page page = LayoutPage("<p><img style=\"width:50%\"></p>", kViewport, intrinsic);
  ExpectBoxGeometry(BoxByTag(page, "img"), 8.0f, 24.0f, 392.0f, 196.0f,
                    "a replaced percentage width resolves against the containing width");
  ExpectBoxGeometry(BoxByTag(page, "p"), 8.0f, 24.0f, 784.0f, 196.0f,
                    "the replaced box raises the line and paragraph height");
}

void TestReplacedPercentageHeightIgnored() {
  const std::optional<ReplacedSize> intrinsic = ReplacedSize{16.0f, 8.0f};
  const Page page = LayoutPage("<p><img style=\"height:50%\"></p>", kViewport, intrinsic);
  ExpectBoxGeometry(BoxByTag(page, "img"), 8.0f, 24.0f, 16.0f, 8.0f,
                    "a percentage replaced height is ignored (intrinsic size)");
}

void TestReplacedPercentageWidthAndHeight() {
  const std::optional<ReplacedSize> intrinsic = ReplacedSize{16.0f, 8.0f};
  // The percentage height is ignored, so the width-only ratio path applies.
  const Page page =
      LayoutPage("<p><img style=\"width:50%; height:50%\"></p>", kViewport, intrinsic);
  ExpectBoxGeometry(BoxByTag(page, "img"), 8.0f, 24.0f, 392.0f, 196.0f,
                    "a percentage replaced height is ignored while the width keeps the ratio");

  // A percentage width with a px height uses both specified sides.
  const Page sized =
      LayoutPage("<p><img style=\"width:50%; height:10px\"></p>", kViewport, intrinsic);
  ExpectBoxGeometry(BoxByTag(sized, "img"), 8.0f, 24.0f, 392.0f, 10.0f,
                    "a percentage width combines with a px height on a replaced element");
}

// ---- line-height -----------------------------------------------------------

void TestLineHeightNumber() {
  const Page page = LayoutPage("<p style=\"line-height:2\">x</p>");
  // contribution = 2 * 16 = 32; half-leading = (32 - 16) / 2 = 8; baseline 24.
  const Box* text = TextBox(page, "x");
  ExpectBoxGeometry(text, 8.0f, 24.0f, 8.0f, 32.0f,
                    "a unitless line-height multiplies the font size");
  ExpectBaseline(text, 24.0f, "half-leading positions the unitless line-height baseline");
  ExpectBoxGeometry(BoxByTag(page, "p"), 8.0f, 24.0f, 784.0f, 32.0f,
                    "the paragraph height is the line-height contribution");
}

void TestLineHeightPx() {
  const Page page = LayoutPage("<p style=\"line-height:24px\">x</p>");
  // contribution 24; half-leading (24 - 16) / 2 = 4; baseline 20.
  const Box* text = TextBox(page, "x");
  ExpectBoxGeometry(text, 8.0f, 24.0f, 8.0f, 24.0f, "a px line-height sets the line box height");
  ExpectBaseline(text, 20.0f, "half-leading positions the px line-height baseline");
  ExpectBoxGeometry(BoxByTag(page, "p"), 8.0f, 24.0f, 784.0f, 24.0f,
                    "the paragraph height follows the px line-height");
}

void TestLineHeightEm() {
  // `em` resolves in style against the element's own font size: 2em at 10px.
  const Page page = LayoutPage("<p style=\"font-size:10px; line-height:2em\">x</p>");
  // contribution 20; measured line height 10; half-leading 5; baseline 15.
  const Box* text = TextBox(page, "x");
  ExpectBoxGeometry(text, 8.0f, 24.0f, 5.0f, 20.0f, "an em line-height becomes px in style");
  ExpectBaseline(text, 15.0f, "half-leading positions the em line-height baseline");
  ExpectBoxGeometry(BoxByTag(page, "p"), 8.0f, 24.0f, 784.0f, 20.0f,
                    "the paragraph height follows the em line-height");
}

void TestLineHeightInheritedPxOverLargerFont() {
  // The px line-height is inherited as computed even when the child font is
  // larger: contribution 16, measured 32 -> negative half-leading.
  const Page page = LayoutPage("<p style=\"line-height:16px\"><span style=\"font-size:32px\">X"
                               "</span></p>");
  const Box* text = TextBox(page, "X");
  ExpectBoxGeometry(text, 8.0f, 24.0f, 16.0f, 16.0f,
                    "an inherited px line-height stays fixed for a larger child font");
  ExpectBaseline(text, 24.0f, "negative half-leading keeps the larger font's ascent");
}

void TestLineHeightMixedSizes() {
  const Page page =
      LayoutPage("<p style=\"line-height:2\"><span style=\"font-size:20px\">aa</span>bb</p>");
  // "aa": contribution 40, half-leading 10, baseline 30. "bb": contribution 32,
  // half-leading 8, baseline 24. The line is 40 tall with the shared max baseline.
  const Box* aa = TextBox(page, "aa");
  const Box* bb = TextBox(page, "bb");
  ExpectBoxGeometry(aa, 8.0f, 24.0f, 20.0f, 40.0f,
                    "the larger font's line-height contribution sets the line height");
  ExpectBaseline(aa, 30.0f, "the larger font's baseline includes its half-leading");
  ExpectBoxGeometry(bb, 28.0f, 24.0f, 16.0f, 40.0f,
                    "a smaller text item shares the line box height");
  ExpectBaseline(bb, 30.0f, "all text boxes share the line's maximum baseline");
  ExpectBoxGeometry(BoxByTag(page, "p"), 8.0f, 24.0f, 784.0f, 40.0f,
                    "the paragraph height is the maximum line-height contribution");
}

void TestLineHeightSmallerThanFont() {
  const Page page = LayoutPage("<p style=\"line-height:10px\">x</p>");
  // contribution 10; half-leading (10 - 16) / 2 = -3; baseline 13.
  const Box* text = TextBox(page, "x");
  ExpectBoxGeometry(text, 8.0f, 24.0f, 8.0f, 10.0f,
                    "a line-height smaller than the font shrinks the line box");
  ExpectBaseline(text, 13.0f, "negative half-leading positions a short line-height baseline");
}

void TestBreakEmptyLineLineHeight() {
  const Page number_page = LayoutPage("<p style=\"line-height:2\"><br></p>");
  ExpectBoxGeometry(BoxByTag(number_page, "p"), 8.0f, 24.0f, 784.0f, 32.0f,
                    "an empty <br> line uses the specified number line-height");

  const Page px_page = LayoutPage("<p style=\"line-height:40px\">a<br><br>b</p>");
  // Line 1: "a" (contribution 40). Line 2: the empty <br> line (40). Line 3: "b".
  ExpectBoxGeometry(BoxByTag(px_page, "p"), 8.0f, 24.0f, 784.0f, 120.0f,
                    "the empty <br> line between two text lines contributes its line-height");
  const Box* a = TextBox(px_page, "a");
  const Box* b = TextBox(px_page, "b");
  ExpectBoxGeometry(a, 8.0f, 24.0f, 8.0f, 40.0f, "the first text line is one line-height tall");
  ExpectBaseline(a, 28.0f, "the first text line's baseline includes the half-leading");
  ExpectBoxGeometry(b, 8.0f, 104.0f, 8.0f, 40.0f,
                    "text after the empty <br> line starts one line-height later");
  ExpectBaseline(b, 28.0f, "the last text line's baseline includes the half-leading");
}

// Pages that do not use line-height keep the exact pre-M14 dump (the fake
// measurer's line height and ascent are both the font size, so `normal` has zero
// half-leading and the baseline equals the ascent).
void TestNormalLineHeightDumpUnchanged() {
  const Page page = LayoutPage("<p>ab cd</p>");
  const char* expected =
      "<html> block x=0 y=0 width=800 height=64\n"
      "  <body> block x=8 y=8 width=784 height=48\n"
      "    <p> block x=8 y=24 width=784 height=16\n"
      "      text \"ab\" x=8 y=24 width=16 height=16\n"
      "      text \"cd\" x=32 y=24 width=16 height=16\n";
  const std::string actual = DumpBoxTree(page.document, page.boxes);
  const bool matches = actual == expected;
  if (!matches) {
    std::fprintf(stderr, "\nstyle properties test: normal line-height dump\nactual:\n%s",
                 actual.c_str());
  }
  toy_test::Check(matches, "pages without line-height keep the pre-M14 box dump");
  ExpectBaseline(TextBox(page, "ab"), 16.0f, "normal line-height keeps the measured ascent");
}

// ---- text-align ------------------------------------------------------------

void TestTextAlignCenter() {
  const Page page = LayoutPage("<p style=\"width:100px; text-align:center\">ab</p>");
  // used width 16 in a 100 px line -> offset (100 - 16) / 2 = 42; x = 8 + 42.
  ExpectBoxGeometry(TextBox(page, "ab"), 50.0f, 24.0f, 16.0f, 16.0f,
                    "text-align center shifts the line boxes");
}

void TestTextAlignRight() {
  const Page page = LayoutPage("<p style=\"width:100px; text-align:right\">ab</p>");
  // offset 100 - 16 = 84; x = 8 + 84.
  ExpectBoxGeometry(TextBox(page, "ab"), 92.0f, 24.0f, 16.0f, 16.0f,
                    "text-align right shifts the line boxes");
}

void TestTextAlignJustifyIsLeft() {
  const Page page = LayoutPage("<p style=\"width:100px; text-align:justify\">ab</p>");
  ExpectBoxGeometry(TextBox(page, "ab"), 8.0f, 24.0f, 16.0f, 16.0f,
                    "text-align justify lays out as left in this MVP");
}

void TestTextAlignOverlongStaysLeft() {
  const Page centered = LayoutPage("<p style=\"width:16px; text-align:center\">aaaa</p>");
  ExpectBoxGeometry(TextBox(centered, "aaaa"), 8.0f, 24.0f, 32.0f, 16.0f,
                    "an overlong centered line stays left-aligned");
  const Page right = LayoutPage("<p style=\"width:16px; text-align:right\">aaaa</p>");
  ExpectBoxGeometry(TextBox(right, "aaaa"), 8.0f, 24.0f, 32.0f, 16.0f,
                    "an overlong right-aligned line stays left-aligned");
}

void TestTextAlignMultipleLines() {
  const Page centered = LayoutPage("<p style=\"width:32px; text-align:center\">aa bb</p>");
  // Each line's used width is 16 in a 32 px line -> offset 8; x = 8 + 8.
  ExpectBoxGeometry(TextBox(centered, "aa"), 16.0f, 24.0f, 16.0f, 16.0f,
                    "the first centered line is shifted by its own used width");
  ExpectBoxGeometry(TextBox(centered, "bb"), 16.0f, 40.0f, 16.0f, 16.0f,
                    "the second centered line is shifted independently");
  ExpectBoxGeometry(BoxByTag(centered, "p"), 8.0f, 24.0f, 32.0f, 32.0f,
                    "centering does not re-wrap or re-measure the lines");

  const Page right = LayoutPage("<p style=\"width:32px; text-align:right\">aa bb</p>");
  ExpectBoxGeometry(TextBox(right, "aa"), 24.0f, 24.0f, 16.0f, 16.0f,
                    "the first right-aligned line is shifted independently");
  ExpectBoxGeometry(TextBox(right, "bb"), 24.0f, 40.0f, 16.0f, 16.0f,
                    "the second right-aligned line is shifted independently");
}

void TestTextAlignShiftsInlineFragments() {
  const Page page = LayoutPage(
      "<p style=\"width:40px; text-align:center\">"
      "<span style=\"padding:0 4px\">x</span></p>");
  // Used width = padding 4 + advance 8 + padding 4 = 16; offset (40 - 16)/2 = 12.
  // The fragment starts at x=8 and the text at x=12 before the shift.
  ExpectBoxGeometry(BoxByTag(page, "span"), 20.0f, 24.0f, 16.0f, 16.0f,
                    "an inline fragment is shifted by the line alignment");
  ExpectBoxGeometry(TextBox(page, "x"), 24.0f, 24.0f, 8.0f, 16.0f,
                    "text inside a shifted fragment keeps its relative position");
}

void TestTextAlignShiftsReplacedBox() {
  const Page page =
      LayoutPage("<p style=\"width:40px; text-align:center\"><img></p>", kViewport,
                 ReplacedSize{16.0f, 8.0f});
  // Used width 16 in a 40 px line -> offset 12; x = 8 + 12.
  ExpectBoxGeometry(BoxByTag(page, "img"), 20.0f, 24.0f, 16.0f, 8.0f,
                    "a replaced box is shifted by the line alignment");
  ExpectBoxGeometry(BoxByTag(page, "p"), 8.0f, 24.0f, 40.0f, 8.0f,
                    "the aligned replaced line keeps the line height");
}

void TestTextAlignInherited() {
  const Page page =
      LayoutPage("<body style=\"text-align:center\"><p style=\"width:100px\">ab</p></body>");
  ExpectBoxGeometry(TextBox(page, "ab"), 50.0f, 24.0f, 16.0f, 16.0f,
                    "text-align is inherited by the block container's lines");
}

// ---- inline percentages and visibility -------------------------------------

// Percentage inline margins/padding use the containing block content width.
void TestInlinePercentageMarginsAndPadding() {
  const Page page = LayoutPage(
      "<p><span style=\"margin-left:10%; padding-left:10%; padding-right:5%;"
      " margin-right:5%\">ab</span></p>");
  // Basis 784: margin-left 78.4, padding-left 78.4, padding-right 39.2,
  // margin-right 39.2. The fragment border box spans from x=86.4 to 220.0.
  ExpectBoxGeometry(BoxByTag(page, "span"), 86.4f, 24.0f, 133.6f, 16.0f,
                    "inline percentage margins/padding resolve against the containing width");
  ExpectBoxGeometry(TextBox(page, "ab"), 164.8f, 24.0f, 16.0f, 16.0f,
                    "the text starts after the inline left margin and padding");
}

// visibility does not change layout: hidden boxes keep their exact geometry.
void TestVisibilityGeometryUnchanged() {
  const Page visible = LayoutPage("<p>a<span>b</span>c</p>");
  const Page hidden = LayoutPage("<p>a<span style=\"visibility:hidden\">b</span>c</p>");
  const std::string visible_dump = DumpBoxTree(visible.document, visible.boxes);
  const std::string hidden_dump = DumpBoxTree(hidden.document, hidden.boxes);
  const bool matches = visible_dump == hidden_dump;
  if (!matches) {
    std::fprintf(stderr, "\nstyle properties test: visibility dump\nvisible:\n%s\nhidden:\n%s\n",
                 visible_dump.c_str(), hidden_dump.c_str());
  }
  toy_test::Check(matches, "visibility hidden keeps the exact same box geometry");
  const std::vector<const Box*> spans = FindBoxes(hidden, "span");
  toy_test::Check(spans.size() == 1, "a visibility-hidden inline element still produces its box");
}

}  // namespace

int main() {
  TestPercentHorizontalBox();
  TestPercentVerticalUsesWidthBasis();
  TestNegativePercentageMarginAndPadding();
  TestNestedPercentageBasis();
  TestPercentPaddingAutoWidth();
  TestMaxWidthClampsAutoWidth();
  TestMinWidthClampsAutoWidth();
  TestSpecifiedWidthClamped();
  TestMinThenMaxWidth();
  TestPercentMaxWidth();
  TestAutoMarginsCenterPercentWidth();
  TestAutoMarginsCenterInsideNestedBlock();
  TestAutoMarginsOddRemainder();
  TestSingleAutoMargin();
  TestAutoMarginWithAutoWidthIsZero();
  TestClampedWidthCenters();
  TestOverconstrainedAutoMargins();
  TestVerticalAutoMarginsAreZero();
  TestMinMaxHeightFixed();
  TestMinMaxHeightContent();
  TestMinThenMaxHeight();
  TestHeightConstraintWithPadding();
  TestPercentageHeightIsAuto();
  TestReplacedPercentageWidth();
  TestReplacedPercentageHeightIgnored();
  TestReplacedPercentageWidthAndHeight();
  TestLineHeightNumber();
  TestLineHeightPx();
  TestLineHeightEm();
  TestLineHeightInheritedPxOverLargerFont();
  TestLineHeightMixedSizes();
  TestLineHeightSmallerThanFont();
  TestBreakEmptyLineLineHeight();
  TestNormalLineHeightDumpUnchanged();
  TestTextAlignCenter();
  TestTextAlignRight();
  TestTextAlignJustifyIsLeft();
  TestTextAlignOverlongStaysLeft();
  TestTextAlignMultipleLines();
  TestTextAlignShiftsInlineFragments();
  TestTextAlignShiftsReplacedBox();
  TestTextAlignInherited();
  TestInlinePercentageMarginsAndPadding();
  TestVisibilityGeometryUnchanged();
  return toy_test::Finish("layout style properties test");
}
