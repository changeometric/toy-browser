#pragma once

#include <string_view>

#include "toy/layout/box.h"

namespace toy::layout::test {

// Deterministic fake text metrics for the layout tests. Advance is half a CSS
// pixel per byte per font-size pixel: 8 px per ASCII byte at the default 16 px
// font size. Line height equals the font size. This keeps golden dumps exact
// without any render or font dependency.
inline TextMetrics MeasureFakeText(std::string_view text_utf8, float font_size_px) {
  TextMetrics metrics;
  metrics.advance_px = 0.5f * font_size_px * static_cast<float>(text_utf8.size());
  metrics.line_height_px = font_size_px;
  return metrics;
}

}  // namespace toy::layout::test
