// Per-line baseline layout: every text box of a line shares the line's maximum
// ascent in baseline_offset_px, non-text boxes keep 0, <br>-forced empty lines
// use the br style's metrics, and the measurer receives the computed style of
// the measured text (words, the separator space and the br empty text).

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
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
using toy::layout::LayoutDocument;
using toy::layout::LayoutOptions;
using toy::layout::MeasureText;
using toy::layout::ReplacedSize;
using toy::layout::ReplacedSizes;
using toy::layout::TextMetrics;

// A laid-out page. The DOM and style trees stay alive next to the box tree.
struct Page {
  Document document;
  toy::style::StyleTree styles;
  BoxTree boxes;
};

Page MakePage(const std::string& html, const MeasureText& measure, float viewport_width = 800.0f,
              const ReplacedSizes* replaced_sizes = nullptr) {
  Page page;
  page.document = toy::dom::ParseDocument(html);
  page.styles = toy::style::ComputeStyles(page.document);
  LayoutOptions options;
  options.viewport_width_px = viewport_width;
  options.replaced_sizes = replaced_sizes;
  page.boxes = LayoutDocument(page.document, page.styles, options, measure);
  return page;
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

const Box* FindTextBox(const BoxTree& boxes, std::string_view text) {
  for (const BoxId id : AllBoxes(boxes)) {
    const Box& box = boxes.Get(id);
    if (box.kind == BoxKind::kText && box.text == text) {
      return &box;
    }
  }
  return nullptr;
}

void ExpectBaseline(const Page& page, std::string_view text, float expected,
                    const char* description) {
  const Box* box = FindTextBox(page.boxes, text);
  if (box == nullptr) {
    std::fprintf(stderr, "baseline test: %s (text box \"%.*s\" not found)\n", description,
                 static_cast<int>(text.size()), text.data());
    toy_test::Check(false, description);
    return;
  }
  if (box->baseline_offset_px != expected) {
    std::fprintf(stderr, "baseline test: %s (expected %g, got %g)\n", description,
                 static_cast<double>(expected), static_cast<double>(box->baseline_offset_px));
  }
  toy_test::Check(box->baseline_offset_px == expected, description);
}

// All non-text boxes keep the initial baseline offset.
void ExpectNonTextBaselinesZero(const Page& page, const char* description) {
  for (const BoxId id : AllBoxes(page.boxes)) {
    const Box& box = page.boxes.Get(id);
    if (box.kind != BoxKind::kText && box.baseline_offset_px != 0.0f) {
      std::fprintf(stderr, "baseline test: %s (a non-text box has baseline_offset %g)\n",
                   description, static_cast<double>(box.baseline_offset_px));
      toy_test::Check(false, description);
      return;
    }
  }
  toy_test::Check(true, description);
}

// One line: every text box gets the line ascent (the fake measurer's ascent is
// the font size), and the block/inline boxes keep 0.
void TestSingleLineBaseline() {
  const Page page = MakePage("<p>ab <span>cd</span></p>", toy::layout::test::MeasureFakeText);
  ExpectBaseline(page, "ab", 16.0f, "the first word's baseline is the line ascent");
  ExpectBaseline(page, "cd", 16.0f, "a word on the same line shares the baseline");
  ExpectNonTextBaselinesZero(page, "non-text boxes keep baseline_offset_px 0");
}

// Mixed metrics: different ascents on one line share the maximum.
TextMetrics MeasureByWeight(std::string_view text, const toy::style::ComputedStyle& style) {
  TextMetrics metrics;
  metrics.advance_px = 8.0f * static_cast<float>(text.size());
  metrics.ascent_px = style.font_weight >= 700 ? 30.0f : 10.0f;
  metrics.line_height_px = metrics.ascent_px + 2.0f;
  return metrics;
}

void TestMaxAscentOnLine() {
  const Page page = MakePage("<p>a<span style=\"font-weight: 700\">b</span></p>", MeasureByWeight);
  ExpectBaseline(page, "a", 30.0f, "the normal word uses the bold word's larger ascent");
  ExpectBaseline(page, "b", 30.0f, "the bold word uses its own ascent");
  ExpectNonTextBaselinesZero(page, "the bold span fragment keeps baseline 0");
  // The line height is the maximum item line height: bold ascent 30 + 2.
  const Box* a = FindTextBox(page.boxes, "a");
  const Box* b = FindTextBox(page.boxes, "b");
  toy_test::Check(a != nullptr && b != nullptr && a->height == 32.0f && b->height == 32.0f,
                  "all boxes of the line share the maximum line height");
}

// Two lines with different font sizes get their own line ascent.
void TestBaselinePerLine() {
  const Page page = MakePage(
      "<p><span style=\"font-size:20px\">aaaa</span> <span style=\"font-size:40px\">"
      "bbbb</span></p>",
      toy::layout::test::MeasureFakeText, 40.0f);
  ExpectBaseline(page, "aaaa", 20.0f, "the first line uses the first line's ascent");
  ExpectBaseline(page, "bbbb", 40.0f, "the wrapped word uses its own line's ascent");
}

// A replaced box is not text: it keeps baseline 0 and does not change the text
// baseline, even when it is taller than the text.
void TestReplacedBoxBaseline() {
  Page page;
  page.document = toy::dom::ParseDocument("<p><img>x</p>");
  page.styles = toy::style::ComputeStyles(page.document);
  ReplacedSizes sizes;
  for (std::size_t index = 0; index < page.document.size(); ++index) {
    const auto id = static_cast<toy::dom::NodeId>(index);
    if (page.document.Get(id).type == toy::dom::NodeType::kElement &&
        page.document.Get(id).name == "img") {
      sizes[id] = ReplacedSize{16.0f, 40.0f};
    }
  }
  LayoutOptions options;
  options.replaced_sizes = &sizes;
  page.boxes =
      LayoutDocument(page.document, page.styles, options, toy::layout::test::MeasureFakeText);
  ExpectBaseline(page, "x", 16.0f, "a taller replaced box does not change the text baseline");
  for (const BoxId id : AllBoxes(page.boxes)) {
    const Box& box = page.boxes.Get(id);
    if (box.kind == BoxKind::kInline && box.node != toy::dom::kInvalidNode &&
        page.document.Get(box.node).name == "img") {
      toy_test::Check(box.baseline_offset_px == 0.0f,
                      "the replaced box itself keeps baseline_offset_px 0");
    }
  }
}

// A recorded measurement call: the text plus the computed style it was given.
struct RecordedCall {
  std::string text;
  std::vector<std::string> families;
  float font_size_px = 0;
  int font_weight = 0;
  toy::style::FontStyle font_style = toy::style::FontStyle::kNormal;
};

class Recorder {
 public:
  MeasureText Callback() {
    return [this](std::string_view text_utf8, const toy::style::ComputedStyle& style) {
      RecordedCall call;
      call.text = std::string(text_utf8);
      call.families = style.font_family;
      call.font_size_px = style.font_size_px;
      call.font_weight = style.font_weight;
      call.font_style = style.font_style;
      calls.push_back(std::move(call));
      return toy::layout::test::MeasureFakeText(text_utf8, style);
    };
  }

  std::vector<RecordedCall> calls;
};

const RecordedCall* FindCall(const Recorder& recorder, std::string_view text) {
  for (const RecordedCall& call : recorder.calls) {
    if (call.text == text) {
      return &call;
    }
  }
  return nullptr;
}

// The measurer sees the computed style of the text node for words and for the
// separator space, including UA-derived font families.
void TestMeasurerReceivesComputedStyle() {
  Recorder recorder;
  const MeasureText measure = recorder.Callback();
  const Page page = MakePage(
      "<p style='font-family: \"Toy Font\"; font-size: 24px; font-weight: 700; "
      "font-style: italic'>ab <code>cd</code></p>",
      measure);
  const RecordedCall* ab = FindCall(recorder, "ab");
  toy_test::Check(ab != nullptr, "the measurer is called for the first word");
  if (ab != nullptr) {
    toy_test::Check(ab->families == std::vector<std::string>{"Toy Font"},
                    "the word uses the element's computed font-family");
    toy_test::Check(ab->font_size_px == 24.0f, "the word uses the computed font size");
    toy_test::Check(ab->font_weight == 700, "the word uses the computed font weight");
    toy_test::Check(ab->font_style == toy::style::FontStyle::kItalic,
                    "the word uses the computed font style");
  }
  const RecordedCall* space = FindCall(recorder, " ");
  toy_test::Check(space != nullptr, "the measurer is called for the separator space");
  if (space != nullptr) {
    toy_test::Check(space->families == std::vector<std::string>{"Toy Font"},
                    "the separator space uses the text node's computed style");
  }
  const RecordedCall* cd = FindCall(recorder, "cd");
  toy_test::Check(cd != nullptr, "the measurer is called for the code word");
  if (cd != nullptr) {
    toy_test::Check(cd->families == std::vector<std::string>{"monospace"},
                    "the code word uses the UA monospace family");
    toy_test::Check(cd->font_weight == 700, "the code word inherits the computed font weight");
  }
  ExpectBaseline(page, "ab", 24.0f, "the line baseline uses the line's ascent");
  ExpectBaseline(page, "cd", 24.0f, "text on the same line shares the baseline");
}

const Box* FindElementBox(const Page& page, std::string_view tag) {
  for (const BoxId id : AllBoxes(page.boxes)) {
    const Box& box = page.boxes.Get(id);
    if ((box.kind == BoxKind::kBlock || box.kind == BoxKind::kInline) &&
        box.node != toy::dom::kInvalidNode && page.document.Get(box.node).name == tag) {
      return &box;
    }
  }
  return nullptr;
}

// A <br>-forced empty line is measured with the br's own computed style.
void TestBreakUsesBrStyle() {
  Recorder recorder;
  const MeasureText measure = recorder.Callback();
  const Page page = MakePage(
      "<p><br style='font-family: ToyFont; font-size: 32px; font-weight: 700'></p>", measure);
  toy_test::Check(recorder.calls.size() == 1,
                  "the empty <br> line triggers exactly one measurement");
  if (!recorder.calls.empty()) {
    const RecordedCall& call = recorder.calls.front();
    toy_test::Check(call.text.empty(), "the empty <br> line is measured with empty text");
    toy_test::Check(call.families == std::vector<std::string>{"ToyFont"},
                    "the empty <br> line uses the br's font-family");
    toy_test::Check(call.font_size_px == 32.0f, "the empty <br> line uses the br's font size");
    toy_test::Check(call.font_weight == 700, "the empty <br> line uses the br's font weight");
  }
  // The line height still comes from the br style's empty-text metrics.
  const Box* p_box = FindElementBox(page, "p");
  toy_test::Check(p_box != nullptr && p_box->height == 32.0f,
                  "the br empty line contributes the br style's line height");
}

}  // namespace

int main() {
  TestSingleLineBaseline();
  TestMaxAscentOnLine();
  TestBaselinePerLine();
  TestReplacedBoxBaseline();
  TestMeasurerReceivesComputedStyle();
  TestBreakUsesBrStyle();
  return toy_test::Finish("layout baseline test");
}
