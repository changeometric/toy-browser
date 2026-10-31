// Tests for toy::render painting: display-list builders, rectangle fill with
// SrcOver blending, the integer clip stack and the built-in bitmap text.

#include "toy/render/paint.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/check.h"
#include "toy/render/surface.h"

namespace {

using toy::render::Argb;
using toy::render::Command;
using toy::render::CommandKind;
using toy::render::DisplayList;
using toy::render::DrawDisplayList;
using toy::render::Image;
using toy::render::Surface;

constexpr Argb kBlack = 0xFF000000u;
constexpr Argb kWhite = 0xFFFFFFFFu;
constexpr Argb kRed = 0xFFFF0000u;
constexpr Argb kGreen = 0xFF00FF00u;
constexpr Argb kBlue = 0xFF0000FFu;

int CountPixels(const Surface& surface, Argb color) {
  int count = 0;
  for (int y = 0; y < surface.height(); ++y) {
    for (int x = 0; x < surface.width(); ++x) {
      if (surface.At(x, y) == color) {
        ++count;
      }
    }
  }
  return count;
}

std::vector<Argb> CopyPixels(const Surface& surface) {
  std::vector<Argb> pixels;
  pixels.reserve(static_cast<std::size_t>(surface.width()) *
                 static_cast<std::size_t>(surface.height()));
  for (int y = 0; y < surface.height(); ++y) {
    for (int x = 0; x < surface.width(); ++x) {
      pixels.push_back(surface.At(x, y));
    }
  }
  return pixels;
}

// True when every pixel that differs from `background` lies inside the given
// clip rectangle.
bool PaintedOnlyInsideClip(const Surface& surface, Argb background, int clip_x, int clip_y,
                           int clip_width, int clip_height) {
  for (int y = 0; y < surface.height(); ++y) {
    for (int x = 0; x < surface.width(); ++x) {
      if (surface.At(x, y) == background) {
        continue;
      }
      const bool inside =
          x >= clip_x && x < clip_x + clip_width && y >= clip_y && y < clip_y + clip_height;
      if (!inside) {
        return false;
      }
    }
  }
  return true;
}

// Fills a fresh surface with `background`, draws `list` into it and returns it.
Surface DrawSurface(int width, int height, Argb background, const DisplayList& list) {
  Surface surface(width, height);
  surface.Fill(background);
  DrawDisplayList(list, surface);
  return surface;
}

// Builds a shared image from row-major ARGB pixels.
std::shared_ptr<const Image> MakeImage(int width, int height, std::vector<Argb> pixels) {
  auto image = std::make_shared<Image>();
  image->width = width;
  image->height = height;
  image->pixels = std::move(pixels);
  return image;
}

std::vector<Argb> RenderText(std::string_view text, int width, int height, float font_size_px) {
  DisplayList list;
  list.DrawText(text, 0, 0, font_size_px, kBlack);
  return CopyPixels(DrawSurface(width, height, kWhite, list));
}

void TestCommandBuilders() {
  DisplayList list;
  list.FillRect(1, 2, 3, 4, 0xAABBCCDDu);
  list.DrawText("hi", 5, 6, 12.5f, 0xFF112233u);
  list.PushClip(7, 8, 9, 10);
  list.PopClip();

  toy_test::Check(list.size() == 4, "four commands were appended");
  toy_test::Check(list.commands().size() == list.size(), "commands() matches size()");
  const std::vector<Command>& commands = list.commands();

  toy_test::Check(commands[0].kind == CommandKind::kFillRect, "first command is a fill rect");
  toy_test::Check(
      commands[0].x == 1 && commands[0].y == 2 && commands[0].width == 3 && commands[0].height == 4,
      "fill rect keeps its geometry");
  toy_test::Check(commands[0].color == 0xAABBCCDDu, "fill rect keeps its color");
  toy_test::Check(commands[0].text.empty() && commands[0].font_size_px == 0.0f,
                  "fill rect leaves the text fields at their defaults");

  toy_test::Check(commands[1].kind == CommandKind::kDrawText, "second command is text");
  toy_test::Check(commands[1].x == 5 && commands[1].y == 6, "text keeps its origin");
  toy_test::Check(commands[1].text == "hi", "text keeps its string");
  toy_test::Check(commands[1].font_size_px == 12.5f, "text keeps its font size");
  toy_test::Check(commands[1].color == 0xFF112233u, "text keeps its color");
  toy_test::Check(commands[1].width == 0 && commands[1].height == 0,
                  "text leaves the size fields at their defaults");

  toy_test::Check(commands[2].kind == CommandKind::kPushClip, "third command pushes a clip");
  toy_test::Check(commands[2].x == 7 && commands[2].y == 8 && commands[2].width == 9 &&
                      commands[2].height == 10,
                  "push clip keeps its rect");
  toy_test::Check(commands[2].color == 0, "push clip leaves the color at its default");

  toy_test::Check(commands[3].kind == CommandKind::kPopClip, "fourth command pops a clip");
  toy_test::Check(
      commands[3].x == 0 && commands[3].y == 0 && commands[3].width == 0 && commands[3].height == 0,
      "pop clip leaves the geometry at its defaults");
}

void TestFillRectExactColor() {
  DisplayList list;
  list.FillRect(1, 1, 2, 2, kRed);
  const Surface surface = DrawSurface(4, 4, kBlack, list);
  toy_test::Check(CountPixels(surface, kRed) == 4, "an opaque fill paints exactly width * height");
  toy_test::Check(surface.At(1, 1) == kRed && surface.At(2, 2) == kRed,
                  "the fill covers its in-bounds corners");
  toy_test::Check(surface.At(0, 0) == kBlack && surface.At(3, 3) == kBlack,
                  "the fill leaves the rest of the surface alone");

  DisplayList empty;
  const Surface untouched = DrawSurface(2, 2, kWhite, empty);
  toy_test::Check(CountPixels(untouched, kWhite) == 4,
                  "an empty display list leaves the surface untouched");
}

void TestFillRectBlending() {
  // Opaque destination: (src * 128 + dst * 127 + 127) / 255 per channel, and
  // the result is always written opaque.
  DisplayList white_list;
  white_list.FillRect(0, 0, 1, 1, 0x80FF0000u);
  const Surface over_white = DrawSurface(1, 1, kWhite, white_list);
  toy_test::Check(over_white.At(0, 0) == 0xFFFF7F7Fu,
                  "alpha 128 red over white blends to 0xFFFF7F7F");

  DisplayList dark_list;
  dark_list.FillRect(0, 0, 1, 1, 0x80406080u);
  const Surface over_dark = DrawSurface(1, 1, 0xFF102030u, dark_list);
  toy_test::Check(over_dark.At(0, 0) == 0xFF284058u,
                  "alpha 128 0x406080 over 0x102030 matches the frozen channel formula");

  DisplayList transparent_list;
  transparent_list.FillRect(0, 0, 1, 1, 0x00FF0000u);
  const Surface over_white_alpha0 = DrawSurface(1, 1, kWhite, transparent_list);
  toy_test::Check(over_white_alpha0.At(0, 0) == kWhite, "alpha 0 is a no-op");

  DisplayList transparent_destination;
  transparent_destination.FillRect(0, 0, 1, 1, 0x80FF0000u);
  Surface transparent(1, 1);
  DrawDisplayList(transparent_destination, transparent);
  toy_test::Check(transparent.At(0, 0) == 0xFF800000u,
                  "a translucent fill over transparent black still writes an opaque pixel");
}

void TestFillRectNoOps() {
  DisplayList list;
  list.FillRect(0, 0, 0, 4, kRed);
  list.FillRect(0, 0, 4, 0, kRed);
  list.FillRect(0, 0, -2, 4, kRed);
  list.FillRect(0, 0, 4, -2, kRed);
  const Surface surface = DrawSurface(4, 4, kBlack, list);
  toy_test::Check(CountPixels(surface, kBlack) == 16, "zero and negative sizes are no-ops");
}

void TestFillRectClipping() {
  DisplayList negative;
  negative.FillRect(-2, -2, 3, 3, kRed);
  const Surface top_left = DrawSurface(4, 4, kBlack, negative);
  toy_test::Check(top_left.At(0, 0) == kRed && CountPixels(top_left, kRed) == 1,
                  "a negative origin is clipped to the surface corner");

  DisplayList bottom_right;
  bottom_right.FillRect(3, 3, 10, 10, kRed);
  const Surface clipped = DrawSurface(4, 4, kBlack, bottom_right);
  toy_test::Check(clipped.At(3, 3) == kRed && CountPixels(clipped, kRed) == 1,
                  "a rectangle past the bottom-right edge is clipped");

  DisplayList oversized;
  oversized.FillRect(-10, -10, 100, 100, kWhite);
  const Surface covered = DrawSurface(4, 4, kBlack, oversized);
  toy_test::Check(CountPixels(covered, kWhite) == 16, "an oversized rectangle covers the surface");

  DisplayList outside;
  outside.FillRect(10, 10, 5, 5, kRed);
  outside.FillRect(-10, -10, 5, 5, kRed);
  const Surface untouched = DrawSurface(4, 4, kBlack, outside);
  toy_test::Check(CountPixels(untouched, kRed) == 0, "off-surface rectangles are no-ops");
}

void TestClipStack() {
  DisplayList restricted;
  restricted.PushClip(1, 1, 2, 2);
  restricted.FillRect(0, 0, 4, 4, kRed);
  const Surface restricted_surface = DrawSurface(4, 4, kBlack, restricted);
  toy_test::Check(CountPixels(restricted_surface, kRed) == 4,
                  "a clip restricts a fill to the clip rectangle");
  toy_test::Check(restricted_surface.At(1, 1) == kRed && restricted_surface.At(2, 2) == kRed,
                  "the fill paints inside the clip");
  toy_test::Check(restricted_surface.At(0, 0) == kBlack && restricted_surface.At(3, 3) == kBlack,
                  "the fill does not paint outside the clip");

  DisplayList nested;
  nested.PushClip(0, 0, 3, 3);
  nested.PushClip(1, 1, 3, 3);
  nested.FillRect(0, 0, 4, 4, kRed);
  const Surface nested_surface = DrawSurface(4, 4, kBlack, nested);
  toy_test::Check(CountPixels(nested_surface, kRed) == 4,
                  "nested clips intersect (2x2 visible area)");
  toy_test::Check(nested_surface.At(1, 1) == kRed && nested_surface.At(2, 2) == kRed,
                  "the nested intersection is painted");
  toy_test::Check(nested_surface.At(0, 0) == kBlack && nested_surface.At(3, 3) == kBlack,
                  "the nested intersection excludes both outer bands");

  DisplayList restored;
  restored.PushClip(1, 1, 1, 1);
  restored.PopClip();
  restored.FillRect(0, 0, 4, 4, kRed);
  const Surface restored_surface = DrawSurface(4, 4, kBlack, restored);
  toy_test::Check(CountPixels(restored_surface, kRed) == 16, "pop restores the previous clip");

  DisplayList popped_empty;
  popped_empty.PopClip();
  popped_empty.FillRect(0, 0, 2, 2, kRed);
  const Surface empty_pop_surface = DrawSurface(4, 4, kBlack, popped_empty);
  toy_test::Check(CountPixels(empty_pop_surface, kRed) == 4, "pop on an empty stack is a no-op");

  DisplayList partly_outside;
  partly_outside.PushClip(-2, -2, 4, 4);
  partly_outside.FillRect(0, 0, 4, 4, kRed);
  const Surface partly_surface = DrawSurface(4, 4, kBlack, partly_outside);
  toy_test::Check(CountPixels(partly_surface, kRed) == 4,
                  "a clip is intersected with the surface bounds");

  DisplayList empty_clip;
  empty_clip.PushClip(0, 0, 0, 4);
  empty_clip.FillRect(0, 0, 4, 4, kRed);
  const Surface empty_clip_surface = DrawSurface(4, 4, kBlack, empty_clip);
  toy_test::Check(CountPixels(empty_clip_surface, kRed) == 0, "a zero-size clip hides everything");
}

void TestTextSpaceAndNonPositiveSize() {
  DisplayList space;
  space.DrawText(" ", 0, 0, 16.0f, kBlack);
  const Surface space_surface = DrawSurface(40, 16, kWhite, space);
  toy_test::Check(CountPixels(space_surface, kBlack) == 0, "a space paints no pixels");
  toy_test::Check(CountPixels(space_surface, kWhite) == 40 * 16,
                  "a space leaves the surface white");

  DisplayList zero;
  zero.DrawText("H", 0, 0, 0.0f, kBlack);
  const Surface zero_surface = DrawSurface(8, 16, kWhite, zero);
  toy_test::Check(CountPixels(zero_surface, kBlack) == 0, "font size 0 paints nothing");

  DisplayList negative;
  negative.DrawText("H", 0, 0, -16.0f, kBlack);
  const Surface negative_surface = DrawSurface(8, 16, kWhite, negative);
  toy_test::Check(CountPixels(negative_surface, kBlack) == 0,
                  "a negative font size paints nothing");
}

void TestExtremeFontSizes() {
  // Page CSS can name an arbitrarily large px size. Rasterization must stay
  // bounded by the surface, never loop over the whole cell, and never overflow
  // the source-mapping arithmetic.
  constexpr int kSurfaceSize = 16;
  constexpr float kExtremeSizes[] = {200000000.0f, 1e9f};
  for (float font_size_px : kExtremeSizes) {
    DisplayList list;
    list.DrawText("HHH", 0, 0, font_size_px, kBlack);
    const Surface surface = DrawSurface(kSurfaceSize, kSurfaceSize, kWhite, list);
    toy_test::Check(CountPixels(surface, kWhite) == kSurfaceSize * kSurfaceSize,
                    "an extreme finite font size paints nothing on a small surface");
    toy_test::Check(PaintedOnlyInsideClip(surface, kWhite, 0, 0, kSurfaceSize, kSurfaceSize),
                    "an extreme finite font size stays inside the surface");
  }

  // Non-finite sizes (inf and NaN) must be rejected before any rounding.
  const float infinity = std::numeric_limits<float>::infinity();
  const float not_a_number = std::numeric_limits<float>::quiet_NaN();
  const float non_finite_sizes[] = {infinity, -infinity, not_a_number};
  for (float font_size_px : non_finite_sizes) {
    DisplayList list;
    list.DrawText("HHH", 0, 0, font_size_px, kBlack);
    const Surface surface = DrawSurface(kSurfaceSize, kSurfaceSize, kWhite, list);
    toy_test::Check(CountPixels(surface, kWhite) == kSurfaceSize * kSurfaceSize,
                    "a non-finite font size paints nothing");
  }

  // The iteration is bounded by the clip: a huge cell under a tiny clip stays
  // clipped and deterministic, and a cell larger than the whole surface paints
  // only inside the clip.
  DisplayList huge_clipped;
  huge_clipped.PushClip(4, 4, 4, 4);
  huge_clipped.DrawText("HHH", -2, -2, 200000000.0f, kBlack);
  const Surface first = DrawSurface(kSurfaceSize, kSurfaceSize, kWhite, huge_clipped);
  const Surface second = DrawSurface(kSurfaceSize, kSurfaceSize, kWhite, huge_clipped);
  toy_test::Check(PaintedOnlyInsideClip(first, kWhite, 4, 4, 4, 4),
                  "huge text only paints inside the clip");
  toy_test::Check(CopyPixels(first) == CopyPixels(second),
                  "huge text rasterization is deterministic");

  DisplayList large_cell;
  large_cell.PushClip(0, 0, 4, 16);
  large_cell.DrawText("H", -16, -56, 128.0f, kBlack);
  const Surface large_cell_surface = DrawSurface(kSurfaceSize, kSurfaceSize, kWhite, large_cell);
  toy_test::Check(CountPixels(large_cell_surface, kBlack) > 0,
                  "a cell larger than the surface still paints its visible part");
  toy_test::Check(PaintedOnlyInsideClip(large_cell_surface, kWhite, 0, 0, 4, 16),
                  "a cell larger than the surface is clipped to the clip rect");
}

void TestTextGlyphPixels() {
  DisplayList list;
  list.DrawText("H", 0, 0, 16.0f, kBlack);
  const Surface surface = DrawSurface(8, 16, kWhite, list);
  toy_test::Check(CountPixels(surface, kBlack) == 22, "H paints its 22 design pixels at 16 px");
  toy_test::Check(surface.At(1, 3) == kBlack, "H paints the top of its left stem");
  toy_test::Check(surface.At(0, 3) == kWhite, "H leaves its left bearing blank");
  toy_test::Check(surface.At(6, 7) == kBlack, "H paints the right end of its crossbar");

  DisplayList double_size;
  double_size.DrawText("H", 0, 0, 32.0f, kBlack);
  const Surface double_surface = DrawSurface(16, 32, kWhite, double_size);
  toy_test::Check(CountPixels(double_surface, kBlack) == 4 * 22,
                  "the 32 px cell is a nearest-neighbour 2x scale of the 16 px cell");
}

void TestTextAdvance() {
  DisplayList list;
  list.DrawText("HH", 0, 0, 32.0f, kBlack);
  const Surface surface = DrawSurface(32, 32, kWhite, list);
  bool second_cell_matches = true;
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 16; ++x) {
      second_cell_matches = second_cell_matches && surface.At(x, y) == surface.At(16 + x, y);
    }
  }
  toy_test::Check(second_cell_matches,
                  "at 32 px the second glyph starts 16 px after the first (one 0.5 em cell)");
  toy_test::Check(CountPixels(surface, kBlack) == 2 * 4 * 22, "both glyphs paint their pixels");
}

void TestTextFallback() {
  toy_test::Check(RenderText("\xC3\xA9", 8, 16, 16.0f) == RenderText("?", 8, 16, 16.0f),
                  "a two-byte non-ASCII code point uses the '?' glyph");
  toy_test::Check(RenderText("\xE2\x82\xAC", 8, 16, 16.0f) == RenderText("?", 8, 16, 16.0f),
                  "a three-byte non-ASCII code point uses the '?' glyph");
  toy_test::Check(RenderText("\xF0\x9F\x98\x80", 8, 16, 16.0f) == RenderText("?", 8, 16, 16.0f),
                  "a four-byte non-ASCII code point uses the '?' glyph");
  toy_test::Check(RenderText("\xFF", 8, 16, 16.0f) == RenderText("?", 8, 16, 16.0f),
                  "a malformed byte uses the '?' glyph");
  toy_test::Check(RenderText("\xE2\x82", 16, 16, 16.0f) == RenderText("??", 16, 16, 16.0f),
                  "an incomplete sequence paints one '?' per bad byte");
}

void TestTextClipping() {
  DisplayList list;
  list.PushClip(0, 0, 3, 16);
  list.DrawText("H", 0, 0, 16.0f, kBlack);
  const Surface surface = DrawSurface(8, 16, kWhite, list);
  toy_test::Check(CountPixels(surface, kBlack) == 10,
                  "the clip keeps only the 10 pixels of H left of x = 3");
  bool nothing_outside_clip = true;
  for (int y = 0; y < 16; ++y) {
    for (int x = 3; x < 8; ++x) {
      nothing_outside_clip = nothing_outside_clip && surface.At(x, y) == kWhite;
    }
  }
  toy_test::Check(nothing_outside_clip, "no text pixel is painted outside the clip");
  toy_test::Check(surface.At(2, 7) == kBlack, "the part of the crossbar inside the clip survives");
  toy_test::Check(surface.At(6, 7) == kWhite,
                  "the part of the crossbar outside the clip is clipped");
}

void TestDeterminism() {
  DisplayList list;
  list.FillRect(0, 0, 10, 10, 0x80FF0000u);
  list.PushClip(2, 2, 6, 6);
  list.FillRect(-5, -5, 20, 20, 0xFF00FF00u);
  list.PopClip();
  list.DrawText("abc", 1, 0, 16.0f, kBlack);

  Surface first(12, 12);
  Surface second(12, 12);
  first.Fill(kWhite);
  second.Fill(kWhite);
  DrawDisplayList(list, first);
  DrawDisplayList(list, second);
  toy_test::Check(CopyPixels(first) == CopyPixels(second),
                  "drawing the same list into two surfaces is pixel-identical");
}

void TestPrintableGlyphs() {
  bool every_printable_draws = true;
  for (char character = '!'; character <= '~'; ++character) {
    DisplayList list;
    list.DrawText(std::string(1, character), 0, 0, 16.0f, kBlack);
    const Surface surface = DrawSurface(8, 16, kWhite, list);
    every_printable_draws = every_printable_draws && CountPixels(surface, kBlack) > 0;
  }
  toy_test::Check(every_printable_draws, "every printable ASCII glyph paints at least one pixel");
}

void TestDrawImageCommandFields() {
  const auto image = MakeImage(2, 1, {kRed, kBlue});
  DisplayList list;
  list.DrawImage(image, 3, 4, 5, 6);

  toy_test::Check(list.size() == 1, "one DrawImage appends one command");
  const Command& command = list.commands()[0];
  toy_test::Check(command.kind == CommandKind::kDrawImage, "the command kind is kDrawImage");
  toy_test::Check(command.x == 3 && command.y == 4 && command.width == 5 && command.height == 6,
                  "the image command keeps its destination rectangle");
  toy_test::Check(command.image == image, "the image command keeps the shared image");
  toy_test::Check(command.color == 0 && command.text.empty() && command.font_size_px == 0.0f,
                  "the image command leaves the other fields at their defaults");
}

void TestDrawImageExactPixels() {
  // A 2x2 source drawn 1:1 at an offset copies its pixels exactly.
  const auto image = MakeImage(2, 2, {kRed, kGreen, kBlue, kWhite});
  DisplayList list;
  list.DrawImage(image, 1, 1, 2, 2);
  const Surface surface = DrawSurface(4, 4, kBlack, list);
  toy_test::Check(surface.At(1, 1) == kRed, "a 1:1 draw copies the top-left source pixel");
  toy_test::Check(surface.At(2, 1) == kGreen, "a 1:1 draw copies the top-right source pixel");
  toy_test::Check(surface.At(1, 2) == kBlue, "a 1:1 draw copies the bottom-left source pixel");
  toy_test::Check(surface.At(2, 2) == kWhite, "a 1:1 draw copies the bottom-right source pixel");
  toy_test::Check(CountPixels(surface, kBlack) == 12,
                  "a 1:1 draw leaves the rest of the surface alone");
}

void TestDrawImageUpscale() {
  // The frozen mapping is nearest-neighbour: destination (dx, dy) samples
  // source (dx * width / dest_width, dy * height / dest_height), so a 2x2 image
  // drawn at 4x4 becomes four 2x2 blocks.
  const auto image = MakeImage(2, 2, {kRed, kGreen, kBlue, kWhite});
  DisplayList list;
  list.DrawImage(image, 0, 0, 4, 4);
  const Surface surface = DrawSurface(4, 4, kBlack, list);

  bool matches = true;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      const Argb expected = y < 2 ? (x < 2 ? kRed : kGreen) : (x < 2 ? kBlue : kWhite);
      matches = matches && surface.At(x, y) == expected;
    }
  }
  toy_test::Check(matches, "a 2x upscale maps each source pixel to a 2x2 destination block");
  toy_test::Check(CountPixels(surface, kBlack) == 0, "a 4x4 draw covers the whole surface");
}

void TestDrawImageDownscale() {
  // Source coordinates use integer floor division. Drawing a 3-pixel-wide image
  // into 2 destination pixels samples source columns 0 and 1: the second
  // destination column computes 1 * 3 / 2 == 1, never the last source column.
  const auto image = MakeImage(3, 1, {kRed, kGreen, kBlue});
  DisplayList list;
  list.DrawImage(image, 0, 0, 2, 1);
  const Surface surface = DrawSurface(2, 1, kBlack, list);
  toy_test::Check(surface.At(0, 0) == kRed, "downscale samples source column 0 first");
  toy_test::Check(surface.At(1, 0) == kGreen, "downscale floors 1 * 3 / 2 to source column 1");
  toy_test::Check(CountPixels(surface, kBlue) == 0,
                  "floor mapping never samples the last source column in this case");
}

void TestDrawImageClipping() {
  // Image pixel (x, y) is 0xFF000000 | (x << 16) | y, so every source pixel is
  // distinct and the sampled coordinates are visible in the result.
  std::vector<Argb> pixels;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      pixels.push_back(0xFF000000u | (static_cast<Argb>(x) << 16) | static_cast<Argb>(y));
    }
  }
  const auto image = MakeImage(4, 4, std::move(pixels));

  DisplayList clipped;
  clipped.PushClip(1, 1, 2, 2);
  clipped.DrawImage(image, 0, 0, 4, 4);
  const Surface surface = DrawSurface(4, 4, kBlack, clipped);
  toy_test::Check(surface.At(1, 1) == 0xFF010001u, "the clip keeps source pixel (1, 1)");
  toy_test::Check(surface.At(2, 1) == 0xFF020001u, "the clip keeps source pixel (2, 1)");
  toy_test::Check(surface.At(1, 2) == 0xFF010002u, "the clip keeps source pixel (1, 2)");
  toy_test::Check(surface.At(2, 2) == 0xFF020002u, "the clip keeps source pixel (2, 2)");
  toy_test::Check(CountPixels(surface, kBlack) == 12, "an image draw is bounded by the clip");

  // A negative destination origin is clipped, and the source mapping still uses
  // the command origin: destination (0, 0) samples source (1, 1).
  const auto corners = MakeImage(2, 2, {kRed, kGreen, kBlue, kWhite});
  DisplayList negative;
  negative.DrawImage(corners, -1, -1, 2, 2);
  const Surface corner_surface = DrawSurface(3, 3, kBlack, negative);
  toy_test::Check(corner_surface.At(0, 0) == kWhite,
                  "a negative origin samples source (1, 1) for destination (0, 0)");
  toy_test::Check(CountPixels(corner_surface, kWhite) == 1,
                  "only the single visible pixel of a negative-origin draw is painted");

  // Completely off-surface draws are no-ops.
  DisplayList outside;
  outside.DrawImage(corners, 10, 10, 2, 2);
  outside.DrawImage(corners, -10, -10, 2, 2);
  const Surface untouched = DrawSurface(3, 3, kBlack, outside);
  toy_test::Check(CountPixels(untouched, kBlack) == 9, "off-surface image draws are no-ops");
}

void TestDrawImageAlphaBlending() {
  // Images use the same frozen SrcOver formula as fills.
  const auto translucent_red = MakeImage(1, 1, {0x80FF0000u});
  DisplayList white_list;
  white_list.DrawImage(translucent_red, 0, 0, 1, 1);
  const Surface over_white = DrawSurface(1, 1, kWhite, white_list);
  toy_test::Check(over_white.At(0, 0) == 0xFFFF7F7Fu,
                  "alpha 128 red image over white blends to 0xFFFF7F7F");

  const auto translucent_blue = MakeImage(1, 1, {0x80406080u});
  DisplayList dark_list;
  dark_list.DrawImage(translucent_blue, 0, 0, 1, 1);
  const Surface over_dark = DrawSurface(1, 1, 0xFF102030u, dark_list);
  toy_test::Check(over_dark.At(0, 0) == 0xFF284058u,
                  "alpha 128 0x406080 image over 0x102030 matches the frozen channel formula");

  const auto transparent = MakeImage(1, 1, {0x00FF0000u});
  DisplayList transparent_list;
  transparent_list.DrawImage(transparent, 0, 0, 1, 1);
  const Surface over_white_alpha0 = DrawSurface(1, 1, kWhite, transparent_list);
  toy_test::Check(over_white_alpha0.At(0, 0) == kWhite, "alpha 0 image pixels are a no-op");
  Surface transparent_black(1, 1);
  DrawDisplayList(transparent_list, transparent_black);
  toy_test::Check(transparent_black.At(0, 0) == 0x00000000u,
                  "alpha 0 image pixels do not write an opaque pixel over transparent black");

  const auto opaque = MakeImage(1, 1, {0xFF0000FFu});
  DisplayList opaque_list;
  opaque_list.DrawImage(opaque, 0, 0, 1, 1);
  Surface transparent_black_opaque(1, 1);
  DrawDisplayList(opaque_list, transparent_black_opaque);
  toy_test::Check(transparent_black_opaque.At(0, 0) == 0xFF0000FFu,
                  "alpha 255 image pixels write 0xFF000000 | rgb");
}

void TestDrawImageNoOps() {
  const auto image = MakeImage(1, 1, {kRed});
  DisplayList list;
  list.DrawImage(nullptr, 0, 0, 4, 4);
  list.DrawImage(MakeImage(0, 0, {}), 0, 0, 4, 4);
  list.DrawImage(MakeImage(2, 2, {}), 0, 0, 2, 2);
  list.DrawImage(image, 0, 0, 0, 4);
  list.DrawImage(image, 0, 0, 4, 0);
  list.DrawImage(image, 0, 0, -2, 4);
  list.DrawImage(image, 0, 0, 4, -2);
  const Surface surface = DrawSurface(4, 4, kBlack, list);
  toy_test::Check(CountPixels(surface, kBlack) == 16,
                  "null, empty, malformed and non-positive-size image commands are no-ops");

  DisplayList empty_clip;
  empty_clip.PushClip(0, 0, 0, 4);
  empty_clip.DrawImage(image, 0, 0, 4, 4);
  const Surface clipped_surface = DrawSurface(4, 4, kBlack, empty_clip);
  toy_test::Check(CountPixels(clipped_surface, kBlack) == 16,
                  "a zero-size clip hides an image draw");
}

void TestDrawImageKeepsImageAlive() {
  auto image = MakeImage(1, 1, {kRed});
  std::weak_ptr<const Image> weak = image;
  {
    DisplayList list;
    list.DrawImage(image, 0, 0, 1, 1);
    image.reset();
    toy_test::Check(!weak.expired(), "the display list keeps the image alive");
    const Surface surface = DrawSurface(1, 1, kBlack, list);
    toy_test::Check(surface.At(0, 0) == kRed,
                    "a command still draws after the caller releases its image reference");
  }
  toy_test::Check(weak.expired(), "destroying the display list releases the image");
}

void TestDrawImageDeterminism() {
  const auto image = MakeImage(2, 2, {kRed, 0x80FF0000u, kBlue, kWhite});
  DisplayList list;
  list.PushClip(1, 1, 3, 3);
  list.DrawImage(image, 0, 0, 4, 4);
  list.PopClip();

  Surface first(4, 4);
  Surface second(4, 4);
  first.Fill(kWhite);
  second.Fill(kWhite);
  DrawDisplayList(list, first);
  DrawDisplayList(list, second);
  toy_test::Check(CopyPixels(first) == CopyPixels(second),
                  "drawing the same image list twice is pixel-identical");
}

}  // namespace

int main() {
  TestCommandBuilders();
  TestFillRectExactColor();
  TestFillRectBlending();
  TestFillRectNoOps();
  TestFillRectClipping();
  TestClipStack();
  TestTextSpaceAndNonPositiveSize();
  TestExtremeFontSizes();
  TestTextGlyphPixels();
  TestTextAdvance();
  TestTextFallback();
  TestTextClipping();
  TestDrawImageCommandFields();
  TestDrawImageExactPixels();
  TestDrawImageUpscale();
  TestDrawImageDownscale();
  TestDrawImageClipping();
  TestDrawImageAlphaBlending();
  TestDrawImageNoOps();
  TestDrawImageKeepsImageAlive();
  TestDrawImageDeterminism();
  TestDeterminism();
  TestPrintableGlyphs();
  return toy_test::Finish("render.paint");
}
