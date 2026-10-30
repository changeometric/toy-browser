// Regression: 10,000 nested block elements must lay out without C++ recursion,
// dump one line per box with the indent capped at 64 levels, and stay linear.

#include <cstddef>
#include <cstdio>
#include <string>

#include "fake_measure.h"
#include "support/check.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/style/computed_style.h"

namespace {

void TestDeepNesting() {
  constexpr int kLevels = 10000;
  std::string html;
  html.reserve(static_cast<std::size_t>(kLevels) * 5);
  for (int level = 0; level < kLevels; ++level) {
    html.append("<div>");
  }

  const toy::dom::Document document = toy::dom::ParseDocument(html);
  toy_test::Check(document.size() > static_cast<std::size_t>(kLevels),
                  "a deeply nested document is parsed");
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  toy::layout::LayoutOptions options;
  options.viewport_width_px = 800.0f;
  const toy::layout::BoxTree boxes =
      toy::layout::LayoutDocument(document, styles, options, toy::layout::test::MeasureFakeText);

  // html + body + 10,000 divs; head is display:none and produces nothing.
  toy_test::Check(boxes.size() == static_cast<std::size_t>(kLevels) + 2,
                  "every element produces a box");

  const std::string dump = toy::layout::DumpBoxTree(document, boxes);
  toy_test::Check(!dump.empty(), "a deeply nested box tree still dumps");
  toy_test::Check(dump.back() == '\n', "a deeply nested dump ends with one newline");

  std::size_t lines = 0;
  for (const char character : dump) {
    if (character == '\n') {
      ++lines;
    }
  }
  toy_test::Check(lines == boxes.size(), "the dump has exactly one line per box");

  const std::string expected_first_line = "<html> block x=0 y=0 width=800 height=16\n";
  toy_test::Check(dump.compare(0, expected_first_line.size(), expected_first_line) == 0,
                  "the root box of a deeply nested page has its expected geometry");
  const std::string expected_div_line = "<div> block x=8 y=8 width=784 height=0\n";
  toy_test::Check(dump.find(expected_div_line) != std::string::npos,
                  "a nested div box of a deeply nested page has its expected geometry");

  // About 10,000 div lines of ~40 bytes plus the capped indentation
  // (~1.7 MB). Without the cap the quadratic indentation would add ~100 MB, so
  // this bound fails loudly if the cap is removed while staying close to the
  // real size.
  toy_test::Check(dump.size() < 6U * 1024U * 1024U,
                  "deep nesting stays far below the uncapped size");

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
  toy_test::Check(max_indent <= 128, "box dump indentation is capped at 64 levels");
  toy_test::Check(max_indent == 128, "a 10,000 level tree reaches the indentation cap");
}

}  // namespace

int main() {
  TestDeepNesting();
  return toy_test::Finish("layout deep nesting test");
}
