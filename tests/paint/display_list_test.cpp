// Tests for toy::paint::BuildDisplayList: a laid-out page becomes an ordered
// display list of background fills, image draws and text draws, wrapped in a
// viewport clip and translated by the vertical scroll offset.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

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
using toy::paint::ImageMap;
using toy::paint::Viewport;
using toy::render::Command;
using toy::render::CommandKind;
using toy::render::DisplayList;
using toy::render::DrawDisplayList;
using toy::render::Image;
using toy::render::Surface;

// The real built-in bitmap-font metrics, adapted exactly like shell does for
// layout. Paint never depends on them; the test pipeline needs a measure
// callback to lay the page out.
TextMetrics MeasureWithBitmapFont(std::string_view text_utf8, float font_size_px) {
  return TextMetrics{toy::render::MeasureTextWidth(text_utf8, font_size_px),
                     toy::render::LineHeightPx(font_size_px)};
}

// Full pipeline for a fixture page with caller-supplied replaced sizes and
// images. The layout viewport is the viewport width, exactly like shell does it.
DisplayList BuildPageForDocument(const Document& document, const ReplacedSizes* replaced_sizes,
                                 const ImageMap& images, int viewport_width, int viewport_height,
                                 int scroll_y) {
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  LayoutOptions options;
  options.viewport_width_px = static_cast<float>(viewport_width);
  options.replaced_sizes = replaced_sizes;
  const BoxTree boxes = LayoutDocument(document, styles, options, MeasureWithBitmapFont);
  const Viewport viewport{viewport_width, viewport_height, scroll_y};
  return BuildDisplayList(document, styles, boxes, viewport, images);
}

// The M7 path: no replaced elements and an empty image map.
DisplayList BuildPage(const std::string& html, int viewport_width = 800, int viewport_height = 600,
                      int scroll_y = 0) {
  const ImageMap images;
  return BuildPageForDocument(toy::dom::ParseDocument(html), nullptr, images, viewport_width,
                              viewport_height, scroll_y);
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
DisplayList BuildImagePage(const std::string& html, const ReplacedSize& intrinsic,
                           const std::shared_ptr<const Image>& image, int viewport_width = 800,
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
  return BuildPageForDocument(document, &sizes, images, viewport_width, viewport_height, scroll_y);
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
  return BuildPageForDocument(document, &sizes, images, viewport_width, viewport_height, scroll_y);
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
         lhs.font_size_px == rhs.font_size_px && lhs.image == rhs.image;
}

// Equality of everything except the y screen position, for scroll comparisons.
bool SameCommandExceptY(const Command& lhs, const Command& rhs) {
  return lhs.kind == rhs.kind && lhs.x == rhs.x && lhs.width == rhs.width &&
         lhs.height == rhs.height && lhs.color == rhs.color && lhs.text == rhs.text &&
         lhs.font_size_px == rhs.font_size_px && lhs.image == rhs.image;
}

// A default-constructed BoxTree has no root; paint must not touch the other
// inputs and must return an empty list (no clip pair either).
void TestEmptyBoxTree() {
  const Document document = toy::dom::ParseDocument("<html></html>");
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  const BoxTree boxes;
  const ImageMap images;
  const DisplayList list = BuildDisplayList(document, styles, boxes, Viewport{800, 600, 0}, images);
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

void TestNegativeCoordinates() {
  const DisplayList list = BuildPage(
      "<div style=\"background-color: #336699; width: 10px; height: 10px; "
      "margin-left: -20px\"></div>");
  ExpectSize(list, 3, "a negative-margin background yields one FillRect");
  ExpectFillRect(list, 1, -12, 8, 10, 10, 0xFF336699u,
                 "negative coordinates pass through to the renderer");
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

// Scrolling translates the image y by -scroll_y and keeps x (M7 rule).
void TestImageScrollTranslation() {
  const std::shared_ptr<const Image> image = MakeImage(16, 8, 0xFF0000FFu);
  const std::string html = "<p><img></p>";
  const DisplayList top = BuildImagePage(html, ReplacedSize{16.0f, 8.0f}, image, 320, 200, 0);
  const DisplayList scrolled = BuildImagePage(html, ReplacedSize{16.0f, 8.0f}, image, 320, 200, 40);
  ExpectSize(top, 3, "the unscrolled image page has one DrawImage");
  ExpectSize(scrolled, 3, "the scrolled image page has the same commands");
  ExpectDrawImage(top, 1, 8, 24, 16, 8, image, "the image starts at its page position");
  ExpectDrawImage(scrolled, 1, 8, -16, 16, 8, image,
                  "scrolling translates the image y by -scroll_y and keeps x");
  toy_test::Check(SameCommand(top.commands().front(), scrolled.commands().front()),
                  "the image viewport clip is unchanged by scrolling");
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
  const DisplayList list = BuildPageForDocument(document, &sizes, images, 800, 600, 0);
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
  return toy_test::Finish("paint display list");
}
