// Inline layout: fragments, horizontal margins/padding, nesting, rule-10 splits.

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
    std::fprintf(stderr, "\ninline test: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n", description,
                 html.c_str(), expected, actual.c_str());
  }
  toy_test::Check(matches, description);
}

// Nested inline elements nest their fragment boxes; the text box is a child of
// the innermost fragment.
void TestNestedInline() {
  ExpectDump("<p><b><i>x</i></b></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      <b> inline x=8 y=24 width=8 height=16
        <i> inline x=8 y=24 width=8 height=16
          text "x" x=8 y=24 width=8 height=16
)boxes",
             "nested inline elements nest their fragment boxes");
}

// Horizontal margin/padding apply at the outer edges: margin-left + padding-left
// before the first content, padding-right + margin-right after the last.
void TestInlineMarginPadding() {
  ExpectDump(
      "<p><span style=\"margin-left:3px; padding-left:5px; padding-right:7px; "
      "margin-right:9px\">ab</span>c</p>",
      800.0f,
      R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      <span> inline x=11 y=24 width=28 height=16
        text "ab" x=16 y=24 width=16 height=16
      text "c" x=48 y=24 width=8 height=16
)boxes",
      "inline margin-left/padding-left precede and padding-right/margin-right follow the content");
}

// An inline element with no content produces no box at all; the single space
// between the words collapses to one advance.
void TestEmptyInlineProducesNoBox() {
  ExpectDump("<p>a <span></span><b>b</b></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "a" x=8 y=24 width=8 height=16
      <b> inline x=24 y=24 width=8 height=16
        text "b" x=24 y=24 width=8 height=16
)boxes",
             "an empty inline element produces no box and spaces collapse");
}

// Whitespace-only inline content produces no box but still separates words
// across inline element boundaries.
void TestWhitespaceOnlyInline() {
  ExpectDump("<p>a<span> </span>b</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "a" x=8 y=24 width=8 height=16
      text "b" x=24 y=24 width=8 height=16
)boxes",
             "whitespace-only inline content produces no box but keeps the separator");
}

// Vertical margins/padding and width/height of inline boxes are ignored.
void TestInlineVerticalPropertiesIgnored() {
  ExpectDump(
      "<p><span style=\"padding-top:50px; padding-bottom:50px; margin-top:30px\">x</span></p>",
      800.0f,
      R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      <span> inline x=8 y=24 width=8 height=16
        text "x" x=8 y=24 width=8 height=16
)boxes",
      "vertical inline margins/padding are ignored, the line stays one line high");
}

// Inline margins may be negative.
void TestNegativeInlineMargin() {
  ExpectDump("<p><span style=\"margin-left:-3px\">ab</span>c</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      <span> inline x=5 y=24 width=16 height=16
        text "ab" x=5 y=24 width=16 height=16
      text "c" x=21 y=24 width=8 height=16
)boxes",
             "a negative inline margin-left moves the fragment left");
}

// Rule 10: a block inside an inline element is laid as a block child of the
// nearest block container; the inline element continues as new fragments.
void TestBlockInsideInlineElement() {
  ExpectDump("<p><span>a<div>x</div>b</span></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=96
  <body> block x=8 y=8 width=784 height=80
    <p> block x=8 y=24 width=784 height=48
      anonymous block x=8 y=24 width=784 height=16
        <span> inline x=8 y=24 width=8 height=16
          text "a" x=8 y=24 width=8 height=16
      <div> block x=8 y=40 width=784 height=16
        text "x" x=8 y=40 width=8 height=16
      anonymous block x=8 y=56 width=784 height=16
        <span> inline x=8 y=56 width=8 height=16
          text "b" x=8 y=56 width=8 height=16
)boxes",
             "a block inside an inline element splits the inline run");
}

// The first fragment's padding-left applies to the first content after the
// block child when the inline element started before it.
void TestBlockInsideInlineFirstFragmentPadding() {
  ExpectDump("<p><span style=\"padding-left:4px\"><div>x</div>y</span></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=80
  <body> block x=8 y=8 width=784 height=64
    <p> block x=8 y=24 width=784 height=32
      <div> block x=8 y=24 width=784 height=16
        text "x" x=8 y=24 width=8 height=16
      anonymous block x=8 y=40 width=784 height=16
        <span> inline x=8 y=40 width=12 height=16
          text "y" x=12 y=40 width=8 height=16
)boxes",
             "the first fragment's padding-left applies after a block child");
}

// An empty inline element with horizontal margins/padding still produces no box
// and no advance at all.
void TestEmptyInlineMarginsDoNotAdvance() {
  ExpectDump("<p><span style=\"margin-left:10px; padding:0 5px\"></span>x</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "x" x=8 y=24 width=8 height=16
)boxes",
             "an empty inline element contributes no box and no horizontal advance");
}

// A whitespace-only text node alone in a block produces no boxes and no lines.
void TestWhitespaceOnlyTextProducesNothing() {
  ExpectDump("<p>   </p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=48
  <body> block x=8 y=8 width=784 height=32
    <p> block x=8 y=24 width=784 height=0
)boxes",
             "a whitespace-only text node produces no boxes and no lines");
}

}  // namespace

int main() {
  TestNestedInline();
  TestInlineMarginPadding();
  TestEmptyInlineProducesNoBox();
  TestWhitespaceOnlyInline();
  TestInlineVerticalPropertiesIgnored();
  TestNegativeInlineMargin();
  TestBlockInsideInlineElement();
  TestBlockInsideInlineFirstFragmentPadding();
  TestEmptyInlineMarginsDoNotAdvance();
  TestWhitespaceOnlyTextProducesNothing();
  return toy_test::Finish("layout inline test");
}
