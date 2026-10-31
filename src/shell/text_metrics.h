#pragma once

#include <string_view>

#include "toy/layout/box.h"
#include "toy/render/font.h"
#include "toy/shell/fonts.h"
#include "toy/style/computed_style.h"

namespace toy::shell {

// Adapts the session's font selection (real system fonts, an explicit font file
// or family) or the built-in bitmap font (fonts == null, the pre-M10 behavior) to
// layout's measurement callback. layout never depends on render; shell is the
// only module that sees both and wires them up. Shared by the --dump-boxes path
// and Browser so both lay out text identically.
//
// `language` is the page language hint (the <html lang> value, or empty) and is
// only used by the real-font selection. In bitmap mode the ascent equals the
// line height, which is what paint's bitmap path assumes.
inline layout::TextMetrics MeasureText(const Fonts* fonts, std::string_view text_utf8,
                                       const style::ComputedStyle& style,
                                       std::string_view language) {
  if (fonts != nullptr) {
    return fonts->MeasureText(text_utf8, style, language);
  }
  const float font_size_px = style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
  const float line_height_px = render::LineHeightPx(font_size_px);
  return layout::TextMetrics{render::MeasureTextWidth(text_utf8, font_size_px), line_height_px,
                             line_height_px};
}

}  // namespace toy::shell
