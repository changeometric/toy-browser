// Hit-testing and scrollable content height on a laid-out box tree: half-open
// border-box rectangles, pre-order tie-breaking and the overflow-aware content
// height. All geometry comes from the deterministic fake text metrics.

#include <cstddef>
#include <cstdio>
#include <string>

#include "fake_measure.h"
#include "support/check.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/style/computed_style.h"

namespace {

using toy::dom::Document;
using toy::layout::Box;
using toy::layout::BoxId;
using toy::layout::BoxKind;
using toy::layout::BoxTree;
using toy::layout::ContentHeight;
using toy::layout::HitTest;
using toy::layout::kInvalidBox;
using toy::layout::LayoutDocument;
using toy::layout::LayoutOptions;

constexpr float kViewportWidth = 800.0f;

// A laid-out page. The DOM and style trees stay alive next to the box tree,
// whose boxes refer to DOM ids.
struct Page {
  Document document;
  toy::style::StyleTree styles;
  BoxTree boxes;
};

Page MakePage(const std::string& html, float viewport_width = kViewportWidth) {
  Page page;
  page.document = toy::dom::ParseDocument(html);
  page.styles = toy::style::ComputeStyles(page.document);
  LayoutOptions options;
  options.viewport_width_px = viewport_width;
  page.boxes =
      LayoutDocument(page.document, page.styles, options, toy::layout::test::MeasureFakeText);
  return page;
}

// The fixture used by most hit-test cases, with its geometry asserted by the
// block test: html 800x86, body 784x70 at (8,8), div 120x60 at (13,13), text
// "x" 8x16 at (23,23).
Page MakePaddingPage() {
  return MakePage("<div style=\"width: 100px; height: 40px; margin: 5px; padding: 10px\">x</div>");
}

void DescribeHit(const Page& page, BoxId hit) {
  if (hit == kInvalidBox) {
    std::fprintf(stderr, "hit test: no box hit\n");
    return;
  }
  const Box& box = page.boxes.Get(hit);
  if (box.kind == BoxKind::kText) {
    std::fprintf(stderr, "hit test: text box \"%s\" x=%.1f y=%.1f width=%.1f height=%.1f\n",
                 box.text.c_str(), static_cast<double>(box.x), static_cast<double>(box.y),
                 static_cast<double>(box.width), static_cast<double>(box.height));
    return;
  }
  std::fprintf(stderr, "hit test: box kind=%d node=%s x=%.1f y=%.1f width=%.1f height=%.1f\n",
               static_cast<int>(box.kind), page.document.Get(box.node).name.c_str(),
               static_cast<double>(box.x), static_cast<double>(box.y),
               static_cast<double>(box.width), static_cast<double>(box.height));
}

void ExpectHitElement(const Page& page, float x, float y, const char* expected_name,
                      const char* description) {
  const BoxId hit = HitTest(page.boxes, x, y);
  bool matches = false;
  if (hit != kInvalidBox) {
    const Box& box = page.boxes.Get(hit);
    matches = (box.kind == BoxKind::kBlock || box.kind == BoxKind::kInline) &&
              page.document.Get(box.node).name == expected_name;
  }
  if (!matches) {
    std::fprintf(stderr, "hit test: (%g, %g) did not hit element <%s>\n", static_cast<double>(x),
                 static_cast<double>(y), expected_name);
    DescribeHit(page, hit);
  }
  toy_test::Check(matches, description);
}

void ExpectHitText(const Page& page, float x, float y, const char* expected_text,
                   const char* description) {
  const BoxId hit = HitTest(page.boxes, x, y);
  bool matches = false;
  if (hit != kInvalidBox) {
    const Box& box = page.boxes.Get(hit);
    matches = box.kind == BoxKind::kText && box.text == expected_text;
  }
  if (!matches) {
    std::fprintf(stderr, "hit test: (%g, %g) did not hit text \"%s\"\n", static_cast<double>(x),
                 static_cast<double>(y), expected_text);
    DescribeHit(page, hit);
  }
  toy_test::Check(matches, description);
}

void ExpectNoHit(const Page& page, float x, float y, const char* description) {
  const BoxId hit = HitTest(page.boxes, x, y);
  if (hit != kInvalidBox) {
    std::fprintf(stderr, "hit test: (%g, %g) hit a box but should not have\n",
                 static_cast<double>(x), static_cast<double>(y));
    DescribeHit(page, hit);
  }
  toy_test::Check(hit == kInvalidBox, description);
}

void ExpectContentHeight(const Page& page, float expected, const char* description) {
  const float actual = ContentHeight(page.boxes);
  if (actual != expected) {
    std::fprintf(stderr, "hit test: expected content height %g, got %g\n",
                 static_cast<double>(expected), static_cast<double>(actual));
  }
  toy_test::Check(actual == expected, description);
}

// A point inside the text box wins over the block containing it: the text box is
// later in pre-order.
void TestInsideText() {
  const Page page = MakePaddingPage();
  ExpectHitText(page, 23.0f, 23.0f, "x",
                "a point on the text box's left/top edge hits the text box");
  ExpectHitText(page, 30.0f, 30.0f, "x", "a point inside the text box hits the text box");
}

// Inside the block but outside every child: the block box itself is returned.
void TestInsideBlockOutsideChildren() {
  const Page page = MakePaddingPage();
  ExpectHitElement(page, 100.0f, 60.0f, "div",
                   "a point inside the block but outside its text hits the block");
  ExpectHitElement(page, 132.9f, 72.9f, "div",
                   "a point just inside the block's bottom-right corner hits the block");
}

// Rectangles are half-open: the left/top edge is inside, the right/bottom edge is
// outside, so adjacent boxes never both contain a boundary point.
void TestHalfOpenBoundaries() {
  const Page page = MakePaddingPage();
  ExpectHitText(page, 23.0f, 30.0f, "x", "x == left edge of the text box is inside the text box");
  ExpectHitElement(page, 31.0f, 30.0f, "div", "x == right edge of the text box is outside it");
  ExpectHitElement(page, 13.0f, 30.0f, "div", "x == left edge of the block is inside the block");
  ExpectHitElement(page, 133.0f, 30.0f, "body", "x == right edge of the block is outside it");
  ExpectHitElement(page, 30.0f, 13.0f, "div", "y == top edge of the block is inside the block");
  ExpectHitElement(page, 30.0f, 73.0f, "body", "y == bottom edge of the block is outside it");
}

void TestNegativeAndPageOriginPoints() {
  const Page page = MakePaddingPage();
  ExpectHitElement(page, 0.0f, 0.0f, "html", "the html border-box origin is inside the page");
  ExpectNoHit(page, -1.0f, -1.0f, "a point above/left of the page hits no box");
  ExpectNoHit(page, -0.5f, 10.0f, "a point left of the page hits no box");
}

void TestPointBelowContent() {
  const Page page = MakePaddingPage();
  ExpectNoHit(page, 10.0f, 200.0f, "a point below the content hits no box");
  ExpectHitElement(page, 10.0f, 85.9f, "html",
                   "a point inside the html box but below the body hits html");
}

// A box with a non-positive size can never contain a point, even its own origin.
void TestNonPositiveSizeNeverMatches() {
  // body 784x60 at (8,8); the first div is at (8,28) and 40 tall, the second is
  // the zero-size box at (8,68) where the body ends, so only html contains it.
  const Page page = MakePage(
      "<div style=\"height: 40px; margin-top: 20px\"></div>"
      "<div style=\"width: 0px; height: 0px\"></div>");
  ExpectHitElement(page, 8.0f, 68.0f, "html",
                   "a box with a non-positive size never contains a point");
}

void TestEmptyTree() {
  const BoxTree boxes;
  toy_test::Check(HitTest(boxes, 0.0f, 0.0f) == kInvalidBox,
                  "an empty box tree has no hit-test result");
  toy_test::Check(ContentHeight(boxes) == 0.0f, "an empty box tree has content height 0");
}

// The content height is the bottom of the tallest box, not just the root's.
void TestContentHeight() {
  const Page content = MakePaddingPage();
  ExpectContentHeight(content, 86.0f, "the content height is the bottom of the tallest box");

  const Page bottom_margin = MakePage("<div style=\"height: 100px; margin-bottom: 50px\"></div>");
  ExpectContentHeight(bottom_margin, 166.0f,
                      "a larger bottom margin extends the scrollable content height");

  // html is 21 tall, but the overflowing child bottom is at 28.
  const Page overflow =
      MakePage("<div style=\"height: 5px\"><div style=\"height: 20px\"></div></div>");
  ExpectContentHeight(overflow, 28.0f,
                      "an overflowing child can define the scrollable content height");
}

// Both queries are iterative: 10,000 nested boxes must not overflow the C++ stack.
void TestDeepNestingIsIterative() {
  constexpr int kLevels = 10000;
  std::string html;
  html.reserve(static_cast<std::size_t>(kLevels) * 24);
  for (int level = 0; level < kLevels; ++level) {
    html.append("<div style=\"height:1px\">");
  }
  const Page page = MakePage(html);
  toy_test::Check(page.boxes.size() == static_cast<std::size_t>(kLevels) + 2,
                  "a deeply nested page produces one box per element");
  // Every box is 784x1 at (8,8); the deepest one is the last pre-order match.
  ExpectHitElement(page, 10.0f, 8.0f, "div",
                   "hit-testing a 10,000-level tree returns the last pre-order match");
  ExpectContentHeight(page, 17.0f, "content height of a deeply nested page stays linear");
}

}  // namespace

int main() {
  TestInsideText();
  TestInsideBlockOutsideChildren();
  TestHalfOpenBoundaries();
  TestNegativeAndPageOriginPoints();
  TestPointBelowContent();
  TestNonPositiveSizeNeverMatches();
  TestEmptyTree();
  TestContentHeight();
  TestDeepNestingIsIterative();
  return toy_test::Finish("layout hit test");
}
