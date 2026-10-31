// M14b box model: used border widths in the box geometry, `box-sizing`
// content-box vs border-box for the width/height and px min/max constraints,
// auto-margin centering against the border box, inline horizontal borders and
// fragment widths, none/hidden borders as zero width, replaced-element and
// overflow/decoration no-ops, border-box hit testing, and byte-identical
// pre-M14b geometry. All metrics come from the deterministic fake measurer.

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
using toy::layout::BoxTree;
using toy::layout::DumpBoxTree;
using toy::layout::HitTest;
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
    if (box.kind == toy::layout::BoxKind::kText && box.text == text) {
      return &box;
    }
  }
  return nullptr;
}

BoxId BoxIdByTag(const Page& page, std::string_view tag) {
  for (const BoxId id : AllBoxes(page.boxes)) {
    const Box& box = page.boxes.Get(id);
    if (box.node == toy::dom::kInvalidNode) {
      continue;
    }
    const toy::dom::Node& node = page.document.Get(box.node);
    if (node.type == toy::dom::NodeType::kElement && node.name == tag) {
      return id;
    }
  }
  return kInvalidBox;
}

void ExpectBoxGeometry(const Box* box, float x, float y, float width, float height,
                       const char* description) {
  if (box == nullptr) {
    std::fprintf(stderr, "box model test: %s (box not found)\n", description);
    toy_test::Check(false, description);
    return;
  }
  const bool matches = std::fabs(box->x - x) <= kTolerance &&
                       std::fabs(box->y - y) <= kTolerance &&
                       std::fabs(box->width - width) <= kTolerance &&
                       std::fabs(box->height - height) <= kTolerance;
  if (!matches) {
    std::fprintf(stderr,
                 "box model test: %s\n  expected x=%g y=%g width=%g height=%g\n"
                 "  actual   x=%g y=%g width=%g height=%g\n",
                 description, static_cast<double>(x), static_cast<double>(y),
                 static_cast<double>(width), static_cast<double>(height),
                 static_cast<double>(box->x), static_cast<double>(box->y),
                 static_cast<double>(box->width), static_cast<double>(box->height));
  }
  toy_test::Check(matches, description);
}

void ExpectDump(const std::string& html, const char* expected, const char* description,
                std::optional<ReplacedSize> image_size = std::nullopt,
                float viewport_width = kViewport) {
  const Page page = LayoutPage(html, viewport_width, image_size);
  const std::string actual = DumpBoxTree(page.document, page.boxes);
  const bool matches = actual == expected;
  if (!matches) {
    std::fprintf(stderr, "\nbox model test: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n",
                 description, html.c_str(), expected, actual.c_str());
  }
  toy_test::Check(matches, description);
}

// Two pages with the same geometry must dump identically (the dump carries only
// geometry, so this also proves a style-only property changed nothing).
void ExpectSameDump(const std::string& html_a, const std::string& html_b, const char* description,
                    std::optional<ReplacedSize> image_size = std::nullopt) {
  const Page page_a = LayoutPage(html_a, kViewport, image_size);
  const Page page_b = LayoutPage(html_b, kViewport, image_size);
  const std::string dump_a = DumpBoxTree(page_a.document, page_a.boxes);
  const std::string dump_b = DumpBoxTree(page_b.document, page_b.boxes);
  const bool matches = dump_a == dump_b;
  if (!matches) {
    std::fprintf(stderr, "\nbox model test: %s\nfirst:\n%s\nsecond:\n%s\n", description,
                 dump_a.c_str(), dump_b.c_str());
  }
  toy_test::Check(matches, description);
}

// ---- block border box ------------------------------------------------------

// A border on all four sides with different widths: the rect is the border box,
// the content box is inset by border + padding, and the height follows.
void TestBorderBoxAllSides() {
  const Page page = LayoutPage(
      "<div style=\"border-top:2px solid; border-right:4px solid;"
      " border-bottom:6px solid; border-left:8px solid;"
      " width:100px; height:40px; padding:10px\">x</div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 132.0f, 68.0f,
                    "the border box is content + padding + all four borders");
  ExpectBoxGeometry(TextBox(page, "x"), 26.0f, 20.0f, 8.0f, 16.0f,
                    "the content box is inset by the left border and padding");
  ExpectBoxGeometry(BoxByTag(page, "body"), 8.0f, 8.0f, 784.0f, 68.0f,
                    "the body consumes the border-box height");
}

// One-sided borders still grow only the sides that have one.
void TestBorderBoxOneSided() {
  const Page page = LayoutPage(
      "<div style=\"border-top:3px solid; border-left:5px solid;"
      " width:50px; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 55.0f, 13.0f,
                    "only the sides with a border grow the border box");
}

// Nested blocks: the child's containing block is the parent's content box, so
// borders and padding shrink the child's auto width.
void TestBorderBoxNested() {
  const Page page = LayoutPage(
      "<div style=\"border:2px solid; padding:10px\">"
      "<div style=\"height:10px\"></div></div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 2, "the nested border fixture has two div boxes");
  if (divs.size() != 2) {
    return;
  }
  ExpectBoxGeometry(divs[0], 8.0f, 8.0f, 784.0f, 34.0f,
                    "the outer border box spans the containing width plus borders");
  ExpectBoxGeometry(divs[1], 20.0f, 20.0f, 760.0f, 10.0f,
                    "the child fills the inner content box");
}

// ---- box-sizing on widths --------------------------------------------------

// content-box: width is the content; border-box: width is the whole border box.
void TestContentBoxVsBorderBoxWidth() {
  const Page page = LayoutPage(
      "<div style=\"width:100px; height:40px; padding:10px; border:2px solid\">a</div>"
      "<div style=\"box-sizing:border-box; width:100px; height:40px; padding:10px;"
      " border:2px solid\">b</div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 2, "the box-sizing fixture has two div boxes");
  if (divs.size() != 2) {
    return;
  }
  ExpectBoxGeometry(divs[0], 8.0f, 8.0f, 124.0f, 64.0f,
                    "content-box width is the content width");
  ExpectBoxGeometry(divs[1], 8.0f, 72.0f, 100.0f, 40.0f,
                    "border-box width is the border-box width");
  ExpectBoxGeometry(TextBox(page, "a"), 20.0f, 20.0f, 8.0f, 16.0f,
                    "the content-box text sits inside padding and borders");
  ExpectBoxGeometry(TextBox(page, "b"), 20.0f, 84.0f, 8.0f, 16.0f,
                    "the border-box content box is border box minus padding/borders");
}

// An auto width still fills the containing block minus margins; the content box
// then loses the padding and borders. Same result for both box-sizing values.
void TestAutoWidthWithBorders() {
  const Page content_box = LayoutPage(
      "<div style=\"padding:10px; border:2px solid\">"
      "<div style=\"height:5px\"></div></div>");
  const std::vector<const Box*> content_divs = FindBoxes(content_box, "div");
  toy_test::Check(content_divs.size() == 2, "the auto-width content-box fixture has two div boxes");
  if (content_divs.size() == 2) {
    ExpectBoxGeometry(content_divs[0], 8.0f, 8.0f, 784.0f, 29.0f,
                      "an auto border box fills the containing content width");
    ExpectBoxGeometry(content_divs[1], 20.0f, 20.0f, 760.0f, 5.0f,
                      "the content width subtracts padding and borders");
  }

  const Page border_box = LayoutPage(
      "<div style=\"box-sizing:border-box; padding:10px; border:2px solid\">"
      "<div style=\"height:5px\"></div></div>");
  const std::vector<const Box*> border_divs = FindBoxes(border_box, "div");
  toy_test::Check(border_divs.size() == 2, "the auto-width border-box fixture has two div boxes");
  if (border_divs.size() == 2) {
    ExpectBoxGeometry(border_divs[0], 8.0f, 8.0f, 784.0f, 29.0f,
                      "box-sizing does not change an auto border box");
    ExpectBoxGeometry(border_divs[1], 20.0f, 20.0f, 760.0f, 5.0f,
                      "an auto border-box child still sees the content width");
  }
}

// ---- min/max-width follow box-sizing ---------------------------------------

void TestMinMaxWidthContentBox() {
  const Page page = LayoutPage(
      "<div style=\"width:50px; min-width:150px; padding:5px; border:2px solid;"
      " height:10px\"></div>"
      "<div style=\"width:500px; max-width:200px; padding:5px; border:2px solid;"
      " height:10px\"></div>"
      "<div style=\"width:100px; min-width:300px; max-width:200px; padding:5px;"
      " border:2px solid; height:10px\"></div>"
      "<div style=\"max-width:200px; padding:5px; border:2px solid; height:10px\"></div>"
      "<div style=\"max-width:50%; padding:5px; border:2px solid; height:10px\"></div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 5, "the content-box min/max fixture has five div boxes");
  if (divs.size() != 5) {
    return;
  }
  ExpectBoxGeometry(divs[0], 8.0f, 8.0f, 164.0f, 24.0f,
                    "content-box min-width clamps the content width");
  ExpectBoxGeometry(divs[1], 8.0f, 32.0f, 214.0f, 24.0f,
                    "content-box max-width clamps the content width");
  ExpectBoxGeometry(divs[2], 8.0f, 56.0f, 214.0f, 24.0f,
                    "min-width is applied before max-width in content-box");
  ExpectBoxGeometry(divs[3], 8.0f, 80.0f, 214.0f, 24.0f,
                    "max-width clamps an auto content-box width");
  ExpectBoxGeometry(divs[4], 8.0f, 104.0f, 406.0f, 24.0f,
                    "a percentage max-width clamps the content width");
}

void TestMinMaxWidthBorderBox() {
  const Page page = LayoutPage(
      "<div style=\"box-sizing:border-box; width:50px; min-width:150px; padding:5px;"
      " border:2px solid; height:30px\"></div>"
      "<div style=\"box-sizing:border-box; width:500px; max-width:200px; padding:5px;"
      " border:2px solid; height:30px\"></div>"
      "<div style=\"box-sizing:border-box; width:100px; min-width:300px;"
      " max-width:200px; padding:5px; border:2px solid; height:30px\"></div>"
      "<div style=\"box-sizing:border-box; max-width:200px; padding:5px;"
      " border:2px solid; height:30px\"></div>"
      "<div style=\"box-sizing:border-box; max-width:50%; padding:5px;"
      " border:2px solid; height:30px\"></div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 5, "the border-box min/max fixture has five div boxes");
  if (divs.size() != 5) {
    return;
  }
  ExpectBoxGeometry(divs[0], 8.0f, 8.0f, 150.0f, 30.0f,
                    "border-box min-width clamps the border-box width");
  ExpectBoxGeometry(divs[1], 8.0f, 38.0f, 200.0f, 30.0f,
                    "border-box max-width clamps the border-box width");
  ExpectBoxGeometry(divs[2], 8.0f, 68.0f, 200.0f, 30.0f,
                    "min-width is applied before max-width in border-box");
  ExpectBoxGeometry(divs[3], 8.0f, 98.0f, 200.0f, 30.0f,
                    "max-width clamps an auto border-box width");
  ExpectBoxGeometry(divs[4], 8.0f, 128.0f, 392.0f, 30.0f,
                    "a percentage max-width clamps the border-box width");
}

// ---- auto margins center the border box ------------------------------------

void TestAutoMarginsWithBorders() {
  // content-box: 50% of 784 = 392 content; border box 392 + 20 padding + 10
  // border = 422; remaining 784 - 422 = 362 -> 181 per side; x = 8 + 181.
  const Page content_box = LayoutPage(
      "<div style=\"width:50%; margin:0 auto; padding:10px; border:5px solid;"
      " height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(content_box, "div"), 189.0f, 8.0f, 422.0f, 40.0f,
                    "auto margins center the content-box border box");

  // border-box: 50% of 784 = 392 border box; remaining 392 -> 196; x = 8 + 196.
  const Page border_box = LayoutPage(
      "<div style=\"box-sizing:border-box; width:50%; margin:0 auto; padding:10px;"
      " border:5px solid; height:50px\"></div>");
  ExpectBoxGeometry(BoxByTag(border_box, "div"), 204.0f, 8.0f, 392.0f, 50.0f,
                    "auto margins center the border-box border box");
}

void TestClampedWidthCentersWithBorders() {
  // content-box max-width 100 content -> border box 130; remaining 654 -> 327.
  const Page content_box = LayoutPage(
      "<div style=\"max-width:100px; margin:0 auto; padding:10px; border:5px solid;"
      " height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(content_box, "div"), 335.0f, 8.0f, 130.0f, 40.0f,
                    "a max-width clamped content box is centered with borders");
  // border-box max-width 100 border box -> remaining 684 -> 342; content 70.
  const Page border_box = LayoutPage(
      "<div style=\"box-sizing:border-box; max-width:100px; margin:0 auto;"
      " padding:10px; border:5px solid; height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(border_box, "div"), 350.0f, 8.0f, 100.0f, 30.0f,
                    "a max-width clamped border box is centered with borders");
}

void TestOverconstrainedAutoMarginsWithBorders() {
  const Page page = LayoutPage(
      "<div style=\"width:900px; margin:0 auto; padding:10px; border:5px solid;"
      " height:10px\"></div>");
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 930.0f, 40.0f,
                    "over-constrained auto margins stay 0 with borders");
}

// ---- heights follow box-sizing ---------------------------------------------

void TestContentVsBorderBoxHeight() {
  const Page page = LayoutPage(
      "<div style=\"width:50px; height:40px; padding:10px; border:2px solid\"></div>"
      "<div style=\"box-sizing:border-box; width:50px; height:40px; padding:10px;"
      " border:2px solid\"></div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 2, "the height box-sizing fixture has two div boxes");
  if (divs.size() != 2) {
    return;
  }
  ExpectBoxGeometry(divs[0], 8.0f, 8.0f, 74.0f, 64.0f,
                    "content-box height is the content height plus padding and borders");
  ExpectBoxGeometry(divs[1], 8.0f, 72.0f, 50.0f, 40.0f,
                    "border-box height is the border-box height");
}

void TestBorderBoxSmallerThanPaddingAndBorders() {
  const Page page = LayoutPage(
      "<div style=\"box-sizing:border-box; width:20px; height:10px; padding:10px;"
      " border:2px solid\"></div>");
  // The content box clamps to 0, so the border box is exactly padding + borders.
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 24.0f, 24.0f,
                    "a border-box smaller than its padding/borders clamps the content to 0");
}

void TestMinMaxHeightContentBox() {
  const Page page = LayoutPage(
      "<div style=\"width:50px; height:50px; min-height:80px; padding:10px;"
      " border:2px solid\"></div>"
      "<div style=\"width:50px; height:100px; max-height:40px; padding:10px;"
      " border:2px solid\"></div>"
      "<div style=\"width:50px; height:50px; min-height:100px; max-height:60px;"
      " padding:10px; border:2px solid\"></div>"
      "<div style=\"width:50px; min-height:40px; padding:10px; border:2px solid\">x</div>"
      "<div style=\"width:50px; padding:10px; border:2px solid\">x</div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 5, "the content-box min/max-height fixture has five div boxes");
  if (divs.size() != 5) {
    return;
  }
  ExpectBoxGeometry(divs[0], 8.0f, 8.0f, 74.0f, 104.0f,
                    "content-box min-height grows the content height");
  ExpectBoxGeometry(divs[1], 8.0f, 112.0f, 74.0f, 64.0f,
                    "content-box max-height clamps the content height");
  ExpectBoxGeometry(divs[2], 8.0f, 176.0f, 74.0f, 84.0f,
                    "min-height is applied before max-height in content-box");
  ExpectBoxGeometry(divs[3], 8.0f, 260.0f, 74.0f, 64.0f,
                    "content-box min-height clamps an auto content height");
  ExpectBoxGeometry(divs[4], 8.0f, 324.0f, 74.0f, 40.0f,
                    "an auto height is the content height plus padding and borders");
}

void TestMinMaxHeightBorderBox() {
  const Page page = LayoutPage(
      "<div style=\"box-sizing:border-box; width:50px; height:50px; min-height:80px;"
      " padding:10px; border:2px solid\"></div>"
      "<div style=\"box-sizing:border-box; width:50px; height:100px; max-height:40px;"
      " padding:10px; border:2px solid\"></div>"
      "<div style=\"box-sizing:border-box; width:50px; height:50px; min-height:100px;"
      " max-height:60px; padding:10px; border:2px solid\"></div>"
      "<div style=\"box-sizing:border-box; width:50px; min-height:40px; padding:10px;"
      " border:2px solid\">x</div>"
      "<div style=\"box-sizing:border-box; width:20px; height:10px; padding:10px;"
      " border:2px solid\"></div>");
  const std::vector<const Box*> divs = FindBoxes(page, "div");
  toy_test::Check(divs.size() == 5, "the border-box min/max-height fixture has five div boxes");
  if (divs.size() != 5) {
    return;
  }
  ExpectBoxGeometry(divs[0], 8.0f, 8.0f, 50.0f, 80.0f,
                    "border-box min-height clamps the border-box height");
  ExpectBoxGeometry(divs[1], 8.0f, 88.0f, 50.0f, 40.0f,
                    "border-box max-height clamps the border-box height");
  ExpectBoxGeometry(divs[2], 8.0f, 128.0f, 50.0f, 60.0f,
                    "min-height is applied before max-height in border-box");
  ExpectBoxGeometry(divs[3], 8.0f, 188.0f, 50.0f, 40.0f,
                    "border-box min-height clamps an auto content height");
  ExpectBoxGeometry(divs[4], 8.0f, 228.0f, 24.0f, 24.0f,
                    "a border-box below its padding/borders keeps a 24 px border box");
}

// ---- none/hidden borders ---------------------------------------------------

// A pair of pages whose dumps must differ (used to prove a style change has a
// layout effect at all).
void ExpectDifferentDump(const std::string& html_a, const std::string& html_b,
                         const char* description) {
  const Page page_a = LayoutPage(html_a);
  const Page page_b = LayoutPage(html_b);
  const std::string dump_a = DumpBoxTree(page_a.document, page_a.boxes);
  const std::string dump_b = DumpBoxTree(page_b.document, page_b.boxes);
  if (dump_a == dump_b) {
    std::fprintf(stderr, "\nbox model test: %s (the dumps are equal)\n%s", description,
                 dump_a.c_str());
  }
  toy_test::Check(dump_a != dump_b, description);
}

void TestBorderStyleNoneAndHiddenUseZeroWidth() {
  const char* plain = "<div style=\"width:100px; height:10px\">x</div>";
  const char* no_style = "<div style=\"width:100px; height:10px; border-width:10px\">x</div>";
  const char* hidden = "<div style=\"width:100px; height:10px; border:10px hidden\">x</div>";
  const char* solid = "<div style=\"width:100px; height:10px; border-style:solid\">x</div>";
  ExpectSameDump(plain, no_style,
                 "a border width without a border style occupies no space");
  ExpectSameDump(plain, hidden, "a hidden border occupies no space");
  ExpectDifferentDump(plain, solid,
                      "a visible border changes the geometry (sanity check for the comparison)");
  const Page page = LayoutPage(plain);
  ExpectBoxGeometry(BoxByTag(page, "div"), 8.0f, 8.0f, 100.0f, 10.0f,
                    "the plain box keeps its pre-M14b geometry");
}

// ---- inline borders --------------------------------------------------------

// Horizontal inline borders advance the line and widen the fragment's border
// box exactly like horizontal padding.
void TestInlineBorderAdvances() {
  const Page plain = LayoutPage("<p><span style=\"border-left:2px solid\">ab</span>c</p>");
  ExpectBoxGeometry(BoxByTag(plain, "span"), 8.0f, 24.0f, 18.0f, 16.0f,
                    "an inline left border widens the fragment border box");
  ExpectBoxGeometry(TextBox(plain, "ab"), 10.0f, 24.0f, 16.0f, 16.0f,
                    "the text starts after the inline left border");
  ExpectBoxGeometry(TextBox(plain, "c"), 26.0f, 24.0f, 8.0f, 16.0f,
                    "the following text starts after the fragment border box");

  const Page padded = LayoutPage(
      "<p><span style=\"border-left:2px solid; padding-left:3px;"
      " border-right:4px solid; padding-right:5px\">ab</span></p>");
  ExpectBoxGeometry(BoxByTag(padded, "span"), 8.0f, 24.0f, 30.0f, 16.0f,
                    "inline borders and padding both widen the fragment border box");
  ExpectBoxGeometry(TextBox(padded, "ab"), 13.0f, 24.0f, 16.0f, 16.0f,
                    "the text starts after the inline left border and padding");
}

// Vertical inline borders are ignored by layout (the M5 inline-vertical limit):
// the line box height does not change.
void TestInlineVerticalBordersIgnored() {
  ExpectSameDump(
      "<p><span style=\"border-left:2px solid\">x</span></p>",
      "<p><span style=\"border-top:10px solid; border-bottom:10px solid;"
      " border-left:2px solid\">x</span></p>",
      "vertical inline borders do not change the line box height");
  const Page page = LayoutPage("<p><span style=\"border-left:2px solid\">x</span></p>");
  ExpectBoxGeometry(TextBox(page, "x"), 10.0f, 24.0f, 8.0f, 16.0f,
                    "the inline line height stays the font line height");
}

// A wrapped inline element keeps border-left on its first fragment and
// border-right on its last fragment.
void TestInlineBordersAcrossFragment() {
  const Page page = LayoutPage(
      "<p style=\"width:40px\"><span style=\"border-left:2px solid;"
      " border-right:4px solid\">aa bb</span></p>");
  const std::vector<const Box*> spans = FindBoxes(page, "span");
  toy_test::Check(spans.size() == 2, "the wrapped inline element produces two fragments");
  if (spans.size() != 2) {
    return;
  }
  ExpectBoxGeometry(spans[0], 8.0f, 24.0f, 18.0f, 16.0f,
                    "the first fragment includes its left border");
  ExpectBoxGeometry(spans[1], 8.0f, 40.0f, 20.0f, 16.0f,
                    "the last fragment includes its right border");
  ExpectBoxGeometry(TextBox(page, "aa"), 10.0f, 24.0f, 16.0f, 16.0f,
                    "the first-fragment text starts after the left border");
  ExpectBoxGeometry(BoxByTag(page, "p"), 8.0f, 24.0f, 40.0f, 32.0f,
                    "wrapping is unchanged by the inline borders");
}

// ---- replaced elements, overflow and decorations ---------------------------

// Replaced elements keep the M8a behavior: padding/margins/borders/box-sizing
// are ignored and width/height still come from the CSS size/ratio.
void TestReplacedIgnoresBorders() {
  const std::optional<ReplacedSize> intrinsic = ReplacedSize{16.0f, 8.0f};
  ExpectSameDump("<p><img style=\"width:32px\"></p>",
                 "<p><img style=\"width:32px; padding:10px; border:20px solid;"
                 " box-sizing:border-box\"></p>",
                 "replaced elements ignore borders, padding and box-sizing", intrinsic);
  const Page page = LayoutPage(
      "<p><img style=\"width:32px; padding:10px; border:20px solid;"
      " box-sizing:border-box\"></p>",
      kViewport, intrinsic);
  ExpectBoxGeometry(BoxByTag(page, "img"), 8.0f, 24.0f, 32.0f, 16.0f,
                    "a replaced box keeps its CSS/intrinsic size and ratio");
}

// overflow clipping and the decoration flags are paint concerns: no layout
// change.
void TestOverflowAndDecorationsDoNotChangeLayout() {
  const char* plain = "<div style=\"width:50px; height:10px\">x</div>";
  ExpectSameDump(plain,
                 "<div style=\"width:50px; height:10px; overflow:hidden;"
                 " text-decoration:line-through overline\">x</div>",
                 "overflow hidden and line-through/overline do not change layout");
  ExpectSameDump(plain,
                 "<div style=\"width:50px; height:10px; overflow-x:hidden;"
                 " text-decoration:underline\">x</div>",
                 "overflow-x hidden and underline do not change layout");
}

// ---- byte compatibility and hit testing ------------------------------------

// Pages without borders/box-sizing keep the pre-M14b (M14a) geometry exactly.
void TestPreM14bDumpUnchanged() {
  ExpectDump("<p>ab cd</p>",
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "ab" x=8 y=24 width=16 height=16
      text "cd" x=32 y=24 width=16 height=16
)boxes",
             "a borderless page keeps the pre-M14b box dump");

  // An M14a-property page (percent width, auto margins, padding) is unchanged.
  ExpectDump("<div style=\"width:50%; margin:0 auto; padding:10px; height:10px\"></div>",
             R"boxes(<html> block x=0 y=0 width=800 height=46
  <body> block x=8 y=8 width=784 height=30
    <div> block x=194 y=8 width=412 height=30
)boxes",
             "the M14a percentage/auto-margin geometry is unchanged by M14b");
}

// Hit testing uses the border-box rectangle, so the border itself is hittable.
void TestHitTestBorderBox() {
  const Page page = LayoutPage(
      "<div style=\"width:50px; height:20px; border:5px solid\"></div>");
  const BoxId div = BoxIdByTag(page, "div");
  toy_test::Check(div != kInvalidBox, "the bordered div produces a box");
  if (div == kInvalidBox) {
    return;
  }
  ExpectBoxGeometry(&page.boxes.Get(div), 8.0f, 8.0f, 60.0f, 30.0f,
                    "the bordered div's rect is its border box");
  toy_test::Check(HitTest(page.boxes, 9.0f, 9.0f) == div,
                  "a point on the border hits the box");
  toy_test::Check(HitTest(page.boxes, 67.9f, 37.9f) == div,
                  "a point inside the bottom-right border hits the box");
  toy_test::Check(HitTest(page.boxes, 68.0f, 10.0f) != div,
                  "a point right of the border box does not hit the box");
  toy_test::Check(HitTest(page.boxes, 10.0f, 38.0f) != div,
                  "a point below the border box does not hit the box");
}

}  // namespace

int main() {
  TestBorderBoxAllSides();
  TestBorderBoxOneSided();
  TestBorderBoxNested();
  TestContentBoxVsBorderBoxWidth();
  TestAutoWidthWithBorders();
  TestMinMaxWidthContentBox();
  TestMinMaxWidthBorderBox();
  TestAutoMarginsWithBorders();
  TestClampedWidthCentersWithBorders();
  TestOverconstrainedAutoMarginsWithBorders();
  TestContentVsBorderBoxHeight();
  TestBorderBoxSmallerThanPaddingAndBorders();
  TestMinMaxHeightContentBox();
  TestMinMaxHeightBorderBox();
  TestBorderStyleNoneAndHiddenUseZeroWidth();
  TestInlineBorderAdvances();
  TestInlineVerticalBordersIgnored();
  TestInlineBordersAcrossFragment();
  TestReplacedIgnoresBorders();
  TestOverflowAndDecorationsDoNotChangeLayout();
  TestPreM14bDumpUnchanged();
  TestHitTestBorderBox();
  return toy_test::Finish("layout box model test");
}
