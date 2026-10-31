// Tests for toy::paint::BuildDisplayList: a laid-out page becomes an ordered
// display list of background fills, image draws and text draws, wrapped in a
// viewport clip and translated by the vertical scroll offset.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/check.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/paint/paint.h"
#include "toy/render/font.h"
#include "toy/render/image.h"
#include "toy/render/paint.h"
#include "toy/style/computed_style.h"

namespace {

using toy::dom::Document;
using toy::dom::NodeId;
using toy::layout::BoxTree;
using toy::layout::LayoutDocument;
using toy::layout::LayoutOptions;
using toy::layout::ReplacedSize;
using toy::layout::ReplacedSizes;
using toy::layout::TextMetrics;
using toy::paint::BuildDisplayList;
using toy::paint::FontRun;
using toy::paint::FontRunsProvider;
using toy::paint::ImageMap;
using toy::paint::Viewport;
using toy::render::Command;
using toy::render::CommandKind;
using toy::render::DisplayList;
using toy::render::DrawDisplayList;
using toy::render::Font;
using toy::render::Image;
using toy::render::Surface;

// The real built-in bitmap-font metrics, adapted exactly like shell does for
// layout when no real font is selected. Paint never depends on them; the test
// pipeline needs a measure callback to lay the page out.
TextMetrics MeasureWithBitmapFont(std::string_view text_utf8,
                                  const toy::style::ComputedStyle& style) {
  const float font_size_px = style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
  const float line_height_px = toy::render::LineHeightPx(font_size_px);
  return TextMetrics{toy::render::MeasureTextWidth(text_utf8, font_size_px), line_height_px,
                     line_height_px};
}

// Layout metrics of one face: the same values the shell's Fonts object reports
// for a FromFile selection, so a real-font paint test lays the page out with the
// same metrics it paints with.
TextMetrics MeasureWithFont(const std::shared_ptr<const Font>& font, std::string_view text_utf8,
                            const toy::style::ComputedStyle& style) {
  const float font_size_px = style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
  return TextMetrics{font->MeasureTextWidth(text_utf8, font_size_px),
                     font->LineHeightPx(font_size_px), font->AscentPx(font_size_px)};
}

// Full pipeline for a fixture page with caller-supplied replaced sizes, images,
// a layout measurer and a paint runs provider. The layout viewport is the
// viewport width, exactly like shell does it.
DisplayList BuildPageWithMeasure(const Document& document, const ReplacedSizes* replaced_sizes,
                                 const ImageMap& images, const toy::layout::MeasureText& measure,
                                 const FontRunsProvider& font_runs, int viewport_width,
                                 int viewport_height, int scroll_y) {
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  LayoutOptions options;
  options.viewport_width_px = static_cast<float>(viewport_width);
  options.replaced_sizes = replaced_sizes;
  const BoxTree boxes = LayoutDocument(document, styles, options, measure);
  const Viewport viewport{viewport_width, viewport_height, scroll_y};
  return BuildDisplayList(document, styles, boxes, viewport, images, font_runs);
}

// The M7 path: no replaced elements, an empty image map and no real font.
DisplayList BuildPage(const std::string& html, int viewport_width = 800, int viewport_height = 600,
                      int scroll_y = 0) {
  const ImageMap images;
  return BuildPageWithMeasure(toy::dom::ParseDocument(html), nullptr, images, MeasureWithBitmapFont,
                              FontRunsProvider{}, viewport_width, viewport_height, scroll_y);
}

// A runs provider that paints the whole text box with one selected face (the
// shape of a single-font selection). Empty text keeps the bitmap path.
FontRunsProvider SingleFontProvider(const std::shared_ptr<const Font>& font) {
  return [font](const toy::style::ComputedStyle&, std::string_view text_utf8) {
    if (text_utf8.empty()) {
      return std::vector<FontRun>{};
    }
    return std::vector<FontRun>{FontRun{font, text_utf8}};
  };
}

// The same pipeline with a real font selected (the M8b-equivalent path): layout
// measures with the font and paint receives one run per text box.
DisplayList BuildFontPage(const std::string& html, const std::shared_ptr<const Font>& font,
                          int viewport_width = 800, int viewport_height = 600, int scroll_y = 0) {
  const ImageMap images;
  const toy::layout::MeasureText measure = [font](std::string_view text_utf8,
                                                  const toy::style::ComputedStyle& style) {
    return MeasureWithFont(font, text_utf8, style);
  };
  return BuildPageWithMeasure(toy::dom::ParseDocument(html), nullptr, images, measure,
                              SingleFontProvider(font), viewport_width, viewport_height, scroll_y);
}

// A two-face test selection: every non-ASCII code point (the Han fixture text)
// uses `han`, ASCII uses `latin`. The same splitting drives layout measurement
// and the paint provider, so the two agree exactly like the shell's Fonts does.
constexpr int kLatinFace = 0;
constexpr int kHanFace = 1;

// The fixture Han code points are three-byte UTF-8 sequences and the ASCII text
// is single-byte, which is all this test splitter needs.
std::vector<FontRun> SplitTestRuns(const std::shared_ptr<const Font>& latin,
                                   const std::shared_ptr<const Font>& han,
                                   std::string_view text_utf8) {
  std::vector<FontRun> runs;
  std::size_t offset = 0;
  std::size_t run_start = 0;
  int run_face = -1;
  while (offset < text_utf8.size()) {
    const std::size_t start = offset;
    const bool han_sequence = (static_cast<unsigned char>(text_utf8[offset]) & 0x80U) != 0U;
    offset = std::min(text_utf8.size(), offset + (han_sequence ? 3U : 1U));
    const int face = han_sequence ? kHanFace : kLatinFace;
    if (run_face < 0) {
      run_face = face;
      run_start = start;
      continue;
    }
    if (face == run_face) {
      continue;
    }
    runs.push_back(FontRun{run_face == kLatinFace ? latin : han,
                           text_utf8.substr(run_start, start - run_start)});
    run_face = face;
    run_start = start;
  }
  if (run_face >= 0) {
    runs.push_back(FontRun{run_face == kLatinFace ? latin : han, text_utf8.substr(run_start)});
  }
  return runs;
}

TextMetrics MeasureWithTwoFonts(const std::shared_ptr<const Font>& latin,
                                const std::shared_ptr<const Font>& han, std::string_view text_utf8,
                                const toy::style::ComputedStyle& style) {
  const float font_size_px = style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
  if (text_utf8.empty()) {
    return TextMetrics{0.0f, latin->LineHeightPx(font_size_px), latin->AscentPx(font_size_px)};
  }
  float advance_px = 0.0f;
  float line_height_px = 0.0f;
  float ascent_px = 0.0f;
  for (const FontRun& run : SplitTestRuns(latin, han, text_utf8)) {
    advance_px += run.font->MeasureTextWidth(run.text, font_size_px);
    line_height_px = std::max(line_height_px, run.font->LineHeightPx(font_size_px));
    ascent_px = std::max(ascent_px, run.font->AscentPx(font_size_px));
  }
  return TextMetrics{advance_px, line_height_px, ascent_px};
}

FontRunsProvider TwoFontProvider(const std::shared_ptr<const Font>& latin,
                                 const std::shared_ptr<const Font>& han) {
  return [latin, han](const toy::style::ComputedStyle&, std::string_view text_utf8) {
    return SplitTestRuns(latin, han, text_utf8);
  };
}

DisplayList BuildTwoFontPage(const std::string& html, const std::shared_ptr<const Font>& latin,
                             const std::shared_ptr<const Font>& han, int viewport_width = 800,
                             int viewport_height = 600, int scroll_y = 0) {
  const ImageMap images;
  const toy::layout::MeasureText measure = [latin, han](std::string_view text_utf8,
                                                        const toy::style::ComputedStyle& style) {
    return MeasureWithTwoFonts(latin, han, text_utf8, style);
  };
  return BuildPageWithMeasure(toy::dom::ParseDocument(html), nullptr, images, measure,
                              TwoFontProvider(latin, han), viewport_width, viewport_height,
                              scroll_y);
}

// The DOM id of the first <img> element: layout and paint both key images by it.
NodeId FindImageNode(const Document& document) {
  for (std::size_t index = 0; index < document.size(); ++index) {
    const NodeId id = static_cast<NodeId>(index);
    const toy::dom::Node& node = document.Get(id);
    if (node.type == toy::dom::NodeType::kElement && node.name == "img") {
      return id;
    }
  }
  return toy::dom::kInvalidNode;
}

// A decoded image with one solid ARGB color. Paint only forwards the shared
// pointer, so the pixel content does not affect the command list.
std::shared_ptr<const Image> MakeImage(int width, int height, std::uint32_t argb) {
  auto image = std::make_shared<Image>();
  image->width = width;
  image->height = height;
  image->pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), argb);
  return image;
}

// The pipeline for a page whose first <img> is a replaced box with `intrinsic`
// size and whose node id maps to `image`.
DisplayList BuildFontImagePage(const std::string& html, const ReplacedSize& intrinsic,
                               const std::shared_ptr<const Image>& image,
                               const std::shared_ptr<const Font>& font, int viewport_width = 800,
                               int viewport_height = 600, int scroll_y = 0) {
  const Document document = toy::dom::ParseDocument(html);
  const NodeId image_node = FindImageNode(document);
  ReplacedSizes sizes;
  if (image_node != toy::dom::kInvalidNode) {
    sizes.emplace(image_node, intrinsic);
  }
  ImageMap images;
  if (image_node != toy::dom::kInvalidNode) {
    images.emplace(image_node, image);
  }
  if (!font) {
    return BuildPageWithMeasure(document, &sizes, images, MeasureWithBitmapFont, FontRunsProvider{},
                                viewport_width, viewport_height, scroll_y);
  }
  const toy::layout::MeasureText measure = [font](std::string_view text_utf8,
                                                  const toy::style::ComputedStyle& style) {
    return MeasureWithFont(font, text_utf8, style);
  };
  return BuildPageWithMeasure(document, &sizes, images, measure, SingleFontProvider(font),
                              viewport_width, viewport_height, scroll_y);
}

// The same pipeline painted with the built-in bitmap font.
DisplayList BuildImagePage(const std::string& html, const ReplacedSize& intrinsic,
                           const std::shared_ptr<const Image>& image, int viewport_width = 800,
                           int viewport_height = 600, int scroll_y = 0) {
  return BuildFontImagePage(html, intrinsic, image, nullptr, viewport_width, viewport_height,
                            scroll_y);
}

// A page laid out with replaced sizes but painted with an empty image map: the
// image box exists in the box tree and nonetheless paints no DrawImage.
DisplayList BuildReplacedPageWithoutImages(const std::string& html, const ReplacedSize& intrinsic,
                                           int viewport_width = 800, int viewport_height = 600,
                                           int scroll_y = 0) {
  const Document document = toy::dom::ParseDocument(html);
  const NodeId image_node = FindImageNode(document);
  ReplacedSizes sizes;
  if (image_node != toy::dom::kInvalidNode) {
    sizes.emplace(image_node, intrinsic);
  }
  const ImageMap images;
  return BuildPageWithMeasure(document, &sizes, images, MeasureWithBitmapFont, FontRunsProvider{},
                              viewport_width, viewport_height, scroll_y);
}

void ExpectSize(const DisplayList& list, std::size_t expected, const char* description) {
  if (list.size() != expected) {
    std::fprintf(stderr, "paint display list test: expected %zu command(s), got %zu: %s\n",
                 expected, list.size(), description);
  }
  toy_test::Check(list.size() == expected, description);
}

void ExpectPushClip(const DisplayList& list, std::size_t index, int x, int y, int width, int height,
                    const char* description) {
  if (index >= list.size()) {
    std::fprintf(stderr, "paint display list test: missing command %zu: %s\n", index, description);
    toy_test::Check(false, description);
    return;
  }
  const Command& command = list.commands()[index];
  const bool matches = command.kind == CommandKind::kPushClip && command.x == x && command.y == y &&
                       command.width == width && command.height == height;
  if (!matches) {
    std::fprintf(stderr,
                 "paint display list test: command %zu is not the expected PushClip "
                 "x=%d y=%d width=%d height=%d: %s\n",
                 index, command.x, command.y, command.width, command.height, description);
  }
  toy_test::Check(matches, description);
}

void ExpectPopClip(const DisplayList& list, std::size_t index, const char* description) {
  if (index >= list.size()) {
    std::fprintf(stderr, "paint display list test: missing command %zu: %s\n", index, description);
    toy_test::Check(false, description);
    return;
  }
  const bool matches = list.commands()[index].kind == CommandKind::kPopClip;
  if (!matches) {
    std::fprintf(stderr, "paint display list test: command %zu is not a PopClip: %s\n", index,
                 description);
  }
  toy_test::Check(matches, description);
}

// A non-empty display list always starts with the viewport clip and ends with
// its PopClip.
void ExpectViewportClip(const DisplayList& list, int width, int height, const char* description) {
  ExpectPushClip(list, 0, 0, 0, width, height, description);
  if (!list.commands().empty()) {
    ExpectPopClip(list, list.size() - 1, description);
  }
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

// One real-font text command: kDrawText with the selected font, a baseline y,
// the box x, the preserved text/size/color and no image.
void ExpectDrawTextWithFont(const DisplayList& list, std::size_t index,
                            const std::shared_ptr<const Font>& font, std::string_view text, int x,
                            int baseline_y, float font_size_px, std::uint32_t color,
                            const char* description) {
  if (index >= list.size()) {
    std::fprintf(stderr, "paint display list test: missing command %zu: %s\n", index, description);
    toy_test::Check(false, description);
    return;
  }
  const Command& command = list.commands()[index];
  const bool matches = command.kind == CommandKind::kDrawText && command.font == font &&
                       command.text == text && command.x == x && command.y == baseline_y &&
                       command.font_size_px == font_size_px && command.color == color;
  if (!matches) {
    std::fprintf(stderr,
                 "paint display list test: command %zu is not the expected font DrawText "
                 "\"%s\" x=%d baseline-y=%d font-size=%.1f color=0x%08X: %s\n",
                 index, command.text.c_str(), command.x, command.y,
                 static_cast<double>(command.font_size_px),
                 static_cast<unsigned int>(command.color), description);
  }
  toy_test::Check(matches, description);
}

void ExpectDrawImage(const DisplayList& list, std::size_t index, int x, int y, int width,
                     int height, const std::shared_ptr<const Image>& expected_image,
                     const char* description) {
  if (index >= list.size()) {
    std::fprintf(stderr, "paint display list test: missing command %zu: %s\n", index, description);
    toy_test::Check(false, description);
    return;
  }
  const Command& command = list.commands()[index];
  const bool matches = command.kind == CommandKind::kDrawImage && command.x == x &&
                       command.y == y && command.width == width && command.height == height &&
                       command.image.get() == expected_image.get();
  if (!matches) {
    std::fprintf(stderr,
                 "paint display list test: command %zu is not the expected DrawImage "
                 "x=%d y=%d width=%d height=%d: %s\n",
                 index, command.x, command.y, command.width, command.height, description);
  }
  toy_test::Check(matches, description);
}

bool SameCommand(const Command& lhs, const Command& rhs) {
  return lhs.kind == rhs.kind && lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
         lhs.height == rhs.height && lhs.color == rhs.color && lhs.text == rhs.text &&
         lhs.font_size_px == rhs.font_size_px && lhs.image == rhs.image && lhs.font == rhs.font;
}

// Equality of everything except the y screen position, for scroll comparisons.
bool SameCommandExceptY(const Command& lhs, const Command& rhs) {
  return lhs.kind == rhs.kind && lhs.x == rhs.x && lhs.width == rhs.width &&
         lhs.height == rhs.height && lhs.color == rhs.color && lhs.text == rhs.text &&
         lhs.font_size_px == rhs.font_size_px && lhs.image == rhs.image && lhs.font == rhs.font;
}

// True when every command uses the built-in bitmap text path (no real font).
bool NoCommandHasFont(const DisplayList& list) {
  for (const Command& command : list.commands()) {
    if (command.font) {
      return false;
    }
  }
  return true;
}

// Renders a list over white, exactly like the shell's page painting pipeline.
Surface RenderList(const DisplayList& list, int width, int height) {
  Surface surface(width, height);
  surface.Fill(0xFFFFFFFFu);
  DrawDisplayList(list, surface);
  return surface;
}

bool SamePixels(const Surface& lhs, const Surface& rhs) {
  if (lhs.width() != rhs.width() || lhs.height() != rhs.height()) {
    return false;
  }
  for (int y = 0; y < lhs.height(); ++y) {
    for (int x = 0; x < lhs.width(); ++x) {
      if (lhs.At(x, y) != rhs.At(x, y)) {
        return false;
      }
    }
  }
  return true;
}

// A default-constructed BoxTree has no root; paint must not touch the other
// inputs and must return an empty list (no clip pair either).
void TestEmptyBoxTree() {
  const Document document = toy::dom::ParseDocument("<html></html>");
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  const BoxTree boxes;
  const ImageMap images;
  const DisplayList list =
      BuildDisplayList(document, styles, boxes, Viewport{800, 600, 0}, images, FontRunsProvider{});
  ExpectSize(list, 0, "an empty box tree yields an empty display list");
}

// html and body are transparent and there is no text: the non-empty box tree
// still yields the viewport clip pair, but no page commands.
void TestNoPaintableBoxes() {
  const DisplayList list = BuildPage("<html></html>");
  ExpectSize(list, 2, "transparent boxes and no text yield only the viewport clip pair");
  ExpectViewportClip(list, 800, 600, "an unpaintable page is still clipped to the viewport");
}

void TestBlockBackground() {
  const DisplayList list =
      BuildPage("<div style=\"background-color: #112233; width: 100px; height: 40px\"></div>");
  ExpectSize(list, 3, "one opaque block background yields one FillRect between the clip pair");
  ExpectViewportClip(list, 800, 600, "the block background list is clipped to the viewport");
  ExpectFillRect(list, 1, 8, 8, 100, 40, 0xFF112233u,
                 "the block background is a FillRect with the exact integer rect and ARGB");
}

void TestTransparentBackground() {
  const DisplayList list =
      BuildPage("<div style=\"background-color: transparent; width: 100px; height: 40px\"></div>");
  ExpectSize(list, 2, "a transparent background emits no FillRect");
  ExpectViewportClip(list, 800, 600, "a transparent page still gets the viewport clip pair");
}

void TestText() {
  const DisplayList list = BuildPage("<p>Hi</p>");
  ExpectSize(list, 3, "text yields exactly one DrawText between the clip pair");
  ExpectDrawText(list, 1, "Hi", 8, 24, 16.0f, 0xFF000000u,
                 "text is drawn with an opaque color at its rounded position");

  const DisplayList colored = BuildPage("<p style=\"color: #ff0000\">Hi</p>");
  ExpectSize(colored, 3, "a colored text box yields one DrawText");
  ExpectDrawText(colored, 1, "Hi", 8, 24, 16.0f, 0xFFFF0000u,
                 "the text color comes from the computed style, forced opaque");

  const DisplayList sized = BuildPage("<p style=\"font-size: 32px\">Hi</p>");
  ExpectSize(sized, 3, "a larger text box yields one DrawText");
  ExpectDrawText(sized, 1, "Hi", 8, 24, 32.0f, 0xFF000000u,
                 "the font size comes from the computed style");
}

void TestInlineBackgroundPrecedesText() {
  const DisplayList list = BuildPage("<p><span style=\"background-color: #00ff00\">ab</span></p>");
  ExpectSize(list, 4, "an inline fragment background and its text yield two page commands");
  ExpectFillRect(list, 1, 8, 24, 16, 16, 0xFF00FF00u,
                 "the inline fragment background is emitted before the text");
  ExpectDrawText(list, 2, "ab", 8, 24, 16.0f, 0xFF000000u,
                 "the inline fragment's text follows its background");
}

// div mixes inline and block content, so the inline span is wrapped in an
// anonymous block; the anonymous block itself has no style and no command.
void TestAnonymousBlockPassesThrough() {
  const DisplayList list =
      BuildPage("<div><span style=\"background-color: #0000ff\">x</span><p>y</p></div>");
  ExpectSize(list, 5, "the anonymous block contributes no command of its own");
  ExpectFillRect(list, 1, 8, 8, 8, 16, 0xFF0000FFu,
                 "the inline background is emitted from inside the anonymous block");
  ExpectDrawText(list, 2, "x", 8, 8, 16.0f, 0xFF000000u, "the inline text follows its background");
  ExpectDrawText(list, 3, "y", 8, 40, 16.0f, 0xFF000000u, "the block text follows in box order");
}

void TestPreOrder() {
  const DisplayList list = BuildPage(
      "<div style=\"background-color: #111111\"><p style=\"background-color: "
      "#222222\">T</p></div>");
  ExpectSize(list, 5, "two nested backgrounds and one text yield three page commands");
  ExpectFillRect(list, 1, 8, 8, 784, 48, 0xFF111111u, "the parent background is emitted first");
  ExpectFillRect(list, 2, 8, 24, 784, 16, 0xFF222222u, "the child background follows its parent");
  ExpectDrawText(list, 3, "T", 8, 24, 16.0f, 0xFF000000u, "text follows both backgrounds");
}

void TestFractionalGeometry() {
  const DisplayList list = BuildPage(
      "<div style=\"background-color: #336699; width: 10.5px; height: 5.5px; "
      "margin-left: 3.5px; margin-top: 2.5px\"></div>");
  ExpectSize(list, 3, "fractional geometry yields one FillRect");
  ExpectFillRect(list, 1, 12, 11, 11, 6, 0xFF336699u,
                 "fractional geometry rounds with std::lround, half away from zero");

  const DisplayList text = BuildPage("<p style=\"margin-left: 3.5px\">Hi</p>");
  ExpectSize(text, 3, "a fractional text position yields one DrawText");
  ExpectDrawText(text, 1, "Hi", 12, 24, 16.0f, 0xFF000000u,
                 "text geometry rounds with std::lround");
}

// A background that is partly visible keeps its negative device coordinates so
// the rasterizer does the clipping (M7 behavior); since M11 a background that is
// entirely outside the viewport is culled and paints nothing either way.
void TestNegativeCoordinates() {
  const DisplayList list = BuildPage(
      "<div style=\"background-color: #336699; width: 10px; height: 10px; "
      "margin-left: -12px\"></div>");
  ExpectSize(list, 3, "a partially visible negative-margin background yields one FillRect");
  ExpectFillRect(list, 1, -4, 8, 10, 10, 0xFF336699u,
                 "negative coordinates pass through to the renderer");

  const DisplayList offscreen = BuildPage(
      "<div style=\"background-color: #336699; width: 10px; height: 10px; "
      "margin-left: -20px\"></div>");
  ExpectSize(offscreen, 2, "a fully off-screen negative-margin background is culled");
  ExpectViewportClip(offscreen, 800, 600, "the culled page keeps the viewport clip pair");
}

// The same page with scroll_y 40 differs only by the y translation of the page
// commands; the clip pair is identical.
void TestScrollTranslation() {
  const std::string html =
      "<div style=\"background-color: #102030; width: 100px; height: 40px\"></div>"
      "<p style=\"color: #00ff00\">Hi</p>";
  const DisplayList top = BuildPage(html, 320, 200, 0);
  const DisplayList scrolled = BuildPage(html, 320, 200, 40);
  ExpectSize(top, 4, "an unscrolled page is clipped and painted in pre-order");
  ExpectSize(scrolled, 4, "a scrolled page has the same commands");
  ExpectViewportClip(top, 320, 200, "the unscrolled list is clipped to the viewport");
  ExpectViewportClip(scrolled, 320, 200, "the scrolled list is clipped to the same viewport");
  ExpectFillRect(top, 1, 8, 8, 100, 40, 0xFF102030u, "the background starts at its page position");
  ExpectFillRect(scrolled, 1, 8, -32, 100, 40, 0xFF102030u,
                 "scrolling translates the background y by -scroll_y and keeps x");
  ExpectDrawText(top, 2, "Hi", 8, 64, 16.0f, 0xFF00FF00u, "the text starts at its page position");
  ExpectDrawText(scrolled, 2, "Hi", 8, 24, 16.0f, 0xFF00FF00u,
                 "scrolling translates the text y by -scroll_y and keeps x");

  bool only_y_translated = top.size() == scrolled.size();
  for (std::size_t index = 1; only_y_translated && index + 1 < top.size(); ++index) {
    const Command& before = top.commands()[index];
    const Command& after = scrolled.commands()[index];
    only_y_translated = SameCommandExceptY(before, after) && after.y == before.y - 40;
  }
  toy_test::Check(only_y_translated,
                  "scroll_y 0 vs 40 differs only by the y translation of the page commands");
  toy_test::Check(!top.commands().empty() && !scrolled.commands().empty() &&
                      SameCommand(top.commands().front(), scrolled.commands().front()) &&
                      SameCommand(top.commands().back(), scrolled.commands().back()),
                  "scrolling leaves the viewport clip pair unchanged");
}

// A viewport smaller than the page still wraps every command in its clip: the
// commands are not truncated, the rasterizer clips them to the viewport.
void TestViewportSmallerThanContent() {
  const std::string html = "<div style=\"background-color: #223344; height: 400px\"></div>";
  const DisplayList list = BuildPage(html, 200, 100, 0);
  ExpectSize(list, 3, "a tall page under a short viewport keeps the clip pair around its commands");
  ExpectPushClip(list, 0, 0, 0, 200, 100,
                 "the viewport clip is the requested device-pixel rectangle");
  ExpectFillRect(list, 1, 8, 8, 184, 400, 0xFF223344u,
                 "the background command is not truncated; the clip bounds it");
  ExpectPopClip(list, 2, "the viewport clip is popped after the page commands");

  Surface surface(200, 300);
  surface.Fill(0xFFFFFFFFu);
  DrawDisplayList(list, surface);
  toy_test::Check(surface.At(10, 50) == 0xFF223344u, "pixels inside the viewport clip are painted");
  toy_test::Check(surface.At(10, 150) == 0xFFFFFFFFu,
                  "pixels below the viewport clip stay unpainted");
}

void TestDeterministic() {
  const std::string html =
      "<div style=\"background-color: #102030\"><p style=\"color: #00ff00\">Hello</p></div>";
  const DisplayList first = BuildPage(html);
  const DisplayList second = BuildPage(html);
  ExpectSize(first, 4, "the nested fixture yields one background and one text command");
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
// C++ stack; the non-empty tree still gets the clip pair.
void TestDeepNestingWalk() {
  constexpr int kLevels = 10000;
  std::string html;
  html.reserve(static_cast<std::size_t>(kLevels) * 5);
  for (int level = 0; level < kLevels; ++level) {
    html.append("<div>");
  }
  const DisplayList list = BuildPage(html);
  ExpectSize(list, 2, "a 10,000-level box tree is walked without recursion and paints nothing");
  ExpectViewportClip(list, 800, 600, "the deep page still gets the viewport clip pair");
}

// A replaced inline box with an image paints one DrawImage with the exact
// integer geometry of its box; the transparent page contributes no fills.
void TestDrawImageGeometry() {
  const std::shared_ptr<const Image> image = MakeImage(16, 8, 0xFFFF0000u);
  const DisplayList list = BuildImagePage("<p><img></p>", ReplacedSize{16.0f, 8.0f}, image);
  ExpectSize(list, 3, "a replaced image yields one DrawImage between the clip pair");
  ExpectViewportClip(list, 800, 600, "the image page is clipped to the viewport");
  ExpectDrawImage(list, 1, 8, 24, 16, 8, image,
                  "the image command has the replaced box's exact device-pixel geometry");
}

// The image paints after its own background and before its children (a replaced
// box has none), exactly like the documented pre-order rule.
void TestImageAfterBackground() {
  const std::shared_ptr<const Image> image = MakeImage(16, 8, 0xFF00FF00u);
  const DisplayList own = BuildImagePage("<p><img style=\"background-color: #112233\"></p>",
                                         ReplacedSize{16.0f, 8.0f}, image);
  ExpectSize(own, 4, "a replaced box with a background yields a FillRect and a DrawImage");
  ExpectFillRect(own, 1, 8, 24, 16, 8, 0xFF112233u,
                 "the replaced box's background is painted before its image");
  ExpectDrawImage(own, 2, 8, 24, 16, 8, image, "the image is painted after its own background");

  // The image is a child of the inline fragment: the ancestor background is
  // painted first, then the image.
  const DisplayList nested =
      BuildImagePage("<p><span style=\"background-color: #445566\"><img></span></p>",
                     ReplacedSize{16.0f, 8.0f}, image);
  ExpectSize(nested, 4, "an inline ancestor background and a child image yield two commands");
  ExpectFillRect(nested, 1, 8, 24, 16, 8, 0xFF445566u,
                 "the inline ancestor background is painted before its child image");
  ExpectDrawImage(nested, 2, 8, 24, 16, 8, image,
                  "the image is painted inside its inline ancestor, after its background");
}

// Scrolling translates the image y by -scroll_y and keeps x (M7 rule). An image
// that is still partly visible keeps its coordinates; one scrolled completely
// out of the viewport is culled (M11).
void TestImageScrollTranslation() {
  const std::shared_ptr<const Image> image = MakeImage(16, 8, 0xFF0000FFu);
  const std::string html = "<p><img></p>";
  const DisplayList top = BuildImagePage(html, ReplacedSize{16.0f, 8.0f}, image, 320, 200, 0);
  const DisplayList scrolled = BuildImagePage(html, ReplacedSize{16.0f, 8.0f}, image, 320, 200, 30);
  ExpectSize(top, 3, "the unscrolled image page has one DrawImage");
  ExpectSize(scrolled, 3, "the partly visible scrolled image page has the same commands");
  ExpectDrawImage(top, 1, 8, 24, 16, 8, image, "the image starts at its page position");
  ExpectDrawImage(scrolled, 1, 8, -6, 16, 8, image,
                  "scrolling translates the image y by -scroll_y and keeps x");
  toy_test::Check(SameCommand(top.commands().front(), scrolled.commands().front()),
                  "the image viewport clip is unchanged by scrolling");

  const DisplayList offscreen =
      BuildImagePage(html, ReplacedSize{16.0f, 8.0f}, image, 320, 200, 40);
  ExpectSize(offscreen, 2, "an image scrolled completely out of the viewport is culled");
  ExpectViewportClip(offscreen, 320, 200, "the culled image page keeps the viewport clip pair");
}

// Geometry is rounded with std::lround, the same rounding as the box dump.
void TestImageFractionalGeometry() {
  const std::shared_ptr<const Image> image = MakeImage(16, 8, 0xFFFFFFFFu);
  // width 10.5 keeps the intrinsic 2:1 ratio, so the replaced box is 10.5x5.25.
  const DisplayList list =
      BuildImagePage("<p><img style=\"width:10.5px\"></p>", ReplacedSize{16.0f, 8.0f}, image);
  ExpectSize(list, 3, "a fractional replaced box still yields one DrawImage");
  ExpectDrawImage(list, 1, 8, 24, 11, 5, image,
                  "fractional image geometry rounds to device pixels with std::lround");
}

// An empty image map produces exactly the M7 command list: no DrawImage appears
// even though the box tree contains a replaced box.
void TestEmptyImageMapKeepsM7List() {
  const std::string html = "<p>Hi</p><p><img></p>";
  const DisplayList m7 = BuildPage(html);
  const DisplayList replaced = BuildReplacedPageWithoutImages(html, ReplacedSize{16.0f, 8.0f});
  ExpectSize(m7, 3, "the image-less page is the M7 clip + text + pop list");
  ExpectSize(replaced, 3, "a replaced box without a decoded image emits no DrawImage");
  bool identical = m7.size() == replaced.size();
  for (std::size_t index = 0; identical && index < m7.size(); ++index) {
    identical = SameCommand(m7.commands()[index], replaced.commands()[index]);
  }
  if (!identical) {
    std::fprintf(stderr,
                 "paint display list test: an empty image map changed the image-less page list\n");
  }
  toy_test::Check(identical, "an empty image map produces exactly the M7 command list");
}

// An image whose node id is not in the map paints nothing: paint keys images by
// DOM node id, so a missing (or wrong) key means "no decoded image".
void TestMissingNodeIdPaintsNoImage() {
  const std::shared_ptr<const Image> image = MakeImage(16, 8, 0xFF00FFFFu);
  const Document document = toy::dom::ParseDocument("<p><img></p>");
  const NodeId image_node = FindImageNode(document);
  toy_test::Check(image_node != toy::dom::kInvalidNode, "the fixture contains an image element");
  ReplacedSizes sizes;
  sizes.emplace(image_node, ReplacedSize{16.0f, 8.0f});
  ImageMap images;
  // Deliberately key the image by the <p> node instead of the <img> node.
  for (std::size_t index = 0; index < document.size(); ++index) {
    const NodeId id = static_cast<NodeId>(index);
    if (document.Get(id).type == toy::dom::NodeType::kElement && document.Get(id).name == "p") {
      images.emplace(id, image);
      break;
    }
  }
  const DisplayList list = BuildPageWithMeasure(document, &sizes, images, MeasureWithBitmapFont,
                                                FontRunsProvider{}, 800, 600, 0);
  ExpectSize(list, 2, "an image keyed by the wrong node id paints no DrawImage");
  ExpectViewportClip(list, 800, 600, "the page with an unmatched image key keeps the clip pair");
}

// A replaced element with display:block is not supported by this MVP: it lays
// out as an empty block and paint must not draw an image for it.
void TestBlockReplacedPaintsNoImage() {
  const std::shared_ptr<const Image> image = MakeImage(16, 8, 0xFFFF00FFu);
  const DisplayList list =
      BuildImagePage("<p><img style=\"display:block\"></p>", ReplacedSize{16.0f, 8.0f}, image);
  ExpectSize(list, 2, "a display:block image paints nothing (empty block MVP limit)");
  ExpectViewportClip(list, 800, 600, "the block-image page keeps the viewport clip pair");
}

// The null-font path must reproduce the pre-M8b command list exactly: every
// command matches the hand-built M8a list (the 5-argument build semantics), no
// command carries a font, and rendering the list yields the same bitmap pixels.
void TestNullFontMatchesPreM8bList() {
  const std::shared_ptr<const Image> image = MakeImage(16, 8, 0xFFFF0000u);
  const std::string html =
      "<div style=\"background-color: #102030; width: 100px; height: 40px\"></div>"
      "<p><img></p><p style=\"color: #00ff00\">Hi</p>";
  const DisplayList list = BuildFontImagePage(html, ReplacedSize{16.0f, 8.0f}, image, nullptr);
  ExpectSize(list, 5, "the mixed page yields clip, background, image, text and pop");

  // The exact M8a command sequence for this page, built by hand.
  DisplayList reference;
  reference.PushClip(0, 0, 800, 600);
  reference.FillRect(8, 8, 100, 40, 0xFF102030u);
  reference.DrawImage(image, 8, 64, 16, 8);
  reference.DrawText("Hi", 8, 104, 16.0f, 0xFF00FF00u);
  reference.PopClip();

  bool identical = list.size() == reference.size();
  for (std::size_t index = 0; identical && index < list.size(); ++index) {
    identical = SameCommand(list.commands()[index], reference.commands()[index]);
  }
  if (!identical) {
    std::fprintf(stderr,
                 "paint display list test: the null-font list differs from the pre-M8b list\n");
  }
  toy_test::Check(identical, "a null font reproduces the pre-M8b command list command by command");
  toy_test::Check(NoCommandHasFont(list), "no command of the null-font list carries a font");

  // Rendering must be unchanged too: the bitmap text pixels and the image are
  // identical to the pre-M8b list rendered the same way.
  const Surface actual = RenderList(list, 800, 600);
  const Surface expected = RenderList(reference, 800, 600);
  toy_test::Check(SamePixels(actual, expected), "a null font keeps the rendered bitmap pixels");
  toy_test::Check(actual.At(9, 107) == 0xFF00FF00u && actual.At(8, 107) == 0xFFFFFFFFu,
                  "the null-font list still paints the opaque bitmap glyph at the box top");
}

// With a real font each text box becomes a baseline-positioned text command:
// baseline = round(box.y) - scroll_y + round(ascent at the computed size).
void TestRealFontBaseline(const std::shared_ptr<const Font>& font) {
  toy_test::Check(std::lround(font->AscentPx(16.0f)) == 13,
                  "the fixture font ascent at 16 px rounds to 13 device pixels");

  const DisplayList bitmap = BuildPage("<p>Hi</p>");
  ExpectSize(bitmap, 3, "the bitmap page has one text command between its clip pair");
  ExpectDrawText(bitmap, 1, "Hi", 8, 24, 16.0f, 0xFF000000u,
                 "the null-font page keeps the M8a bitmap command at the box top");

  const DisplayList real = BuildFontPage("<p>Hi</p>", font);
  ExpectSize(real, 3, "the real-font page has one text command between its clip pair");
  ExpectViewportClip(real, 800, 600, "the real-font page keeps the viewport clip pair");
  ExpectDrawTextWithFont(real, 1, font, "Hi", 8, 37, 16.0f, 0xFF000000u,
                         "the 16 px baseline is 24 + round(12.8) = 37");
  toy_test::Check(real.commands()[1].x == bitmap.commands()[1].x &&
                      real.commands()[1].text == bitmap.commands()[1].text &&
                      real.commands()[1].font_size_px == bitmap.commands()[1].font_size_px,
                  "selecting a real font keeps the text box x, text and size");
  toy_test::Check(real.commands()[1].y - bitmap.commands()[1].y == 13,
                  "selecting a real font moves the text command down by the rounded ascent");
  toy_test::Check(real.commands()[1].font == font && bitmap.commands()[1].font == nullptr,
                  "only the real-font path records a font on the text command");
}

// The ascent is rounded to nearest device pixel at every computed size; the
// fixture font's 0.8 em ascent gives both a rounded-up and a rounded-down case.
void TestRealFontBaselineRounding(const std::shared_ptr<const Font>& font) {
  toy_test::Check(std::lround(font->AscentPx(32.0f)) == 26,
                  "the fixture font ascent at 32 px rounds to 26 device pixels");
  toy_test::Check(std::lround(font->AscentPx(8.0f)) == 6,
                  "the fixture font ascent at 8 px rounds to 6 device pixels");

  const DisplayList large =
      BuildFontPage("<p style=\"color: #ff0000; font-size: 32px\">H</p>", font);
  ExpectSize(large, 3, "a 32 px real-font page has one text command");
  ExpectDrawTextWithFont(large, 1, font, "H", 8, 50, 32.0f, 0xFFFF0000u,
                         "the 32 px baseline is 24 + round(25.6) = 50");

  const DisplayList small = BuildFontPage("<p style=\"font-size: 8px\">H</p>", font);
  ExpectSize(small, 3, "an 8 px real-font page has one text command");
  ExpectDrawTextWithFont(small, 1, font, "H", 8, 30, 8.0f, 0xFF000000u,
                         "the 8 px baseline is 24 + round(6.4) = 30");
}

// Scrolling translates the baseline by -scroll_y; x, ordering and the viewport
// clip pair stay exactly as in the bitmap path.
void TestRealFontScrollAndClip(const std::shared_ptr<const Font>& font) {
  const std::string html = "<p>Hi</p>";
  const DisplayList top = BuildFontPage(html, font, 320, 200, 0);
  const DisplayList scrolled = BuildFontPage(html, font, 320, 200, 40);
  ExpectSize(top, 3, "the unscrolled real-font page is painted in pre-order");
  ExpectSize(scrolled, 3, "the scrolled real-font page has the same commands");
  ExpectViewportClip(top, 320, 200, "the real-font page is clipped to the viewport");
  ExpectDrawTextWithFont(top, 1, font, "Hi", 8, 37, 16.0f, 0xFF000000u,
                         "the real-font baseline starts at its page position");
  ExpectDrawTextWithFont(scrolled, 1, font, "Hi", 8, -3, 16.0f, 0xFF000000u,
                         "scrolling translates the real-font baseline by -scroll_y and keeps x");
  toy_test::Check(SameCommand(top.commands().front(), scrolled.commands().front()) &&
                      SameCommand(top.commands().back(), scrolled.commands().back()),
                  "scrolling leaves the real-font viewport clip pair unchanged");
}

// Paint order is unchanged with a real font: background, then image, then text.
void TestRealFontOrderingWithImage(const std::shared_ptr<const Font>& font) {
  const std::shared_ptr<const Image> image = MakeImage(16, 8, 0xFFFF0000u);
  const std::string html =
      "<div style=\"background-color: #102030; width: 100px; height: 40px\"></div>"
      "<p><img></p><p>Hi</p>";
  const DisplayList list = BuildFontImagePage(html, ReplacedSize{16.0f, 8.0f}, image, font);
  ExpectSize(list, 5, "the mixed real-font page keeps clip, background, image, text and pop");
  ExpectPushClip(list, 0, 0, 0, 800, 600, "the mixed real-font page keeps the viewport clip");
  ExpectFillRect(list, 1, 8, 8, 100, 40, 0xFF102030u, "the background is painted first");
  ExpectDrawImage(list, 2, 8, 64, 16, 8, image, "the image follows the background");
  ExpectDrawTextWithFont(list, 3, font, "Hi", 8, 117, 16.0f, 0xFF000000u,
                         "real-font text follows the image at 104 + round(12.8) = 117");
  ExpectPopClip(list, 4, "the mixed real-font page keeps the viewport pop");
}

// Building the same real-font page twice yields identical command lists.
void TestRealFontDeterministic(const std::shared_ptr<const Font>& font) {
  const std::string html =
      "<div style=\"background-color: #102030\"><p style=\"color: #00ff00\">Hello</p></div>";
  const DisplayList first = BuildFontPage(html, font);
  const DisplayList second = BuildFontPage(html, font);
  ExpectSize(first, 4, "the real-font fixture yields one background and one text command");
  bool identical = first.size() == second.size();
  for (std::size_t index = 0; identical && index < first.size(); ++index) {
    identical = SameCommand(first.commands()[index], second.commands()[index]);
  }
  if (!identical) {
    std::fprintf(stderr, "paint display list test: two real-font builds of one page differ\n");
  }
  toy_test::Check(identical, "building the same real-font page twice yields identical commands");

  // Font selection only changes the text commands; everything else is frozen.
  const DisplayList bitmap = BuildPage(html);
  bool same_except_text = bitmap.size() == first.size();
  for (std::size_t index = 0; same_except_text && index < first.size(); ++index) {
    const Command& before = bitmap.commands()[index];
    const Command& after = first.commands()[index];
    same_except_text = before.kind == after.kind && before.x == after.x;
    if (after.kind == CommandKind::kDrawText) {
      same_except_text = same_except_text && after.font == font && before.font == nullptr &&
                         after.y == before.y + 13;
    } else {
      same_except_text = same_except_text && SameCommand(before, after);
    }
  }
  toy_test::Check(same_except_text,
                  "selecting a real font changes only the text command's font and baseline");
}

// The runs provider is optional: a null provider and an explicitly empty
// provider must both reproduce the frozen pre-M10 bitmap command list exactly.
void TestEmptyProviderMatchesNullProvider() {
  const std::string html = "<div style=\"background-color: #102030\"><p>Hi</p><p>ABC</p></div>";
  const Document document = toy::dom::ParseDocument(html);
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  LayoutOptions options;
  options.viewport_width_px = 800.0f;
  const BoxTree boxes = LayoutDocument(document, styles, options, MeasureWithBitmapFont);
  const ImageMap images;
  const Viewport viewport{800, 600, 0};
  const DisplayList empty_provider =
      BuildDisplayList(document, styles, boxes, viewport, images, FontRunsProvider{});
  const DisplayList null_provider =
      BuildDisplayList(document, styles, boxes, viewport, images, nullptr);
  bool identical = empty_provider.size() == null_provider.size();
  for (std::size_t index = 0; identical && index < empty_provider.size(); ++index) {
    identical = SameCommand(empty_provider.commands()[index], null_provider.commands()[index]);
  }
  if (!identical) {
    std::fprintf(stderr,
                 "paint display list test: the empty provider differs from the null provider\n");
  }
  toy_test::Check(identical, "an empty provider reproduces the null-provider bitmap list");
  toy_test::Check(NoCommandHasFont(empty_provider), "the empty-provider list has no real font");
}

// A text box whose characters resolve to different faces becomes one
// DrawTextWithFont per run, in order; the x pen starts at box.x and advances by
// each run's shaped width, so the runs stay contiguous; all runs share the
// box's baseline (layout computed the line ascent).
void TestMultiRunPenAdvance(const std::shared_ptr<const Font>& latin,
                            const std::shared_ptr<const Font>& han) {
  const std::string html = "<p>A\u4e2dB\u6587</p>";
  const DisplayList list = BuildTwoFontPage(html, latin, han);
  ExpectSize(list, 6, "four mixed-script runs yield four text commands between the clip pair");
  ExpectViewportClip(list, 800, 600, "the runs page keeps the viewport clip pair");
  const int baseline_y = 24 + static_cast<int>(std::lround(latin->AscentPx(16.0f)));
  // The pen advances by the exact shaped widths; each command x is lround(pen).
  float pen_x = 8.0f;
  ExpectDrawTextWithFont(list, 1, latin, "A", static_cast<int>(std::lround(pen_x)), baseline_y,
                         16.0f, 0xFF000000u, "the first ASCII run starts at the box x");
  pen_x += latin->MeasureTextWidth("A", 16.0f);
  ExpectDrawTextWithFont(list, 2, han, "\u4e2d", static_cast<int>(std::lround(pen_x)), baseline_y,
                         16.0f, 0xFF000000u,
                         "the Han run advances the pen by the first run's shaped width");
  pen_x += han->MeasureTextWidth("\u4e2d", 16.0f);
  ExpectDrawTextWithFont(list, 3, latin, "B", static_cast<int>(std::lround(pen_x)), baseline_y,
                         16.0f, 0xFF000000u,
                         "the next ASCII run advances the pen by the Han run's shaped width");
  pen_x += latin->MeasureTextWidth("B", 16.0f);
  ExpectDrawTextWithFont(list, 4, han, "\u6587", static_cast<int>(std::lround(pen_x)), baseline_y,
                         16.0f, 0xFF000000u,
                         "the last Han run advances the pen by the previous run's shaped width");
  std::string painted;
  for (std::size_t index = 1; index + 1 < list.size(); ++index) {
    painted += list.commands()[index].text;
  }
  toy_test::Check(painted == "A\u4e2dB\u6587", "the painted runs concatenate to the box text");
}

// The baseline comes from Box::baseline_offset_px, not from each run's own
// ascent: two inline text boxes with different font sizes on one line share the
// line's maximum ascent (the 32 px box's), so both baselines must be equal.
void TestBaselineFromBoxBaselineOffset(const std::shared_ptr<const Font>& font) {
  const DisplayList list = BuildFontPage(
      "<p><span style=\"font-size: 32px\">A</span><span style=\"font-size: 16px\">B</span></p>",
      font);
  ExpectSize(list, 4, "two differently sized text boxes yield two text commands");
  ExpectViewportClip(list, 800, 600, "the mixed-size page keeps the viewport clip pair");
  // Layout sets the line ascent 25.6 px on both boxes: round(24) + round(25.6).
  ExpectDrawTextWithFont(list, 1, font, "A", 8, 50, 32.0f, 0xFF000000u,
                         "the large text sits on the line baseline");
  ExpectDrawTextWithFont(list, 2, font, "B", 27, 50, 16.0f, 0xFF000000u,
                         "the small text shares the line baseline instead of using its own ascent");
  toy_test::Check(list.commands()[1].y == list.commands()[2].y,
                  "both text boxes of one line share the line baseline");
}

// A provider may return no runs for one box while returning real runs for
// another: the empty box keeps the frozen bitmap DrawText, the other becomes a
// real-font command.
void TestPerBoxEmptyRunsKeepsBitmap(const std::shared_ptr<const Font>& font) {
  const FontRunsProvider mixed = [font](const toy::style::ComputedStyle&,
                                        std::string_view text_utf8) {
    if (text_utf8 == "Hi") {
      return std::vector<FontRun>{FontRun{font, text_utf8}};
    }
    return std::vector<FontRun>{};
  };
  const toy::layout::MeasureText measure = [font](std::string_view text_utf8,
                                                  const toy::style::ComputedStyle& style) {
    return MeasureWithFont(font, text_utf8, style);
  };
  const ImageMap images;
  const DisplayList list = BuildPageWithMeasure(toy::dom::ParseDocument("<p>Hi</p><p>AB</p>"),
                                                nullptr, images, measure, mixed, 800, 600, 0);
  ExpectSize(list, 4, "one real-font box and one bitmap box yield two text commands");
  ExpectViewportClip(list, 800, 600, "the mixed-provider page keeps the viewport clip pair");
  ExpectDrawTextWithFont(list, 1, font, "Hi", 8, 37, 16.0f, 0xFF000000u,
                         "the box with runs is painted with the real font on its baseline");
  ExpectDrawText(list, 2, "AB", 8, 72, 16.0f, 0xFF000000u,
                 "the box without runs keeps the frozen bitmap DrawText at the box top");
}

// --- M11 viewport culling ----------------------------------------------------

// A deterministic tall page, much taller than the viewport, mixing a very tall
// background (visible at almost every scroll offset), repeated tall background
// blocks, paragraphs, an image, text whose box starts left of the viewport, an
// unbreakable word overflowing the right edge and Han text. At any scroll
// offset most of it is off-screen, so culling has plenty of commands to drop.
std::string CullingPageHtml() {
  std::string html =
      "<!doctype html><html><head><style>"
      "div.block { height: 120px }"
      "p { margin: 4px 8px }"
      "</style></head><body>";
  // One background taller than several viewports: it must never be culled while
  // any of its area is visible.
  html += "<div style=\"background-color: #204060; height: 3600px\"></div>";
  for (int index = 0; index < 24; ++index) {
    char color[16];
    std::snprintf(color, sizeof(color), "#%02x%02x%02x", 0x20 + (index * 11) % 0x60,
                  0x30 + (index * 7) % 0x60, 0x40 + (index * 5) % 0x60);
    html += "<div class=\"block\" style=\"background-color: ";
    html += color;
    html += "\"></div><p>Row ";
    html += std::to_string(index);
    html += " quick brown fox</p>";
    if (index == 3) {
      // The text box starts left of the viewport: only part of it is painted.
      html += "<p style=\"margin-left: -600px\">OffLeft</p>";
      // One unbreakable word overflowing the right viewport edge.
      html += "<p style=\"margin-left: 300px\">OverflowingWordABCDEFGHIJKLMNOPQRSTUVWXYZ</p>";
    }
    if (index == 5) {
      html += "<p><img></p>";
    }
    if (index == 7) {
      html += "<p>\u4e2d\u6587\u4f60\u597d</p>";
    }
    if (index == 2) {
      // Real-font overhang cases: italic and bold faces at a larger size, so
      // anti-aliased ink near the viewport edges is part of the comparison.
      html += "<p style=\"font-style: italic; font-size: 24px\">Italic edge text</p>";
    }
    if (index == 9) {
      html += "<p style=\"font-weight: 700; font-size: 20px\">Bold edge text</p>";
    }
  }
  html += "</body></html>";
  return html;
}

std::size_t CountKind(const DisplayList& list, CommandKind kind) {
  std::size_t count = 0;
  for (const Command& command : list.commands()) {
    if (command.kind == kind) {
      ++count;
    }
  }
  return count;
}

// Builds the box tree once, then compares a culled viewport render at many
// scroll offsets with the same page rendered in one tall reference viewport that
// contains every command. Any command wrongly culled at a viewport edge (or
// wrongly kept, though that only changes the list size) shows up as a pixel
// difference. Also asserts the frozen single viewport clip pair and that culling
// actually removed commands.
void CheckCullingMatchesFullPage(const std::string& html, int width, int height,
                                 const toy::layout::MeasureText& measure,
                                 const FontRunsProvider& font_runs, bool with_image,
                                 bool sweep_every_offset = false) {
  const Document document = toy::dom::ParseDocument(html);
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  ReplacedSizes sizes;
  ImageMap images;
  if (with_image) {
    const NodeId image_node = FindImageNode(document);
    if (image_node != toy::dom::kInvalidNode) {
      sizes.emplace(image_node, ReplacedSize{40.0f, 30.0f});
      images.emplace(image_node, MakeImage(40, 30, 0xFF3366CCu));
    }
  }
  LayoutOptions options;
  options.viewport_width_px = static_cast<float>(width);
  options.replaced_sizes = &sizes;
  const BoxTree boxes = LayoutDocument(document, styles, options, measure);

  const float content_height_px = toy::layout::ContentHeight(boxes);
  const long long rounded_content =
      static_cast<long long>(std::ceil(static_cast<double>(content_height_px)));
  // The reference viewport is taller than the page plus the text culling margin,
  // so every command that can paint is kept in the reference list.
  const int reference_height = static_cast<int>(std::max<long long>(1, rounded_content) + 512);
  const DisplayList full = BuildDisplayList(
      document, styles, boxes, Viewport{width, reference_height, 0}, images, font_runs);
  toy_test::Check(
      CountKind(full, CommandKind::kPushClip) == 1 && CountKind(full, CommandKind::kPopClip) == 1,
      "the full-content reference list has exactly one viewport clip pair");
  const Surface reference = RenderList(full, width, reference_height);

  const int max_scroll =
      static_cast<int>(std::max<long long>(0, rounded_content - static_cast<long long>(height)));
  std::vector<int> offsets = {
      0, 1, 2, height / 2, height - 1, max_scroll / 2, max_scroll - 1, max_scroll};
  if (sweep_every_offset) {
    for (int scroll_y = 0; scroll_y <= max_scroll; ++scroll_y) {
      offsets.push_back(scroll_y);
    }
  } else {
    for (int scroll_y = 0; scroll_y <= max_scroll; scroll_y += 97) {
      offsets.push_back(scroll_y);
    }
  }
  std::sort(offsets.begin(), offsets.end());
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());

  bool compared = false;
  bool culled = false;
  for (const int scroll_y : offsets) {
    if (scroll_y < 0 || scroll_y > max_scroll || scroll_y + height > reference_height) {
      continue;
    }
    const DisplayList list = BuildDisplayList(document, styles, boxes,
                                              Viewport{width, height, scroll_y}, images, font_runs);
    toy_test::Check(
        CountKind(list, CommandKind::kPushClip) == 1 && CountKind(list, CommandKind::kPopClip) == 1,
        "a culled list keeps exactly one viewport clip pair");
    ExpectViewportClip(list, width, height, "a culled list keeps the viewport clip pair");
    toy_test::Check(list.size() <= full.size(), "culling only removes commands");
    const Surface culled_surface = RenderList(list, width, height);
    bool identical = true;
    for (int y = 0; identical && y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if (culled_surface.At(x, y) != reference.At(x, y + scroll_y)) {
          identical = false;
          break;
        }
      }
    }
    if (!identical) {
      std::fprintf(stderr,
                   "paint display list test: the culled render differs from the full-content "
                   "reference at scroll_y=%d\n",
                   scroll_y);
    }
    toy_test::Check(identical, "a culled render is pixel-identical to the full-content reference");
    culled = culled || list.size() < full.size();
    compared = true;
  }
  toy_test::Check(compared, "at least one scroll offset of the culling page was compared");
  toy_test::Check(culled, "culling removes most off-screen commands");
}

// 1x1 background pixels placed exactly on the viewport edges (column 0 and
// column width - 1) on consecutive page rows, swept over every scroll offset and
// compared with a hand-built expected frame. The reference is computed from the
// known page geometry instead of from another display list, so a wrong visibility
// rule cannot hide by being applied to both sides: any off-by-one that drops the
// only visible pixel of an edge command (or keeps a command that paints no
// pixel) shows up here.
void TestCullingExactViewportEdges() {
  constexpr int kWidth = 64;
  constexpr int kHeight = 40;
  constexpr int kRows = 120;
  constexpr std::uint32_t kRed = 0xFFFF0000u;
  constexpr std::uint32_t kBlue = 0xFF0000FFu;
  std::string html = "<!doctype html><html><body>";
  for (int row = 0; row < kRows; ++row) {
    // The body margin is 8 px, so margin-left -8 puts a 1 px block at x = 0 and a
    // block at x = width - 1 starts at body content x + (width - 9).
    html +=
        "<div style=\"background-color: #ff0000; width: 1px; height: 1px; "
        "margin-left: -8px\"></div>";
    html +=
        "<div style=\"background-color: #0000ff; width: 1px; height: 1px; "
        "margin-left: ";
    html += std::to_string(kWidth - 9);
    html += "px\"></div>";
  }
  html += "</body></html>";
  const Document document = toy::dom::ParseDocument(html);
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  LayoutOptions options;
  options.viewport_width_px = static_cast<float>(kWidth);
  const BoxTree boxes = LayoutDocument(document, styles, options, MeasureWithBitmapFont);
  const ImageMap images;
  const int content_height =
      static_cast<int>(std::ceil(static_cast<double>(toy::layout::ContentHeight(boxes)) - 1.0e-4));

  // Page row of each 1 px block: the stacked blocks start at the body content
  // top (8 px) and then advance one row each.
  const int first_row_y = 8;
  const int max_scroll = std::max(0, content_height - kHeight);
  bool compared = false;
  for (int scroll_y = 0; scroll_y <= max_scroll; ++scroll_y) {
    const DisplayList list = BuildDisplayList(
        document, styles, boxes, Viewport{kWidth, kHeight, scroll_y}, images, FontRunsProvider{});
    ExpectViewportClip(list, kWidth, kHeight, "the edge-pixel page keeps the viewport clip pair");
    const Surface actual = RenderList(list, kWidth, kHeight);

    Surface expected(kWidth, kHeight);
    expected.Fill(0xFFFFFFFFu);
    for (int row = 0; row < 2 * kRows; ++row) {
      const int view_y = first_row_y + row - scroll_y;
      if (view_y < 0 || view_y >= kHeight) {
        continue;
      }
      const bool left_edge = row % 2 == 0;
      expected.FillRect(left_edge ? 0 : kWidth - 1, view_y, 1, 1, left_edge ? kRed : kBlue);
    }
    bool identical = true;
    for (int y = 0; identical && y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        if (actual.At(x, y) != expected.At(x, y)) {
          identical = false;
          break;
        }
      }
    }
    if (!identical) {
      std::fprintf(stderr,
                   "paint display list test: edge-pixel culling differs from the expected frame "
                   "at scroll_y=%d\n",
                   scroll_y);
    }
    toy_test::Check(identical, "edge pixels are culled exactly at the viewport boundary");
    compared = true;
  }
  toy_test::Check(compared, "at least one scroll offset of the edge-pixel page was compared");
}

void TestCullingBitmap() {
  CheckCullingMatchesFullPage(CullingPageHtml(), 320, 200, MeasureWithBitmapFont,
                              FontRunsProvider{}, true);
}

// The fixture face for a computed style, mirroring the shell's automatic
// selection closely enough to exercise real bold/italic ink: the italic and bold
// fixture faces are used for the corresponding computed styles, regular
// otherwise. Layout measurement and painting use the same mapping.
const std::shared_ptr<const Font>& StyleFace(const std::shared_ptr<const Font>& regular,
                                             const std::shared_ptr<const Font>& bold,
                                             const std::shared_ptr<const Font>& italic,
                                             const toy::style::ComputedStyle& style) {
  if (style.font_style == toy::style::FontStyle::kItalic) {
    return italic;
  }
  if (style.font_weight >= 700) {
    return bold;
  }
  return regular;
}

TextMetrics MeasureWithStyleFonts(const std::shared_ptr<const Font>& regular,
                                  const std::shared_ptr<const Font>& bold,
                                  const std::shared_ptr<const Font>& italic,
                                  std::string_view text_utf8,
                                  const toy::style::ComputedStyle& style) {
  const float font_size_px = style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
  const std::shared_ptr<const Font>& font = StyleFace(regular, bold, italic, style);
  return TextMetrics{font->MeasureTextWidth(text_utf8, font_size_px),
                     font->LineHeightPx(font_size_px), font->AscentPx(font_size_px)};
}

FontRunsProvider StyleAwareProvider(const std::shared_ptr<const Font>& regular,
                                    const std::shared_ptr<const Font>& bold,
                                    const std::shared_ptr<const Font>& italic) {
  return
      [regular, bold, italic](const toy::style::ComputedStyle& style, std::string_view text_utf8) {
        if (text_utf8.empty()) {
          return std::vector<FontRun>{};
        }
        return std::vector<FontRun>{FontRun{StyleFace(regular, bold, italic, style), text_utf8}};
      };
}

void TestCullingRealFont(const std::shared_ptr<const Font>& regular,
                         const std::shared_ptr<const Font>& bold,
                         const std::shared_ptr<const Font>& italic) {
  const toy::layout::MeasureText measure =
      [regular, bold, italic](std::string_view text_utf8, const toy::style::ComputedStyle& style) {
        return MeasureWithStyleFonts(regular, bold, italic, text_utf8, style);
      };
  CheckCullingMatchesFullPage(CullingPageHtml(), 320, 200, measure,
                              StyleAwareProvider(regular, bold, italic), true);
}

void TestCullingTwoFonts(const std::shared_ptr<const Font>& latin,
                         const std::shared_ptr<const Font>& han) {
  const toy::layout::MeasureText measure = [latin, han](std::string_view text_utf8,
                                                        const toy::style::ComputedStyle& style) {
    return MeasureWithTwoFonts(latin, han, text_utf8, style);
  };
  CheckCullingMatchesFullPage(CullingPageHtml(), 320, 200, measure, TwoFontProvider(latin, han),
                              true);
}

// A page of stacked tall backgrounds in a short viewport: culling must keep only
// the few commands that can paint, not one FillRect per background, and the
// visible output must stay exactly the reference window.
void TestCullingManyOffscreenBackgrounds() {
  std::string html = "<!doctype html><html><body>";
  for (int index = 0; index < 40; ++index) {
    char color[16];
    std::snprintf(color, sizeof(color), "#%02x%02x%02x", 0x10 + index, 0x20 + index, 0x30 + index);
    html += "<div style=\"background-color: ";
    html += color;
    html += "; height: 200px\"></div>";
  }
  html += "</body></html>";
  const Document document = toy::dom::ParseDocument(html);
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  LayoutOptions options;
  options.viewport_width_px = 320.0f;
  const BoxTree boxes = LayoutDocument(document, styles, options, MeasureWithBitmapFont);
  const ImageMap images;
  const DisplayList list =
      BuildDisplayList(document, styles, boxes, Viewport{320, 120, 0}, images, FontRunsProvider{});
  ExpectViewportClip(list, 320, 120, "the stacked-background page keeps the viewport clip pair");
  // Only the first 200 px background intersects the 120 px viewport.
  ExpectSize(list, 3, "culling keeps only the background that intersects the viewport");
  toy_test::Check(CountKind(list, CommandKind::kFillRect) == 1,
                  "the off-screen stacked backgrounds are culled");
}

}  // namespace

int main(int argc, char** argv) {
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
  TestScrollTranslation();
  TestViewportSmallerThanContent();
  TestDeterministic();
  TestDeepNestingWalk();
  TestDrawImageGeometry();
  TestImageAfterBackground();
  TestImageScrollTranslation();
  TestImageFractionalGeometry();
  TestEmptyImageMapKeepsM7List();
  TestMissingNodeIdPaintsNoImage();
  TestBlockReplacedPaintsNoImage();
  TestNullFontMatchesPreM8bList();
  TestEmptyProviderMatchesNullProvider();
  TestCullingBitmap();
  TestCullingManyOffscreenBackgrounds();
  TestCullingExactViewportEdges();

  // The real-font tests load the committed self-authored fixture font. CMake
  // passes its directory as argv[1], so no installed font is ever required.
  if (argc < 2) {
    std::fprintf(stderr, "paint display list test: missing font fixture directory argument\n");
    return 1;
  }
  const std::string font_path = std::string(argv[1]) + "/toy_test.ttf";
  std::string font_error;
  const std::shared_ptr<const Font> font = Font::LoadFromFile(font_path, &font_error);
  if (!font) {
    std::fprintf(stderr, "paint display list test: cannot load %s: %s\n", font_path.c_str(),
                 font_error.c_str());
    return 1;
  }
  // The style-aware culling page paints real bold/italic ink at the viewport
  // edges, so those faces are loaded from the same fixture directory.
  const auto load_face = [](const std::string& path, std::string* error) {
    return Font::LoadFromFile(path, error);
  };
  std::string bold_error;
  const std::shared_ptr<const Font> bold =
      load_face(std::string(argv[1]) + "/toy_test_bold.ttf", &bold_error);
  std::string italic_error;
  const std::shared_ptr<const Font> italic =
      load_face(std::string(argv[1]) + "/toy_test_italic.ttf", &italic_error);
  if (!bold || !italic) {
    std::fprintf(stderr, "paint display list test: cannot load the bold/italic fixture faces\n");
    return 1;
  }
  TestRealFontBaseline(font);
  TestRealFontBaselineRounding(font);
  TestRealFontScrollAndClip(font);
  TestRealFontOrderingWithImage(font);
  TestRealFontDeterministic(font);
  TestBaselineFromBoxBaselineOffset(font);
  TestPerBoxEmptyRunsKeepsBitmap(font);
  TestCullingRealFont(font, bold, italic);

  // The two-font tests split mixed ASCII/Han text by character coverage, like
  // the shell's system font selection does.
  const std::string cjk_path = std::string(argv[1]) + "/toy_cjk.ttf";
  std::string cjk_error;
  const std::shared_ptr<const Font> cjk = Font::LoadFromFile(cjk_path, &cjk_error);
  if (!cjk) {
    std::fprintf(stderr, "paint display list test: cannot load %s: %s\n", cjk_path.c_str(),
                 cjk_error.c_str());
    return 1;
  }
  TestMultiRunPenAdvance(font, cjk);
  TestCullingTwoFonts(font, cjk);
  return toy_test::Finish("paint display list");
}
