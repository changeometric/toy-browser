// Dump format: empty tree, escaping, integer rounding, zero-size boxes, hidden
// subtrees.

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

BoxTree LayoutPage(const Document& document, float viewport_width) {
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  LayoutOptions options;
  options.viewport_width_px = viewport_width;
  return LayoutDocument(document, styles, options, toy::layout::test::MeasureFakeText);
}

std::string DumpHtml(const std::string& html, float viewport_width = 800.0f) {
  const Document document = toy::dom::ParseDocument(html);
  return DumpBoxTree(document, LayoutPage(document, viewport_width));
}

void ExpectDump(const std::string& html, float viewport_width, const char* expected,
                const char* description) {
  const std::string actual = DumpHtml(html, viewport_width);
  const bool matches = actual == expected;
  if (!matches) {
    std::fprintf(stderr, "\ndump test: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n", description,
                 html.c_str(), expected, actual.c_str());
  }
  toy_test::Check(matches, description);
}

// A default-constructed Document has no #document root: the layout is empty.
void TestEmptyDocument() {
  const Document document;
  const toy::style::StyleTree styles;
  const BoxTree boxes =
      LayoutDocument(document, styles, LayoutOptions{}, toy::layout::test::MeasureFakeText);
  toy_test::Check(boxes.root() == toy::layout::kInvalidBox, "an empty document has no root box");
  toy_test::Check(boxes.size() == 0, "an empty document has no boxes");
  toy_test::Check(DumpBoxTree(document, boxes).empty(), "an empty document dumps as empty text");
}

// A parsed empty page still yields html and body boxes, both zero-sized.
void TestEmptyPage() {
  ExpectDump("", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=16
  <body> block x=8 y=8 width=784 height=0
)boxes",
             "a parsed empty page dumps html and body");
}

// Text escaping is identical to dom::DumpDocument: quotes and backslashes.
void TestQuoteAndBackslashEscaping() {
  ExpectDump(R"html(<p>"a\b"</p>)html", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "\"a\\b\"" x=8 y=24 width=40 height=16
)boxes",
             "quotes and backslashes in words are escaped");
}

// C0 bytes other than the whitespace separators are escaped as \u00XX with
// uppercase hex digits; &#1; is a non-whitespace word byte.
void TestControlByteEscaping() {
  ExpectDump("<p>a&#1;b</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "a\u0001b" x=8 y=24 width=24 height=16
)boxes",
             "C0 control bytes in words are escaped as uppercase \\u00XX");
}

// Geometry is rounded to the nearest integer with std::lround (halves away
// from zero): 15.5 advances/line heights round to 16.
void TestIntegerRounding() {
  ExpectDump("<p style=\"font-size:15.5px\">aa</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "aa" x=8 y=24 width=16 height=16
)boxes",
             "fractional geometry is rounded with std::lround");
}

// Zero-size element boxes are still dumped.
void TestZeroSizeBoxes() {
  ExpectDump("<div></div><div style=\"width:0; height:0\"></div>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=16
  <body> block x=8 y=8 width=784 height=0
    <div> block x=8 y=8 width=784 height=0
    <div> block x=8 y=8 width=0 height=0
)boxes",
             "zero-size element boxes are dumped");
}

// display:none subtrees, comments and doctypes produce no boxes; head is hidden
// by the UA style sheet.
void TestHiddenContent() {
  ExpectDump("<!doctype html><!-- ignored --><p style=\"display:none\">x</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=16
  <body> block x=8 y=8 width=784 height=0
)boxes",
             "hidden subtrees, comments and doctypes produce no boxes");
}

// Non-ASCII bytes are word bytes (no Unicode line breaking) and are dumped
// verbatim; the fake measurer counts bytes.
void TestNonAsciiWord() {
  ExpectDump("<p>\xC3\xA9</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "é" x=8 y=24 width=16 height=16
)boxes",
             "non-ASCII bytes are word bytes and are dumped verbatim");
}

}  // namespace

int main() {
  TestEmptyDocument();
  TestEmptyPage();
  TestQuoteAndBackslashEscaping();
  TestControlByteEscaping();
  TestIntegerRounding();
  TestZeroSizeBoxes();
  TestHiddenContent();
  TestNonAsciiWord();
  return toy_test::Finish("layout dump test");
}
