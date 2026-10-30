// Tests for toy::paint::BuildDisplayList: a laid-out page becomes an ordered
// display list of background fills and text draws.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "support/check.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/paint/paint.h"
#include "toy/render/font.h"
#include "toy/render/paint.h"
#include "toy/style/computed_style.h"

namespace {

using toy::dom::Document;
using toy::layout::BoxTree;
using toy::layout::LayoutDocument;
using toy::layout::LayoutOptions;
using toy::layout::TextMetrics;
using toy::paint::BuildDisplayList;
using toy::render::Command;
using toy::render::CommandKind;
using toy::render::DisplayList;

// The real built-in bitmap-font metrics, adapted exactly like shell does for
// layout. Paint never depends on them; the test pipeline needs a measure
// callback to lay the page out.
TextMetrics MeasureWithBitmapFont(std::string_view text_utf8, float font_size_px) {
  return TextMetrics{toy::render::MeasureTextWidth(text_utf8, font_size_px),
                     toy::render::LineHeightPx(font_size_px)};
}

// Full pipeline for a fixture page at the default 800 px viewport.
DisplayList BuildPage(const std::string& html) {
  const Document document = toy::dom::ParseDocument(html);
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  LayoutOptions options;
  options.viewport_width_px = 800.0f;
  const BoxTree boxes = LayoutDocument(document, styles, options, MeasureWithBitmapFont);
  return BuildDisplayList(document, styles, boxes);
}

void ExpectSize(const DisplayList& list, std::size_t expected, const char* description) {
  if (list.size() != expected) {
    std::fprintf(stderr, "paint display list test: expected %zu command(s), got %zu: %s\n",
                 expected, list.size(), description);
  }
  toy_test::Check(list.size() == expected, description);
}

void ExpectFillRect(const DisplayList& list, std::size_t index, int x, int y, int width, int height,
                    std::uint32_t color, const char* description) {
  if (index >= list.size()) {
    std::fprintf(stderr, "paint display list test: missing command %zu: %s\n", index, description);
    toy_test::Check(false, description);
    return;
  }
  const Command& command = list.commands()[index];
  const bool matches = command.kind == CommandKind::kFillRect && command.x == x && command.y == y &&
                       command.width == width && command.height == height && command.color == color;
  if (!matches) {
    std::fprintf(stderr,
                 "paint display list test: command %zu is not the expected FillRect "
                 "x=%d y=%d width=%d height=%d color=0x%08X: %s\n",
                 index, command.x, command.y, command.width, command.height,
                 static_cast<unsigned int>(command.color), description);
  }
  toy_test::Check(matches, description);
}

void ExpectDrawText(const DisplayList& list, std::size_t index, std::string_view text, int x, int y,
                    float font_size_px, std::uint32_t color, const char* description) {
  if (index >= list.size()) {
    std::fprintf(stderr, "paint display list test: missing command %zu: %s\n", index, description);
    toy_test::Check(false, description);
    return;
  }
  const Command& command = list.commands()[index];
  const bool matches = command.kind == CommandKind::kDrawText && command.text == text &&
                       command.x == x && command.y == y && command.font_size_px == font_size_px &&
                       command.color == color;
  if (!matches) {
    std::fprintf(stderr,
                 "paint display list test: command %zu is not the expected DrawText "
                 "\"%s\" x=%d y=%d font-size=%.1f color=0x%08X: %s\n",
                 index, command.text.c_str(), command.x, command.y,
                 static_cast<double>(command.font_size_px),
                 static_cast<unsigned int>(command.color), description);
  }
  toy_test::Check(matches, description);
}

bool SameCommand(const Command& lhs, const Command& rhs) {
  return lhs.kind == rhs.kind && lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
         lhs.height == rhs.height && lhs.color == rhs.color && lhs.text == rhs.text &&
         lhs.font_size_px == rhs.font_size_px;
}

// A default-constructed BoxTree has no root; paint must not touch the other
// inputs and must return an empty list.
void TestEmptyBoxTree() {
  const Document document = toy::dom::ParseDocument("<html></html>");
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  const BoxTree boxes;
  const DisplayList list = BuildDisplayList(document, styles, boxes);
  ExpectSize(list, 0, "an empty box tree yields an empty display list");
}

// html and body are transparent and there is no text, so a non-empty box tree
// still paints nothing.
void TestNoPaintableBoxes() {
  const DisplayList list = BuildPage("<html></html>");
  ExpectSize(list, 0, "transparent boxes and no text yield an empty display list");
}

void TestBlockBackground() {
  const DisplayList list =
      BuildPage("<div style=\"background-color: #112233; width: 100px; height: 40px\"></div>");
  ExpectSize(list, 1, "one opaque block background yields one FillRect");
  ExpectFillRect(list, 0, 8, 8, 100, 40, 0xFF112233u,
                 "the block background is a FillRect with the exact integer rect and ARGB");
}

void TestTransparentBackground() {
  const DisplayList list =
      BuildPage("<div style=\"background-color: transparent; width: 100px; height: 40px\"></div>");
  ExpectSize(list, 0, "a transparent background emits no FillRect");
}

void TestText() {
  const DisplayList list = BuildPage("<p>Hi</p>");
  ExpectSize(list, 1, "text yields exactly one DrawText");
  ExpectDrawText(list, 0, "Hi", 8, 24, 16.0f, 0xFF000000u,
                 "text is drawn with an opaque color at its rounded position");

  const DisplayList colored = BuildPage("<p style=\"color: #ff0000\">Hi</p>");
  ExpectSize(colored, 1, "a colored text box yields one DrawText");
  ExpectDrawText(colored, 0, "Hi", 8, 24, 16.0f, 0xFFFF0000u,
                 "the text color comes from the computed style, forced opaque");

  const DisplayList sized = BuildPage("<p style=\"font-size: 32px\">Hi</p>");
  ExpectSize(sized, 1, "a larger text box yields one DrawText");
  ExpectDrawText(sized, 0, "Hi", 8, 24, 32.0f, 0xFF000000u,
                 "the font size comes from the computed style");
}

void TestInlineBackgroundPrecedesText() {
  const DisplayList list = BuildPage("<p><span style=\"background-color: #00ff00\">ab</span></p>");
  ExpectSize(list, 2, "an inline fragment background and its text yield two commands");
  ExpectFillRect(list, 0, 8, 24, 16, 16, 0xFF00FF00u,
                 "the inline fragment background is emitted before the text");
  ExpectDrawText(list, 1, "ab", 8, 24, 16.0f, 0xFF000000u,
                 "the inline fragment's text follows its background");
}

// div mixes inline and block content, so the inline span is wrapped in an
// anonymous block; the anonymous block itself has no style and no command.
void TestAnonymousBlockPassesThrough() {
  const DisplayList list =
      BuildPage("<div><span style=\"background-color: #0000ff\">x</span><p>y</p></div>");
  ExpectSize(list, 3, "the anonymous block contributes no command of its own");
  ExpectFillRect(list, 0, 8, 8, 8, 16, 0xFF0000FFu,
                 "the inline background is emitted from inside the anonymous block");
  ExpectDrawText(list, 1, "x", 8, 8, 16.0f, 0xFF000000u, "the inline text follows its background");
  ExpectDrawText(list, 2, "y", 8, 40, 16.0f, 0xFF000000u, "the block text follows in box order");
}

void TestPreOrder() {
  const DisplayList list = BuildPage(
      "<div style=\"background-color: #111111\"><p style=\"background-color: "
      "#222222\">T</p></div>");
  ExpectSize(list, 3, "two nested backgrounds and one text yield three commands");
  ExpectFillRect(list, 0, 8, 8, 784, 48, 0xFF111111u, "the parent background is emitted first");
  ExpectFillRect(list, 1, 8, 24, 784, 16, 0xFF222222u, "the child background follows its parent");
  ExpectDrawText(list, 2, "T", 8, 24, 16.0f, 0xFF000000u, "text follows both backgrounds");
}

void TestFractionalGeometry() {
  const DisplayList list = BuildPage(
      "<div style=\"background-color: #336699; width: 10.5px; height: 5.5px; "
      "margin-left: 3.5px; margin-top: 2.5px\"></div>");
  ExpectSize(list, 1, "fractional geometry yields one FillRect");
  ExpectFillRect(list, 0, 12, 11, 11, 6, 0xFF336699u,
                 "fractional geometry rounds with std::lround, half away from zero");

  const DisplayList text = BuildPage("<p style=\"margin-left: 3.5px\">Hi</p>");
  ExpectSize(text, 1, "a fractional text position yields one DrawText");
  ExpectDrawText(text, 0, "Hi", 12, 24, 16.0f, 0xFF000000u,
                 "text geometry rounds with std::lround");
}

void TestNegativeCoordinates() {
  const DisplayList list = BuildPage(
      "<div style=\"background-color: #336699; width: 10px; height: 10px; "
      "margin-left: -20px\"></div>");
  ExpectSize(list, 1, "a negative-margin background yields one FillRect");
  ExpectFillRect(list, 0, -12, 8, 10, 10, 0xFF336699u,
                 "negative coordinates pass through to the renderer");
}

void TestDeterministic() {
  const std::string html =
      "<div style=\"background-color: #102030\"><p style=\"color: #00ff00\">Hello</p></div>";
  const DisplayList first = BuildPage(html);
  const DisplayList second = BuildPage(html);
  ExpectSize(first, 2, "the nested fixture yields one background and one text command");
  bool identical = first.size() == second.size();
  for (std::size_t index = 0; identical && index < first.size(); ++index) {
    identical = SameCommand(first.commands()[index], second.commands()[index]);
  }
  if (!identical) {
    std::fprintf(stderr, "paint display list test: two builds of one page differ\n");
  }
  toy_test::Check(identical, "building the same page twice yields identical commands");
}

// The walk is iterative: 10,000 nested transparent blocks must not overflow the
// C++ stack.
void TestDeepNestingWalk() {
  constexpr int kLevels = 10000;
  std::string html;
  html.reserve(static_cast<std::size_t>(kLevels) * 5);
  for (int level = 0; level < kLevels; ++level) {
    html.append("<div>");
  }
  const DisplayList list = BuildPage(html);
  ExpectSize(list, 0, "a 10,000-level box tree is walked without recursion and paints nothing");
}

}  // namespace

int main() {
  TestEmptyBoxTree();
  TestNoPaintableBoxes();
  TestBlockBackground();
  TestTransparentBackground();
  TestText();
  TestInlineBackgroundPrecedesText();
  TestAnonymousBlockPassesThrough();
  TestPreOrder();
  TestFractionalGeometry();
  TestNegativeCoordinates();
  TestDeterministic();
  TestDeepNestingWalk();
  return toy_test::Finish("paint display list");
}
