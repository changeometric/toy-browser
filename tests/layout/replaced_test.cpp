// Replaced elements (<img> with a caller-supplied intrinsic size): sizing from
// CSS width/height and the intrinsic size, aspect-ratio preservation, atomic
// inline placement, line wrapping, line-height contribution, top alignment, the
// no-entry/zero-size fallbacks and the documented display:block MVP limit.
// All geometry comes from the deterministic fake text metrics.

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "fake_measure.h"
#include "support/check.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/style/computed_style.h"

namespace {

using toy::dom::Document;
using toy::dom::NodeId;
using toy::layout::BoxId;
using toy::layout::BoxKind;
using toy::layout::BoxTree;
using toy::layout::ContentHeight;
using toy::layout::DumpBoxTree;
using toy::layout::HitTest;
using toy::layout::kInvalidBox;
using toy::layout::LayoutDocument;
using toy::layout::LayoutOptions;
using toy::layout::ReplacedSize;
using toy::layout::ReplacedSizes;

constexpr float kViewportWidth = 800.0f;

// A laid-out page. The DOM and style trees stay alive next to the box tree,
// whose boxes refer to DOM ids.
struct Page {
  Document document;
  toy::style::StyleTree styles;
  BoxTree boxes;
};

// Lays `html` out with an intrinsic size for every <img> element (or no entry at
// all when `image_size` is nullopt, which is the "not a replaced element"
// fallback). The map is keyed by DOM element id, exactly like shell does it.
Page LayoutFixture(const std::string& html, std::optional<ReplacedSize> image_size,
                   float viewport_width = kViewportWidth) {
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

std::string DumpFixture(const std::string& html, std::optional<ReplacedSize> image_size,
                        float viewport_width = kViewportWidth) {
  const Page page = LayoutFixture(html, image_size, viewport_width);
  return DumpBoxTree(page.document, page.boxes);
}

void ExpectDump(const std::string& html, std::optional<ReplacedSize> image_size,
                float viewport_width, const char* expected, const char* description) {
  const std::string actual = DumpFixture(html, image_size, viewport_width);
  const bool matches = actual == expected;
  if (!matches) {
    std::fprintf(stderr, "\nreplaced test: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n",
                 description, html.c_str(), expected, actual.c_str());
  }
  toy_test::Check(matches, description);
}

// No entry for the image: it is an ordinary empty inline element and produces no
// box at all (the M5 behavior is untouched).
constexpr const char* kNoImageBox =
    R"boxes(<html> block x=0 y=0 width=800 height=48
  <body> block x=8 y=8 width=784 height=32
    <p> block x=8 y=24 width=784 height=0
)boxes";

// An intrinsic 16x8 image with no CSS size: the replaced box is 16x8, sets the
// line height (8) and is the only box on its line.
void TestIntrinsicSize() {
  ExpectDump("<p><img></p>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=56
  <body> block x=8 y=8 width=784 height=40
    <p> block x=8 y=24 width=784 height=8
      <img> inline x=8 y=24 width=16 height=8
)boxes",
             "an intrinsic-sized image becomes an inline box with that size");

  // A taller image raises the line height, and the <p> content height follows.
  ExpectDump("<p><img></p>", ReplacedSize{16.0f, 20.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=68
  <body> block x=8 y=8 width=784 height=52
    <p> block x=8 y=24 width=784 height=20
      <img> inline x=8 y=24 width=16 height=20
)boxes",
             "an image taller than the font raises the line height");
}

// Both CSS sides specified: they win over the intrinsic size and do not
// preserve the intrinsic aspect ratio (explicit sizes are explicit).
void TestCssWidthAndHeight() {
  ExpectDump("<p><img style=\"width:20px; height:30px\"></p>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=78
  <body> block x=8 y=8 width=784 height=62
    <p> block x=8 y=24 width=784 height=30
      <img> inline x=8 y=24 width=20 height=30
)boxes",
             "CSS width and height override the intrinsic size");
}

// Only CSS width: the intrinsic aspect ratio supplies the height.
void TestCssWidthOnlyAspectRatio() {
  ExpectDump("<p><img style=\"width:40px\"></p>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=68
  <body> block x=8 y=8 width=784 height=52
    <p> block x=8 y=24 width=784 height=20
      <img> inline x=8 y=24 width=40 height=20
)boxes",
             "a width-only image keeps the intrinsic aspect ratio");

  // A portrait intrinsic size: 20 x 16/8 == 40.
  ExpectDump("<p><img style=\"width:20px\"></p>", ReplacedSize{8.0f, 16.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=88
  <body> block x=8 y=8 width=784 height=72
    <p> block x=8 y=24 width=784 height=40
      <img> inline x=8 y=24 width=20 height=40
)boxes",
             "the aspect ratio comes from the intrinsic size, not from the CSS width");
}

// Only CSS height: the intrinsic aspect ratio supplies the width.
void TestCssHeightOnlyAspectRatio() {
  ExpectDump("<p><img style=\"height:20px\"></p>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=68
  <body> block x=8 y=8 width=784 height=52
    <p> block x=8 y=24 width=784 height=20
      <img> inline x=8 y=24 width=40 height=20
)boxes",
             "a height-only image keeps the intrinsic aspect ratio");
}

// An image is unbreakable: it moves to the next line when it does not fit after
// the text, and a pending separator is dropped with the wrapped line.
void TestWrapsToNextLine() {
  ExpectDump("<p>aa <img></p>", ReplacedSize{40.0f, 10.0f}, 56.0f,
             R"boxes(<html> block x=0 y=0 width=56 height=74
  <body> block x=8 y=8 width=40 height=58
    <p> block x=8 y=24 width=40 height=26
      text "aa" x=8 y=24 width=16 height=16
      <img> inline x=8 y=40 width=40 height=10
)boxes",
             "an image that does not fit after a word wraps as a whole to the next line");

  // Like an overlong first word, an image alone on an empty line may overflow.
  ExpectDump("<p><img></p>", ReplacedSize{100.0f, 10.0f}, 40.0f,
             R"boxes(<html> block x=0 y=0 width=40 height=58
  <body> block x=8 y=8 width=24 height=42
    <p> block x=8 y=24 width=24 height=10
      <img> inline x=8 y=24 width=100 height=10
)boxes",
             "a lone overlong image overflows its line instead of shrinking");
}

// Line boxes share the line height, but the replaced box is only top-aligned and
// keeps its own height: a text box grows to a taller image, an image keeps its
// height next to taller text.
void TestLineHeightAndTopAlignment() {
  ExpectDump("<p><img>a</p>", ReplacedSize{40.0f, 40.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=88
  <body> block x=8 y=8 width=784 height=72
    <p> block x=8 y=24 width=784 height=40
      <img> inline x=8 y=24 width=40 height=40
      text "a" x=48 y=24 width=8 height=40
)boxes",
             "a tall image raises the line height and the text box height follows");

  ExpectDump("<p><img>a</p>", ReplacedSize{40.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      <img> inline x=8 y=24 width=40 height=8
      text "a" x=48 y=24 width=8 height=16
)boxes",
             "a short image keeps its own height and stays top-aligned on a taller line");
}

// An image inside inline elements nests its box under their fragments, which
// grow to the line height like any other inline fragment.
void TestImageInsideInlineElement() {
  ExpectDump("<p><span><img></span></p>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=56
  <body> block x=8 y=8 width=784 height=40
    <p> block x=8 y=24 width=784 height=8
      <span> inline x=8 y=24 width=16 height=8
        <img> inline x=8 y=24 width=16 height=8
)boxes",
             "an image inside an inline element is a child of that element's fragment");

  // The inline ancestor opening and closing around the image is materialized by
  // the same path that places a word, so its padding applies normally.
  ExpectDump("<p><span style=\"padding:0 4px\"><img></span></p>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=56
  <body> block x=8 y=8 width=784 height=40
    <p> block x=8 y=24 width=784 height=8
      <span> inline x=8 y=24 width=24 height=8
        <img> inline x=12 y=24 width=16 height=8
)boxes",
             "inline ancestor padding surrounds the image like it surrounds a word");
}

// Two images in a row: the whitespace between them becomes one separator.
void TestTwoImagesWithSeparator() {
  ExpectDump("<p><img> <img></p>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=56
  <body> block x=8 y=8 width=784 height=40
    <p> block x=8 y=24 width=784 height=8
      <img> inline x=8 y=24 width=16 height=8
      <img> inline x=32 y=24 width=16 height=8
)boxes",
             "two images are separated by one collapsed space");
}

// An inline run that contains only a replaced box still produces boxes, so in
// mixed block/inline content it is wrapped in an anonymous block like a text
// run (RunProducesBoxes must know about replaced items).
void TestImageOnlyRunCreatesAnonymousBlock() {
  ExpectDump("<div><p>x</p><img></div>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=72
  <body> block x=8 y=8 width=784 height=56
    <div> block x=8 y=8 width=784 height=56
      <p> block x=8 y=24 width=784 height=16
        text "x" x=8 y=24 width=8 height=16
      anonymous block x=8 y=56 width=784 height=8
        <img> inline x=8 y=56 width=16 height=8
)boxes",
             "an image-only inline run after a block is wrapped in an anonymous block");
}

// No entry and non-positive entries are the old behavior: no replaced box.
void TestNoEntryFallback() {
  ExpectDump("<p><img></p>", std::nullopt, 800.0f, kNoImageBox,
             "an image without an intrinsic-size entry is not a replaced element");
  ExpectDump("<p><img></p>", ReplacedSize{0.0f, 8.0f}, 800.0f, kNoImageBox,
             "a zero intrinsic width is not a replaced element");
  ExpectDump("<p><img></p>", ReplacedSize{16.0f, 0.0f}, 800.0f, kNoImageBox,
             "a zero intrinsic height is not a replaced element");
  ExpectDump("<p><img></p>", ReplacedSize{-16.0f, 8.0f}, 800.0f, kNoImageBox,
             "a negative intrinsic side is not a replaced element");
}

// A CSS size of 0 is specified, so the replaced box exists with a zero side.
void TestCssZeroSize() {
  ExpectDump("<p><img style=\"width:0px\"></p>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=48
  <body> block x=8 y=8 width=784 height=32
    <p> block x=8 y=24 width=784 height=0
      <img> inline x=8 y=24 width=0 height=0
)boxes",
             "a specified zero width keeps the aspect ratio and gives a zero-size replaced box");
}

// display:none stays hidden; display:block is not supported for replaced
// elements and lays out as an ordinary empty block (documented MVP limit).
void TestDisplayNoneAndBlock() {
  ExpectDump("<p><img style=\"display:none\"></p>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=48
  <body> block x=8 y=8 width=784 height=32
    <p> block x=8 y=24 width=784 height=0
)boxes",
             "a display:none image with an entry stays hidden");

  ExpectDump("<p><img style=\"display:block\"></p>", ReplacedSize{16.0f, 8.0f}, 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=48
  <body> block x=8 y=8 width=784 height=32
    <p> block x=8 y=24 width=784 height=0
      <img> block x=8 y=24 width=784 height=0
)boxes",
             "display:block replaced elements fall through to the empty-block MVP path");
}

// The replaced box is a normal box for hit-testing: half-open rectangle, later
// in pre-order than its block ancestors.
void TestHitTest() {
  const Page page = LayoutFixture("<p><img></p>", ReplacedSize{16.0f, 8.0f});
  BoxId image_box = kInvalidBox;
  for (std::size_t index = 0; index < page.boxes.size(); ++index) {
    const BoxId id = static_cast<BoxId>(index);
    const toy::layout::Box& box = page.boxes.Get(id);
    if (box.kind == BoxKind::kInline && box.node != toy::dom::kInvalidNode &&
        page.document.Get(box.node).name == "img") {
      image_box = id;
    }
  }
  toy_test::Check(image_box != kInvalidBox, "the replaced element produces an inline box");
  if (image_box == kInvalidBox) {
    return;
  }
  const toy::layout::Box& image = page.boxes.Get(image_box);
  toy_test::Check(
      image.x == 8.0f && image.y == 24.0f && image.width == 16.0f && image.height == 8.0f,
      "the replaced box has the intrinsic size at its line position");
  toy_test::Check(HitTest(page.boxes, 10.0f, 26.0f) == image_box,
                  "a point inside the image hits the image box, not the containing block");
  toy_test::Check(HitTest(page.boxes, 23.9f, 31.9f) == image_box,
                  "a point on the image's bottom-right interior hits the image box");
  toy_test::Check(HitTest(page.boxes, 8.0f, 24.0f) == image_box,
                  "the image box's top-left edge is inside the image box");
  const BoxId right = HitTest(page.boxes, 24.0f, 26.0f);
  toy_test::Check(right != image_box && right != kInvalidBox,
                  "x == right edge is outside the half-open image box");
  const BoxId bottom = HitTest(page.boxes, 10.0f, 32.0f);
  toy_test::Check(bottom != image_box && bottom != kInvalidBox,
                  "y == bottom edge is outside the half-open image box");

  const Page fallback = LayoutFixture("<p><img></p>", std::nullopt);
  bool has_image_box = false;
  for (std::size_t index = 0; index < fallback.boxes.size(); ++index) {
    const toy::layout::Box& box = fallback.boxes.Get(static_cast<BoxId>(index));
    if (box.kind == BoxKind::kInline && box.node != toy::dom::kInvalidNode &&
        fallback.document.Get(box.node).name == "img") {
      has_image_box = true;
    }
  }
  toy_test::Check(!has_image_box, "without an entry no image box exists to hit");
}

// A replaced box deep inside 10,000 nested inline elements lays out iteratively:
// the box count and dump stay linear and the indent stays capped.
void TestDeepNestingIsIterative() {
  constexpr int kLevels = 10000;
  std::string html;
  html.reserve(static_cast<std::size_t>(kLevels) * 6 + 5);
  for (int level = 0; level < kLevels; ++level) {
    html.append("<span>");
  }
  html.append("<img>");

  const Page page = LayoutFixture(html, ReplacedSize{16.0f, 8.0f});
  // html + body + 10,000 span fragments + the image box.
  toy_test::Check(page.boxes.size() == static_cast<std::size_t>(kLevels) + 3,
                  "deeply nested inline elements and an image produce one box each");

  const std::string dump = DumpBoxTree(page.document, page.boxes);
  std::size_t lines = 0;
  std::size_t max_indent = 0;
  std::size_t line_start = 0;
  while (line_start < dump.size()) {
    std::size_t indent = 0;
    while (line_start + indent < dump.size() && dump[line_start + indent] == ' ') {
      ++indent;
    }
    max_indent = indent > max_indent ? indent : max_indent;
    ++lines;
    const std::size_t line_end = dump.find('\n', line_start);
    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
  }
  toy_test::Check(lines == page.boxes.size(), "the dump has exactly one line per box");
  toy_test::Check(max_indent <= 128, "box dump indentation is capped at 64 levels");
  toy_test::Check(dump.find("<img> inline x=8 y=8 width=16 height=8") != std::string::npos,
                  "the deeply nested image keeps its intrinsic geometry");
  toy_test::Check(ContentHeight(page.boxes) == 24.0f,
                  "content height of a deeply nested page with one replaced box stays finite");
}

}  // namespace

int main() {
  TestIntrinsicSize();
  TestCssWidthAndHeight();
  TestCssWidthOnlyAspectRatio();
  TestCssHeightOnlyAspectRatio();
  TestWrapsToNextLine();
  TestLineHeightAndTopAlignment();
  TestImageInsideInlineElement();
  TestTwoImagesWithSeparator();
  TestImageOnlyRunCreatesAnonymousBlock();
  TestNoEntryFallback();
  TestCssZeroSize();
  TestDisplayNoneAndBlock();
  TestHitTest();
  TestDeepNestingIsIterative();
  return toy_test::Finish("layout replaced test");
}
