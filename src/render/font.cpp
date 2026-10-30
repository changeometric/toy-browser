#include "toy/render/font.h"

#include <cstddef>

namespace toy::render {

namespace {

// One glyph cell expressed in em: 8/16 = 0.5 em.
constexpr float kGlyphAdvanceEm =
    static_cast<float>(kGlyphAdvanceUnits) / static_cast<float>(kFontDesignSizePx);

bool IsContinuationByte(unsigned char byte) { return (byte & 0xC0u) == 0x80u; }

// Length of the UTF-8 sequence introduced by `lead`, or 0 when `lead` cannot
// start one (a stray continuation byte or 0xF8..0xFF).
std::size_t SequenceLength(unsigned char lead) {
  if (lead < 0x80u) {
    return 1;
  }
  if ((lead & 0xE0u) == 0xC0u) {
    return 2;
  }
  if ((lead & 0xF0u) == 0xE0u) {
    return 3;
  }
  if ((lead & 0xF8u) == 0xF0u) {
    return 4;
  }
  return 0;
}

// Counts the code points in `text`. A structurally valid UTF-8 sequence counts
// as one code point; every byte that does not complete such a sequence counts
// as one code point on its own. Malformed input therefore never fails and never
// loses a cell: it can only make the text wider, one cell per bad byte.
std::size_t CountCodePoints(std::string_view text) {
  std::size_t count = 0;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const std::size_t length = SequenceLength(static_cast<unsigned char>(text[offset]));
    bool valid = length != 0;
    if (valid && length > 1) {
      valid = offset + length <= text.size();
      for (std::size_t index = 1; valid && index < length; ++index) {
        valid = IsContinuationByte(static_cast<unsigned char>(text[offset + index]));
      }
    }
    // An incomplete or invalid sequence consumes just its first byte, so the
    // remaining bytes are examined again on the next loop iteration.
    offset += valid ? length : 1;
    ++count;
  }
  return count;
}

}  // namespace

float MeasureTextWidth(std::string_view text_utf8, float font_size_px) {
  // The `!(value > 0)` form also catches NaN, keeping the guard total.
  if (text_utf8.empty() || !(font_size_px > 0.0f)) {
    return 0.0f;
  }
  const float glyph_advance_px = font_size_px * kGlyphAdvanceEm;
  return static_cast<float>(CountCodePoints(text_utf8)) * glyph_advance_px;
}

float LineHeightPx(float font_size_px) {
  if (!(font_size_px > 0.0f)) {
    return 0.0f;
  }
  return font_size_px * (static_cast<float>(kLineHeightUnits) / kFontDesignSizePx);
}

}  // namespace toy::render
