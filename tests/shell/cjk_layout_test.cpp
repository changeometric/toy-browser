// M10 post-CJK-line-breaking layout test for the committed CJK fixture page: the
// Chinese text must be split into break units so every Han code point becomes its
// own word-level text box (no spaces are needed), and the long Han paragraph must
// wrap into two lines inside the frozen viewport. This is the per-unit,
// text-content-level counterpart of the shell.cjk_dump_boxes CLI regex (which
// freezes positions/widths but cannot easily spell out the Han characters).
//
// The test drives the shell-private loader directly so it can inspect the box
// tree; it uses Fonts::SystemDefault() with the generated fixture fontconfig
// config supplied through the CTest environment, so the geometric values are the
// same as the CLI e2e tests (Toy Test Font Latin + Toy CJK fallback) and never
// depend on installed fonts. file:// only: no network, no display.

#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "page.h"
#include "support/check.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/net/url.h"
#include "toy/shell/fonts.h"

namespace {

constexpr int kViewportWidth = 320;

// One expected text box: its exact UTF-8 text and its device-pixel geometry
// (1 CSS px = 1 device px; the dump rounds with std::lround).
struct ExpectedTextBox {
  std::string_view text;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

int RoundPx(float value) { return static_cast<int>(std::lround(value)); }

bool NearlyEqual(float actual, float expected) { return std::fabs(actual - expected) <= 0.01f; }

// Pre-order text boxes of the loaded page, in box-tree order.
std::vector<const toy::layout::Box*> TextBoxes(const toy::layout::BoxTree& boxes) {
  std::vector<const toy::layout::Box*> texts;
  if (boxes.root() == toy::layout::kInvalidBox) {
    return texts;
  }
  std::vector<toy::layout::BoxId> pending;
  pending.push_back(boxes.root());
  while (!pending.empty()) {
    const toy::layout::BoxId id = pending.back();
    pending.pop_back();
    const toy::layout::Box& box = boxes.Get(id);
    if (box.kind == toy::layout::BoxKind::kText) {
      texts.push_back(&box);
    }
    std::vector<toy::layout::BoxId> children;
    for (toy::layout::BoxId child = box.first_child; child != toy::layout::kInvalidBox;
         child = boxes.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(children[index]);
    }
  }
  return texts;
}

// The fixture repeats the five Toy CJK Han characters 7 times in its last
// paragraph (35 break units total).
constexpr std::string_view kLongParagraph =
    "\u4e2d\u6587\u4f60\u597d\u7f51\u4e2d\u6587\u4f60\u597d\u7f51\u4e2d\u6587\u4f60\u597d\u7f51"
    "\u4e2d\u6587\u4f60\u597d\u7f51\u4e2d\u6587\u4f60\u597d\u7f51\u4e2d\u6587\u4f60\u597d\u7f51"
    "\u4e2d\u6587\u4f60\u597d\u7f51";

void CheckTextBox(const ExpectedTextBox& expected, const toy::layout::Box& box, int index) {
  const std::string prefix = "text box " + std::to_string(index) + " ";
  toy_test::Check(box.text == expected.text, (prefix + "has the expected UTF-8 text").c_str());
  toy_test::Check(RoundPx(box.x) == expected.x, (prefix + "has the frozen x").c_str());
  toy_test::Check(RoundPx(box.y) == expected.y, (prefix + "has the frozen y").c_str());
  toy_test::Check(RoundPx(box.width) == expected.width, (prefix + "has the frozen width").c_str());
  toy_test::Check(RoundPx(box.height) == expected.height,
                  (prefix + "has the frozen height").c_str());
  if (RoundPx(box.x) != expected.x || RoundPx(box.width) != expected.width) {
    std::fprintf(stderr, "  box %d \"%s\": x=%d width=%d, expected x=%d width=%d\n", index,
                 box.text.c_str(), RoundPx(box.x), RoundPx(box.width), expected.x, expected.width);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <cjk_site-directory>\n", argv[0]);
    return 1;
  }
  const std::string url_text = "file://" + std::string(argv[1]) + "/index.html";
  std::string error;
  const std::optional<toy::net::Url> url = toy::net::ParseUrl(url_text, &error);
  toy_test::Check(url.has_value(), "the cjk_site fixture URL parses");
  if (!url) {
    return toy_test::Finish("cjk layout");
  }
  const std::shared_ptr<const toy::shell::Fonts> fonts = toy::shell::Fonts::SystemDefault();
  toy::shell::Page page;
  toy_test::Check(toy::shell::LoadPage(*url, kViewportWidth, fonts, &page, &error),
                  "the cjk_site fixture page loads with system fonts");
  toy_test::Check(error.empty(), "a successful load clears the error");
  if (!page.boxes.size()) {
    return toy_test::Finish("cjk layout");
  }

  const std::vector<const toy::layout::Box*> texts = TextBoxes(page.boxes);

  // Every Han code point is its own text box, Latin words stay grouped, and the
  // space separators produce no text box. This exact list is the frozen
  // post-CJK-line-breaking segmentation of the fixture page.
  std::vector<ExpectedTextBox> expected = {
      // <h1>你好网</h1>: three 32 px Han break units, 0.7 em each.
      {"\u4f60", 8, 24, 22, 32},
      {"\u597d", 30, 24, 22, 32},
      {"\u7f51", 53, 24, 22, 32},
      // <p>Hello 中文</p>: the Latin word then two Han units after the space.
      {"Hello", 8, 88, 48, 16},
      {"\u4e2d", 66, 88, 11, 16},
      {"\u6587", 77, 88, 11, 16},
      // <p class="bold">ABC 网</p>: bold Latin word then the Han unit.
      {"ABC", 8, 136, 31, 16},
      {"\u7f51", 50, 136, 11, 16},
      // <p class="italic">abc 好 Hello</p>: italic Latin, Han unit, italic Latin.
      {"abc", 8, 184, 30, 16},
      {"\u597d", 48, 184, 11, 16},
      {"Hello", 69, 184, 50, 16},
  };
  // The long Han paragraph wraps: 27 units on the first line starting at x=8 and
  // ending at x=299 (27 * 11.203125 px = 302.484375 px, which fits the 304 px
  // content width), then the remaining 8 units on the second line at y=248.
  constexpr std::size_t kUnitsPerLine = 27;
  for (std::size_t index = 0; index < kLongParagraph.size() / 3; ++index) {
    const bool second_line = index >= kUnitsPerLine;
    expected.push_back(
        ExpectedTextBox{kLongParagraph.substr(index * 3, 3), 0, second_line ? 248 : 232, 11, 16});
  }
  // Compute the expected x positions from the shaped advance (the layout pen is
  // the same accumulation the frozen dump shows: 8, 19, 30, 42, ...).
  {
    constexpr float kHanAdvancePx = 11.203125f;
    for (std::size_t index = 0; index < kLongParagraph.size() / 3; ++index) {
      const std::size_t line_index = index % kUnitsPerLine;
      expected[11 + index].x = RoundPx(8.0f + static_cast<float>(line_index) * kHanAdvancePx);
    }
  }

  toy_test::Check(texts.size() == expected.size(),
                  "the page has exactly the expected number of text boxes");
  const std::size_t compared = texts.size() < expected.size() ? texts.size() : expected.size();
  for (std::size_t index = 0; index < compared; ++index) {
    CheckTextBox(expected[index], *texts[index], static_cast<int>(index));
  }

  // Every Han break unit is exactly one 3-byte UTF-8 code point: no unit glued
  // across characters (no cross-unit fallback groups) and no Latin character
  // split out of its word.
  std::size_t han_units = 0;
  for (const toy::layout::Box* box : texts) {
    const bool han =
        box->text.size() == 3 && (static_cast<unsigned char>(box->text[0]) & 0xF0) == 0xE0;
    if (han) {
      ++han_units;
    }
  }
  toy_test::Check(han_units == 3 + 2 + 1 + 1 + kLongParagraph.size() / 3,
                  "every Han code point is its own text box (one per character)");

  // Baselines: layout sets each line's ascent on its text boxes, so the 32 px
  // heading boxes share 25.6 px and every 16 px line box shares 12.8 px.
  bool baselines_ok = true;
  for (const toy::layout::Box* box : texts) {
    const float expected_ascent = box->height > 20.0f ? 25.6f : 12.8f;
    if (!NearlyEqual(box->baseline_offset_px, expected_ascent)) {
      baselines_ok = false;
      std::fprintf(stderr, "  box \"%s\": baseline_offset=%f, expected %f\n", box->text.c_str(),
                   static_cast<double>(box->baseline_offset_px),
                   static_cast<double>(expected_ascent));
    }
  }
  toy_test::Check(baselines_ok, "text boxes carry their line ascent as baseline offset");

  // Two lines of the hanging long paragraph are 16 px apart; the whole page is
  // 288 CSS px tall (content height) at this viewport.
  toy_test::Check(NearlyEqual(toy::layout::ContentHeight(page.boxes), 288.0f),
                  "the fixture page content height is 288 CSS px");
  toy_test::Check(texts.size() >= 11 + kLongParagraph.size() / 3 &&
                      RoundPx(texts[11 + 26]->y) == 232 && RoundPx(texts[11 + 27]->y) == 248,
                  "the long Han paragraph wraps after 27 units onto a second line");

  return toy_test::Finish("cjk layout");
}
