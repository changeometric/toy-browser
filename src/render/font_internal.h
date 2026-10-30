#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Private to the render module: shared by the font metrics and the rasterizer.
namespace toy::render::internal {

// Design cell of the built-in bitmap font, in pixels. Row bit 7 is the
// leftmost pixel; rows run top to bottom.
inline constexpr int kGlyphDesignWidth = 8;
inline constexpr int kGlyphDesignHeight = 16;

// Returns the 16-row design cell for `code_point`. Code points outside
// U+0020..U+007E return the '?' glyph. The returned pointer is static and
// owned by the render module.
const std::uint8_t* GlyphRows(char32_t code_point);

// Decodes one lenient UTF-8 code point at `offset` (which must be less than
// text.size()) and returns the number of bytes consumed, at least one. A
// structurally valid sequence is one code point; every malformed byte is one
// replacement code point (U+FFFD). Never throws.
std::size_t DecodeUtf8(std::string_view text, std::size_t offset, char32_t* code_point);

}  // namespace toy::render::internal
