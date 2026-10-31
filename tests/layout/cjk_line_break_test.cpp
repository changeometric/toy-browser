// Minimal CJK line breaking: break units, no inserted spaces, simplified
// punctuation prohibitions, U+3000 whitespace and lenient invalid UTF-8.
// Geometry comes from the deterministic fake text metrics (advance = 0.5 px per
// byte per font-size px, so a 3-byte CJK character is 24 px wide at 16 px).

#include <cstddef>
#include <cstdio>
#include <iterator>
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
using toy::layout::Box;
using toy::layout::BoxId;
using toy::layout::BoxKind;
using toy::layout::BoxTree;
using toy::layout::kInvalidBox;

// A laid-out page. The DOM and style trees stay alive next to the box tree.
struct Page {
  Document document;
  toy::style::StyleTree styles;
  BoxTree boxes;
};

Page MakePage(const std::string& html, float viewport_width = 800.0f) {
  Page page;
  page.document = toy::dom::ParseDocument(html);
  page.styles = toy::style::ComputeStyles(page.document);
  toy::layout::LayoutOptions options;
  options.viewport_width_px = viewport_width;
  page.boxes = toy::layout::LayoutDocument(page.document, page.styles, options,
                                           toy::layout::test::MeasureFakeText);
  return page;
}

std::string DumpPage(const std::string& html, float viewport_width) {
  const Page page = MakePage(html, viewport_width);
  return toy::layout::DumpBoxTree(page.document, page.boxes);
}

void ExpectDump(const std::string& html, float viewport_width, const char* expected,
                const char* description) {
  const std::string actual = DumpPage(html, viewport_width);
  if (actual == expected) {
    toy_test::Check(true, description);
    return;
  }
  std::fprintf(stderr, "\nCJK line break test: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n",
               description, html.c_str(), expected, actual.c_str());
  toy_test::Check(false, description);
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

std::vector<const Box*> TextBoxes(const Page& page) {
  std::vector<const Box*> text_boxes;
  for (const BoxId id : AllBoxes(page.boxes)) {
    const Box& box = page.boxes.Get(id);
    if (box.kind == BoxKind::kText) {
      text_boxes.push_back(&box);
    }
  }
  return text_boxes;
}

std::vector<std::vector<const Box*>> Lines(const Page& page) {
  std::vector<std::vector<const Box*>> lines;
  for (const Box* box : TextBoxes(page)) {
    if (lines.empty() || lines.back().front()->y != box->y) {
      lines.push_back({});
    }
    lines.back().push_back(box);
  }
  return lines;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// Wrap case description strings use UTF-8 literals; these are the test's
// punctuation characters as byte strings (one CJK code point = 3 bytes).
constexpr std::string_view kClosingPunctuation[] = {"。", "）", "，", "！"};
constexpr std::string_view kOpeningPunctuation[] = {"（", "「", "【"};

bool StartsWithPunctuation(std::string_view text, const std::string_view* set,
                           std::size_t set_size) {
  for (std::size_t index = 0; index < set_size; ++index) {
    if (StartsWith(text, set[index])) {
      return true;
    }
  }
  return false;
}

bool EndsWithPunctuation(std::string_view text, const std::string_view* set, std::size_t set_size) {
  for (std::size_t index = 0; index < set_size; ++index) {
    if (EndsWith(text, set[index])) {
      return true;
    }
  }
  return false;
}

// A long Han paragraph wraps between characters inside the viewport, without any
// spaces or inserted gaps.
void TestLongHanParagraphWraps() {
  // Viewport 104: body/p content width 88, each Han character is 24 px wide, so
  // three characters fit per line (3 * 24 = 72 <= 88 < 96 = 4 * 24).
  ExpectDump("<p>中文测试中文测试</p>", 104.0f,
             R"boxes(<html> block x=0 y=0 width=104 height=96
  <body> block x=8 y=8 width=88 height=80
    <p> block x=8 y=24 width=88 height=48
      text "中" x=8 y=24 width=24 height=16
      text "文" x=32 y=24 width=24 height=16
      text "测" x=56 y=24 width=24 height=16
      text "试" x=8 y=40 width=24 height=16
      text "中" x=32 y=40 width=24 height=16
      text "文" x=56 y=40 width=24 height=16
      text "测" x=8 y=56 width=24 height=16
      text "试" x=32 y=56 width=24 height=16
)boxes",
             "a long Han paragraph wraps between characters without spaces");

  const Page page = MakePage("<p>中文测试中文测试</p>", 104.0f);
  const std::vector<const Box*> text_boxes = TextBoxes(page);
  toy_test::Check(text_boxes.size() == 8, "every Han character becomes its own text box");
  bool all_16px_wide = true;
  bool all_baseline_16 = true;
  for (const Box* box : text_boxes) {
    all_16px_wide = all_16px_wide && box->width == 24.0f;
    all_baseline_16 = all_baseline_16 && box->baseline_offset_px == 16.0f;
  }
  toy_test::Check(all_16px_wide, "every CJK unit is measured with the callback");
  toy_test::Check(all_baseline_16, "CJK units contribute to the line ascent/baseline");
}

// Closing punctuation attaches to the preceding unit, so it never starts a line.
void TestClosingPunctuationStaysWithPrevious() {
  // Viewport 104, line width 88: "中" (24) and "文。" (48) fit on one line, the
  // next "中" wraps. "。" stays glued to "文" instead of starting a line.
  ExpectDump("<p>中文。中文。中文。</p>", 104.0f,
             R"boxes(<html> block x=0 y=0 width=104 height=96
  <body> block x=8 y=8 width=88 height=80
    <p> block x=8 y=24 width=88 height=48
      text "中" x=8 y=24 width=24 height=16
      text "文。" x=32 y=24 width=48 height=16
      text "中" x=8 y=40 width=24 height=16
      text "文。" x=32 y=40 width=48 height=16
      text "中" x=8 y=56 width=24 height=16
      text "文。" x=32 y=56 width=48 height=16
)boxes",
             "closing punctuation stays with the preceding character");
  const Page page = MakePage("<p>中文。中文。中文。</p>", 104.0f);
  for (const std::vector<const Box*>& line : Lines(page)) {
    toy_test::Check(!StartsWithPunctuation(line.front()->text, kClosingPunctuation,
                                           std::size(kClosingPunctuation)),
                    "no line starts with closing punctuation");
  }
}

// Opening punctuation attaches to the following unit, so it never ends a line.
void TestOpeningPunctuationDoesNotEndLine() {
  ExpectDump("<p>测试（中文）测试</p>", 104.0f,
             R"boxes(<html> block x=0 y=0 width=104 height=112
  <body> block x=8 y=8 width=88 height=96
    <p> block x=8 y=24 width=88 height=64
      text "测" x=8 y=24 width=24 height=16
      text "试" x=32 y=24 width=24 height=16
      text "（中" x=8 y=40 width=48 height=16
      text "文）" x=8 y=56 width=48 height=16
      text "测" x=56 y=56 width=24 height=16
      text "试" x=8 y=72 width=24 height=16
)boxes",
             "opening punctuation never ends a line");
  const Page page = MakePage("<p>测试（中文）测试</p>", 104.0f);
  for (const std::vector<const Box*>& line : Lines(page)) {
    // The opening "（" is glued to the following "中", so no line ends with it.
    toy_test::Check(!EndsWithPunctuation(line.back()->text, kOpeningPunctuation,
                                         std::size(kOpeningPunctuation)),
                    "no line ends with opening punctuation");
  }
}

// Mixed Han/Latin text splits at every CJK boundary but keeps Latin words whole,
// and no spaces are inserted at any boundary.
void TestMixedSegmentation() {
  ExpectDump("<p>中文abc中文</p>", 800.0f,
             R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "中" x=8 y=24 width=24 height=16
      text "文" x=32 y=24 width=24 height=16
      text "abc" x=56 y=24 width=24 height=16
      text "中" x=80 y=24 width=24 height=16
      text "文" x=104 y=24 width=24 height=16
)boxes",
             "Han characters split individually while the Latin word stays one unit");
}

// U+3000 IDEOGRAPHIC SPACE is a separator exactly like an ASCII space.
void TestIdeographicSpaceSeparator() {
  const char* expected =
      R"boxes(<html> block x=0 y=0 width=800 height=64
  <body> block x=8 y=8 width=784 height=48
    <p> block x=8 y=24 width=784 height=16
      text "中" x=8 y=24 width=24 height=16
      text "文" x=40 y=24 width=24 height=16
)boxes";
  ExpectDump("<p>中　文</p>", 800.0f, expected, "U+3000 separates like a space");
  ExpectDump("<p>中 文</p>", 800.0f, expected, "an ASCII space separates identically");
}

// A Latin word is never split: an overlong word overflows its line as one box.
void TestLatinWordDoesNotBreak() {
  ExpectDump("<p>abcdef</p>", 40.0f,
             R"boxes(<html> block x=0 y=0 width=40 height=64
  <body> block x=8 y=8 width=24 height=48
    <p> block x=8 y=24 width=24 height=16
      text "abcdef" x=8 y=24 width=48 height=16
)boxes",
             "a Latin word stays one unit and overflows instead of breaking");
}

// Invalid UTF-8 is lenient: each offending byte becomes its own unit and layout
// keeps going; truncated sequences do not crash or loop.
void TestInvalidUtf8IsLenient() {
  {
    const Page page = MakePage(
        "<p>a\xFF"
        "b</p>");
    const std::vector<const Box*> text_boxes = TextBoxes(page);
    toy_test::Check(text_boxes.size() == 3, "an invalid byte splits the surrounding units");
    if (text_boxes.size() == 3) {
      toy_test::Check(text_boxes[0]->text == "a", "the unit before the invalid byte is kept");
      toy_test::Check(text_boxes[1]->text.size() == 1 &&
                          static_cast<unsigned char>(text_boxes[1]->text[0]) == 0xFFu,
                      "the invalid byte is its own unit");
      toy_test::Check(text_boxes[2]->text == "b", "layout continues after the invalid byte");
    }
  }
  {
    // "\xE4\xB8" is a truncated three-byte sequence: E4 is invalid (truncated)
    // and B8 is an invalid lead byte, so both are their own units.
    const Page page = MakePage("<p>\xE4\xB8</p>");
    const std::vector<const Box*> text_boxes = TextBoxes(page);
    toy_test::Check(text_boxes.size() == 2, "a truncated sequence yields one unit per byte");
    if (text_boxes.size() == 2) {
      toy_test::Check(static_cast<unsigned char>(text_boxes[0]->text[0]) == 0xE4u,
                      "the truncated lead byte is its own unit");
      toy_test::Check(static_cast<unsigned char>(text_boxes[1]->text[0]) == 0xB8u,
                      "the continuation byte is its own unit");
    }
  }
}

// Kana, Hangul and fullwidth forms are individual break units too.
void TestOtherCjkRanges() {
  const Page page = MakePage("<p>あア한Ａ</p>");
  const std::vector<const Box*> text_boxes = TextBoxes(page);
  toy_test::Check(text_boxes.size() == 4, "Hiragana, Katakana, Hangul and fullwidth are units");
  const std::vector<std::string> expected = {"あ", "ア", "한", "Ａ"};
  if (text_boxes.size() == expected.size()) {
    bool matches = true;
    for (std::size_t index = 0; index < expected.size(); ++index) {
      matches = matches && text_boxes[index]->text == expected[index];
    }
    toy_test::Check(matches, "the CJK units keep their source order and text");
  }
}

// Every break unit contributes its own metrics: a mixed-size line shares the
// maximum ascent and line height.
void TestCjkUnitsContributeMetrics() {
  const Page page = MakePage("<p>中<span style=\"font-size:32px\">文</span></p>");
  const std::vector<const Box*> text_boxes = TextBoxes(page);
  toy_test::Check(text_boxes.size() == 2, "the mixed-size line has two text boxes");
  if (text_boxes.size() == 2) {
    toy_test::Check(text_boxes[0]->width == 24.0f, "the 16 px Han unit is 24 px wide");
    toy_test::Check(text_boxes[1]->width == 48.0f, "the 32 px Han unit is 48 px wide");
    toy_test::Check(
        text_boxes[0]->baseline_offset_px == 32.0f && text_boxes[1]->baseline_offset_px == 32.0f,
        "all text boxes of the line share the maximum ascent");
    toy_test::Check(text_boxes[0]->height == 32.0f && text_boxes[1]->height == 32.0f,
                    "all text boxes of the line share the line height");
  }
}

}  // namespace

int main() {
  TestLongHanParagraphWraps();
  TestClosingPunctuationStaysWithPrevious();
  TestOpeningPunctuationDoesNotEndLine();
  TestMixedSegmentation();
  TestIdeographicSpaceSeparator();
  TestLatinWordDoesNotBreak();
  TestInvalidUtf8IsLenient();
  TestOtherCjkRanges();
  TestCjkUnitsContributeMetrics();
  return toy_test::Finish("layout CJK line break test");
}
