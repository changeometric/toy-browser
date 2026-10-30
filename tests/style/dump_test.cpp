// Exact dump tests for the computed style tree.

#include <cstddef>
#include <cstdio>
#include <string>

#include "toy/dom/node.h"
#include "toy/style/computed_style.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "style dump test failed: %s\n", description);
    ++g_failures;
  }
}

void ExpectDump(const char* html, const char* expected, const char* description) {
  const toy::dom::Document document = toy::dom::ParseDocument(html);
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  const std::string actual = toy::style::DumpStyleTree(document, styles);
  if (actual == expected) {
    return;
  }
  std::fprintf(stderr, "style dump test failed: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n",
               description, html, expected, actual.c_str());
  ++g_failures;
}

void TestDumpCases() {
  // A minimal page: only element nodes are dumped, with DOM-depth indentation.
  ExpectDump(
      "<p>hi</p>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
      <p> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=16px margin-right=0px margin-bottom=16px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
)dom",
      "a minimal page dumps only element nodes");

  // Author rules, inline overrides, shorthands, em widths and opaque colors.
  ExpectDump(
      "<!doctype html><html><head><style>p { color: red; font-size: 20px; margin: 2px 4px; } "
      ".wide { width: 10em; padding: 1px; background-color: #abc; }</style></head>"
      "<body><div class=\"wide\"><p id=\"t\" style=\"margin-left: 3px\">x</p></div></body>"
      "</html>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
      <style> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
      <div> display=block color=#000000 background-color=#aabbcc font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=1px padding-right=1px padding-bottom=1px padding-left=1px width=160px height=auto
        <p> display=block color=#ff0000 background-color=transparent font-size=20px font-weight=400 font-style=normal margin-top=2px margin-right=4px margin-bottom=2px margin-left=3px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
)dom",
      "author rules, inline style and em lengths are dumped");

  // Lengths are rounded to the nearest integer; #rgb colors are expanded.
  ExpectDump(
      "<div style=\"width: 10.4px; margin-top: 2.5px; margin-bottom: 2.4px; "
      "font-size: 15.5px; color: #123; background-color: #abc\">x</div>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
      <div> display=block color=#112233 background-color=#aabbcc font-size=16px font-weight=400 font-style=normal margin-top=3px margin-right=0px margin-bottom=2px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=10px height=auto
)dom",
      "lengths are rounded and colors normalized");

  // Nested elements at normal depth keep their exact, uncapped indentation.
  ExpectDump(
      "<div><div><p>x</p></div></div>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
      <div> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
        <div> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
          <p> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=16px margin-right=0px margin-bottom=16px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
)dom",
      "nested elements at normal depth keep exact indentation");

  // Inline elements and the UA link color.
  ExpectDump(
      "<a href=\"/x\">link</a>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
      <a> display=inline color=#0000ee background-color=transparent font-size=16px font-weight=400 font-style=normal margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto
)dom",
      "inline elements and the UA link color are dumped");
}

// Regression: the capped indent keeps the dump linear for deep nesting.
void TestDeepNestingDump() {
  std::string html;
  html.reserve(10000 * 5);
  for (int level = 0; level < 10000; ++level) {
    html.append("<div>");
  }
  const toy::dom::Document document = toy::dom::ParseDocument(html);
  Check(document.size() > 10000, "a deeply nested document is parsed");
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  const std::string dump = toy::style::DumpStyleTree(document, styles);
  Check(!dump.empty(), "a deeply nested style tree still dumps");
  Check(dump.back() == '\n', "a deeply nested dump ends with one newline");
  // About 10,000 style lines of roughly 250 bytes each: ~3.8 MB with the cap.
  // Without the cap the quadratic indentation would add ~100 MB, so this bound
  // fails loudly if the cap is removed while staying close to the real size.
  Check(dump.size() < 6U * 1024U * 1024U, "deep nesting stays far below the uncapped size");

  std::size_t max_indent = 0;
  std::size_t line_start = 0;
  while (line_start < dump.size()) {
    std::size_t indent = 0;
    while (line_start + indent < dump.size() && dump[line_start + indent] == ' ') {
      ++indent;
    }
    max_indent = indent > max_indent ? indent : max_indent;
    const std::size_t line_end = dump.find('\n', line_start);
    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
  }
  Check(max_indent <= 128, "style dump indentation is capped at 64 levels");
  Check(max_indent == 128, "a 10,000 level tree reaches the indentation cap");
}

}  // namespace

int main() {
  TestDumpCases();
  TestDeepNestingDump();
  if (g_failures != 0) {
    std::fprintf(stderr, "style dump test: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("style dump test passed\n");
  return 0;
}
