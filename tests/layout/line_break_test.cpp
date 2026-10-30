// Line breaking, inline fragmentation and <br> handling.

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
    std::fprintf(stderr, "\nline break test: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n",
                 description, html.c_str(), expected, actual.c_str());
  }
  toy_test::Check(matches, description);
}

// Viewport 40: body content width 40 - 8 - 8 = 24, the p content width is 24.
// The second word does not fit and starts a new line; the first word is allowed
// to overflow.
void TestWordWrap() {
  ExpectDump("<p>aaaa bbbb</p>", 40.0f,
             R"boxes(<html> block x=0 y=0 width=40 height=80
  <body> block x=8 y=8 width=24 height=64
    <p> block x=8 y=24 width=24 height=32
      text "aaaa" x=8 y=24 width=32 height=16
      text "bbbb" x=8 y=40 width=32 height=16
)boxes",
             "a word that does not fit starts a new line, a single word may overflow");
}

// Viewport 56: p content width 40 and 16 + 8 + 16 == 40 fits exactly, so the
// line is not broken (the check is strictly greater than the line width).
void TestExactFit() {
  ExpectDump("<p>aa bb</p>", 56.0f,
             R"boxes(<html> block x=0 y=0 width=56 height=64
  <body> block x=8 y=8 width=40 height=48
    <p> block x=8 y=24 width=40 height=16
      text "aa" x=8 y=24 width=16 height=16
      text "bb" x=32 y=24 width=16 height=16
)boxes",
             "a line that fits exactly is not broken");
}

// A single overlong first word overflows the line and stays on one line.
void TestOverlongFirstWord() {
  ExpectDump("<p>abcdefgh</p>", 40.0f,
             R"boxes(<html> block x=0 y=0 width=40 height=64
  <body> block x=8 y=8 width=24 height=48
    <p> block x=8 y=24 width=24 height=16
      text "abcdefgh" x=8 y=24 width=64 height=16
)boxes",
             "an overlong first word overflows the line");
}

// An inline element split across two lines yields two sibling fragment boxes
// with the same node id, not nested fragments.
void TestInlineSplitAcrossLines() {
  ExpectDump("<p><span>aaaa bbbb</span></p>", 56.0f,
             R"boxes(<html> block x=0 y=0 width=56 height=80
  <body> block x=8 y=8 width=40 height=64
    <p> block x=8 y=24 width=40 height=32
      <span> inline x=8 y=24 width=32 height=16
        text "aaaa" x=8 y=24 width=32 height=16
      <span> inline x=8 y=40 width=32 height=16
        text "bbbb" x=8 y=40 width=32 height=16
)boxes",
             "an inline element split across lines produces sibling fragments");
}

// <br> flushes the current line.
void TestBreakFlushesLine() {
  ExpectDump("<p>a<br>b</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=80
  <body> block x=8 y=8 width=784 height=64
    <p> block x=8 y=24 width=784 height=32
      text "a" x=8 y=24 width=8 height=16
      text "b" x=8 y=40 width=8 height=16
)boxes",
             "<br> flushes the current line");
}

// <p><br></p> is one empty line; a trailing empty line is dropped after a
// non-empty line; two <br>s produce two empty lines.
void TestBreakEmptyLines() {
  ExpectDump("<p><br></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
)boxes",
             "a lone <br> produces one empty line");

  ExpectDump("<p>a<br></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "a" x=8 y=24 width=8 height=16
)boxes",
             "the empty line after a trailing <br> is dropped");

  ExpectDump("<p><br><br></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=80
  <body> block x=8 y=8 width=784 height=64
    <p> block x=8 y=24 width=784 height=32
)boxes",
             "two <br>s produce two empty lines");
}

// An empty line forced by <br> uses the line height of the br's font size.
void TestBreakEmptyLineFontSize() {
  ExpectDump("<p><br style=\"font-size:32px\"></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=80
  <body> block x=8 y=8 width=784 height=64
    <p> block x=8 y=24 width=784 height=32
)boxes",
             "an empty <br> line uses the br font size line height");
}

// A <br> inside an inline element closes its current fragment and starts a new
// fragment for the remaining content.
void TestBreakSplitsInlineElement() {
  ExpectDump("<p><span>a<br>b</span></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=80
  <body> block x=8 y=8 width=784 height=64
    <p> block x=8 y=24 width=784 height=32
      <span> inline x=8 y=24 width=8 height=16
        text "a" x=8 y=24 width=8 height=16
      <span> inline x=8 y=40 width=8 height=16
        text "b" x=8 y=40 width=8 height=16
)boxes",
             "a <br> inside an inline element starts a new fragment");
}

// A <br> ends the last fragment of an inline element before the element closes:
// the last fragment's border box still includes its padding-right.
void TestBreakLastFragmentPadding() {
  ExpectDump("<p><span style=\"padding:0 4px\">abc<br></span></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      <span> inline x=8 y=24 width=32 height=16
        text "abc" x=12 y=24 width=24 height=16
)boxes",
             "padding-right applies to an inline fragment ended by a <br>");
}

// The padding of an inline element applies to its first fragment even when the
// element starts with a <br>.
void TestBreakBeforeInlineContent() {
  ExpectDump("<p><span style=\"padding:0 4px\"><br>a</span></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=80
  <body> block x=8 y=8 width=784 height=64
    <p> block x=8 y=24 width=784 height=32
      <span> inline x=8 y=40 width=16 height=16
        text "a" x=12 y=40 width=8 height=16
)boxes",
             "inline padding applies on the line where the first fragment content lands");
}

// All boxes on one line share the line height, the maximum over the items.
void TestLineHeightIsMaxOfItems() {
  ExpectDump("<p>a<span style=\"font-size:32px\">b</span></p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=80
  <body> block x=8 y=8 width=784 height=64
    <p> block x=8 y=24 width=784 height=32
      text "a" x=8 y=24 width=8 height=32
      <span> inline x=16 y=24 width=16 height=32
        text "b" x=16 y=24 width=16 height=32
)boxes",
             "all boxes of a line share the maximum item line height");
}

// Leading, trailing and repeated ASCII whitespace collapses: no separator at a
// line start, none at the end and never two in a row.
void TestWhitespaceCollapse() {
  ExpectDump("<p>  a   b  </p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "a" x=8 y=24 width=8 height=16
      text "b" x=24 y=24 width=8 height=16
)boxes",
             "leading/trailing/repeated whitespace collapses to one separator");
}

// Horizontal inline padding applies only at the outer edges of a multi-line
// inline element: the first fragment gets padding-left, the last padding-right.
void TestSplitInlineOuterPadding() {
  ExpectDump("<p><span style=\"padding:0 4px\">aaaa bbbb</span></p>", 56.0f,
             R"boxes(<html> block x=0 y=0 width=56 height=80
  <body> block x=8 y=8 width=40 height=64
    <p> block x=8 y=24 width=40 height=32
      <span> inline x=8 y=24 width=36 height=16
        text "aaaa" x=12 y=24 width=32 height=16
      <span> inline x=8 y=40 width=36 height=16
        text "bbbb" x=8 y=40 width=32 height=16
)boxes",
             "a split inline element applies padding-left only to the first fragment and "
             "padding-right only to the last");
}

// <br> with display:none is hidden and does not break; any other display value
// still forces a break (rule 9 wins over rule 2).
void TestBreakDisplayValues() {
  ExpectDump("<p>a<br style=\"display:none\">b</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "a" x=8 y=24 width=8 height=16
      text "b" x=16 y=24 width=8 height=16
)boxes",
             "a display:none <br> is skipped entirely");

  ExpectDump("<p>a<br style=\"display:block\">b</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=80
  <body> block x=8 y=8 width=784 height=64
    <p> block x=8 y=24 width=784 height=32
      text "a" x=8 y=24 width=8 height=16
      text "b" x=8 y=40 width=8 height=16
)boxes",
             "a <br> with another display value still forces a break");
}

}  // namespace

int main() {
  TestWordWrap();
  TestExactFit();
  TestOverlongFirstWord();
  TestInlineSplitAcrossLines();
  TestBreakFlushesLine();
  TestBreakEmptyLines();
  TestBreakEmptyLineFontSize();
  TestBreakSplitsInlineElement();
  TestBreakLastFragmentPadding();
  TestBreakBeforeInlineContent();
  TestLineHeightIsMaxOfItems();
  TestWhitespaceCollapse();
  TestSplitInlineOuterPadding();
  TestBreakDisplayValues();
  return toy_test::Finish("layout line break test");
}
