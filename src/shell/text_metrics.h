#pragma once

#include <string_view>

#include "toy/layout/box.h"
#include "toy/render/font.h"

namespace toy::shell {

// Adapts the built-in bitmap font to layout's measurement callback. layout never
// depends on render; shell is the only module that sees both and wires them up.
// Shared by the --dump-boxes path and Browser so both lay out text identically.
inline layout::TextMetrics MeasureText(std::string_view text_utf8, float font_size_px) {
  return layout::TextMetrics{render::MeasureTextWidth(text_utf8, font_size_px),
                             render::LineHeightPx(font_size_px)};
}

}  // namespace toy::shell
