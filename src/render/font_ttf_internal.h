#pragma once

#include <string_view>

#include "toy/render/font.h"
#include "toy/render/surface.h"

// Private to the render module: the bridge between the display-list rasterizer
// (paint.cpp) and the real-font engine (font_ttf.cpp). Declaring this struct as a
// friend in the public font.h lets the renderer reach the freetype face while no
// freetype or harfbuzz type appears in a public header.
namespace toy::render::internal {

struct FontRasterizer {
  // Paints `text_utf8` with the real font: harfbuzz shapes the string once, then
  // freetype rasterizes each glyph with 8-bit anti-aliased coverage. `x` is the
  // pen origin and `baseline_y` is the baseline, in device pixels (top-left
  // origin, y grows down). (clip_x, clip_y, clip_width, clip_height) is the
  // current display-list clip, already intersected with the surface; only
  // pixels inside it are written.
  //
  // No-op for an empty string, a non-positive/non-finite size, or an empty
  // clip. If the font cannot be sized (e.g. a freetype error) the call also
  // paints nothing. Never throws.
  static void DrawText(const Font& font, std::string_view text_utf8, int x, int baseline_y,
                       float font_size_px, Argb color, Surface& surface, int clip_x, int clip_y,
                       int clip_width, int clip_height);
};

}  // namespace toy::render::internal
