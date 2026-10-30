#pragma once

#include <string_view>

namespace toy::render {

// Metrics of the built-in monospace bitmap font, the measurement source that
// layout consumes through a shell adapter. The design grid is 8x16 bitmap cells
// at a 16 CSS px em; glyphs are painted by the rasterizer and non-ASCII code
// points fall back to '?'.
inline constexpr float kFontDesignSizePx = 16.0f;
inline constexpr int kGlyphAdvanceUnits = 8;
inline constexpr int kLineHeightUnits = 16;

// Width in CSS pixels of `text_utf8` set at `font_size_px`. UTF-8 is decoded
// leniently, never failing and never throwing: a malformed byte counts as one
// code point. Every code point advances one 8/16 em cell (0.5 * font_size_px),
// including non-ASCII code points, which paint the '?' fallback glyph. Empty
// text or a non-positive font size returns 0.0f. Pure and deterministic.
float MeasureTextWidth(std::string_view text_utf8, float font_size_px);

// Line box height in CSS pixels for `font_size_px`: 16/16 em, so one em.
// Returns 0.0f for non-positive sizes.
float LineHeightPx(float font_size_px);

}  // namespace toy::render
