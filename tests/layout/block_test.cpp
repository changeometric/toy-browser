// Block layout: stacking, auto/fixed sizes, margins, padding, anonymous blocks.

#include <cstdio>
#include <string>

#include "fake_measure.h"
#include "support/check.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/style/computed_style.h"

namespace {

using toy::dom::Document;
using toy::layout::BoxTree;
using toy::layout::DumpBoxTree;
using toy::layout::LayoutDocument;
using toy::layout::LayoutOptions;

std::string DumpPage(const std::string& html, float viewport_width) {
  const Document document = toy::dom::ParseDocument(html);
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  LayoutOptions options;
  options.viewport_width_px = viewport_width;
  const BoxTree boxes =
      LayoutDocument(document, styles, options, toy::layout::test::MeasureFakeText);
  return DumpBoxTree(document, boxes);
}

void ExpectDump(const std::string& html, float viewport_width, const char* expected,
                const char* description) {
  const std::string actual = DumpPage(html, viewport_width);
  const bool matches = actual == expected;
  if (!matches) {
    std::fprintf(stderr, "\nblock test: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n", description,
                 html.c_str(), expected, actual.c_str());
  }
  toy_test::Check(matches, description);
}

// The default UA margins of html/body/p and the auto widths derived from the
// viewport (800 - body margins - p margins).
void TestMinimalPage() {
  ExpectDump("<p>hi</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "hi" x=8 y=24 width=16 height=16
)boxes",
             "a minimal page stacks html/body/p with UA margins");
}

// Fixed width and height plus padding: the border box is content + padding.
void TestFixedSizeAndPadding() {
  ExpectDump("<div style=\"width:100px; height:40px; margin:5px; padding:10px\">x</div>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=86
  <body> block x=8 y=8 width=784 height=70
    <div> block x=13 y=13 width=120 height=60
      text "x" x=23 y=23 width=8 height=16
)boxes",
             "fixed width/height, padding and margins define the border box");
}

// Vertical flow adds margin-top + height + margin-bottom; auto width fills the
// containing content box.
void TestStackedBlocks() {
  ExpectDump(
      "<div style=\"height:10px; margin-top:20px; margin-bottom:20px\"></div>"
      "<div style=\"height:5px\"></div>",
      800.0f,
      R"boxes(<html> block x=0 y=0 width=800 height=71
  <body> block x=8 y=8 width=784 height=55
    <div> block x=8 y=28 width=784 height=10
    <div> block x=8 y=58 width=784 height=5
)boxes",
      "block children stack with their vertical margins");
}

// Padding shrinks the auto content width but is included in the border box;
// the child's containing block is the parent's content box.
void TestNestedPadding() {
  ExpectDump("<div style=\"padding:5px\"><div style=\"height:10px\"></div></div>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=36
  <body> block x=8 y=8 width=784 height=20
    <div> block x=8 y=8 width=784 height=20
      <div> block x=13 y=13 width=774 height=10
)boxes",
             "padding shrinks the auto content width of the child");
}

// Negative margins are used as computed.
void TestNegativeMargin() {
  ExpectDump("<div style=\"margin-top:-5px; height:10px\"></div>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=21
  <body> block x=8 y=8 width=784 height=5
    <div> block x=8 y=3 width=784 height=10
)boxes",
             "a negative margin-top moves the box up and shrinks the parent height");
}

// Mixed block and inline content: each maximal inline run becomes one anonymous
// block, block children stay in flow between them.
void TestAnonymousBlocks() {
  ExpectDump("<div>a<div>b</div>c</div>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <div> block x=8 y=8 width=784 height=48
      anonymous block x=8 y=8 width=784 height=16
        text "a" x=8 y=8 width=8 height=16
      <div> block x=8 y=24 width=784 height=16
        text "b" x=8 y=24 width=8 height=16
      anonymous block x=8 y=40 width=784 height=16
        text "c" x=8 y=40 width=8 height=16
)boxes",
             "mixed block+inline content creates anonymous blocks");
}

// All-inline content lays lines directly in the block box: no anonymous block.
void TestAllInlineNoAnonymousBlock() {
  ExpectDump("<div><span>a</span>b</div>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=32
  <body> block x=8 y=8 width=784 height=16
    <div> block x=8 y=8 width=784 height=16
      <span> inline x=8 y=8 width=8 height=16
        text "a" x=8 y=8 width=8 height=16
      text "b" x=16 y=8 width=8 height=16
)boxes",
             "all-inline content needs no anonymous block");
}

// A fixed height wins over the children's flow height: the content may overflow
// but the parent consumes only its fixed height.
void TestFixedHeightOverflow() {
  ExpectDump("<div style=\"height:5px\"><div style=\"height:20px\"></div></div>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=21
  <body> block x=8 y=8 width=784 height=5
    <div> block x=8 y=8 width=784 height=5
      <div> block x=8 y=8 width=784 height=20
)boxes",
             "a fixed height overrides the auto height, the child overflows");
}

// A whitespace-only text node between two block children produces neither a box
// nor an anonymous block.
void TestWhitespaceBetweenBlocks() {
  ExpectDump("<div><div>a</div> <div>b</div></div>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=48
  <body> block x=8 y=8 width=784 height=32
    <div> block x=8 y=8 width=784 height=32
      <div> block x=8 y=8 width=784 height=16
        text "a" x=8 y=8 width=8 height=16
      <div> block x=8 y=24 width=784 height=16
        text "b" x=8 y=24 width=8 height=16
)boxes",
             "whitespace-only text between block children creates no anonymous block");
}

// End-to-end fixture agreed with the shell owner: author p margin (4px 8px)
// overrides the UA 16px 0, so the p content box is 768 wide at x=16, y=12.
void TestShellFixtureAgreement() {
  ExpectDump(
      "<!doctype html><html><head><style>p { margin: 4px 8px }</style></head>"
      "<body><p>Hello</p></body></html>",
      800.0f,
      R"boxes(<html> block x=0 y=0 width=800 height=40
  <body> block x=8 y=8 width=784 height=24
    <p> block x=16 y=12 width=768 height=16
      text "Hello" x=16 y=12 width=40 height=16
)boxes",
      "the shell owner's fixture expectation is reproduced");
}

}  // namespace

int main() {
  TestMinimalPage();
  TestFixedSizeAndPadding();
  TestStackedBlocks();
  TestNestedPadding();
  TestNegativeMargin();
  TestAnonymousBlocks();
  TestAllInlineNoAnonymousBlock();
  TestFixedHeightOverflow();
  TestWhitespaceBetweenBlocks();
  TestShellFixtureAgreement();
  return toy_test::Finish("layout block test");
}
