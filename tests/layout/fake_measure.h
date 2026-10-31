#pragma once

#include <string_view>

#include "toy/layout/box.h"
#include "toy/style/computed_style.h"

namespace toy::layout::test {

// Deterministic fake text metrics for the layout tests. Advance is half a CSS
// pixel per byte per font-size pixel: 8 px per ASCII byte at the default 16 px
// font size. Line height equals the font size and the ascent equals the line
// height (the pre-M10 assumption), so geometry and the per-line baseline stay
// exact without any render or font dependency. Negative computed font sizes are
// clamped to 0 like real font metrics do.
inline TextMetrics MeasureFakeText(std::string_view text_utf8, const style::ComputedStyle& style) {
  const float font_size_px = style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
  TextMetrics metrics;
  metrics.advance_px = 0.5f * font_size_px * static_cast<float>(text_utf8.size());
  metrics.line_height_px = font_size_px;
  metrics.ascent_px = font_size_px;
  return metrics;
}

}  // namespace toy::layout::test
