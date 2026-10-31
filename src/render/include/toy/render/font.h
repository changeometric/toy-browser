#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace toy::render {

namespace internal {
// Private to the render module: the anti-aliased real-font rasterizer declared
// in src/render/font_ttf_internal.h. It needs access to the font engine's
// private freetype state, which must not appear in this public header.
struct FontRasterizer;
}  // namespace internal

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

// Upper bound on the effective size used to shape and rasterize real-font text.
// CSS can request arbitrarily large px sizes, and freetype allocates the whole
// glyph coverage bitmap before clipping, so metrics and painting both clamp the
// real-font size to this bound (sizes above it behave exactly like this size).
// The built-in bitmap font keeps its own bounded rasterizer guard instead; this
// constant only applies to the real-font path.
inline constexpr float kMaxRealFontSizePx = 4096.0f;

// A real TrueType/OpenType font loaded through the approved freetype and
// harfbuzz dependencies. No freetype or harfbuzz type appears in this header:
// loading, shaping and rasterization are private to the render module. A Font is
// immutable after loading and shared by value (shell, paint, display list); the
// display list keeps a shared reference so a built list may outlive its page.
//
// Units: sizes and metrics are CSS pixels (1 CSS px = 1 device px in this MVP).
// Real-font text is anti-aliased with 8-bit glyph coverage and composited with
// the frozen SrcOver formula; that is the deliberate difference from the built-in
// bitmap font, whose opaque, unaliased path stays unchanged. Shaping is
// harfbuzz; rasterization is freetype face 0.
class Font {
 public:
  // Loads face 0 from a font file, or from an in-memory font image. Returns null
  // on failure and stores a human-readable message in *error (when not null);
  // never throws. LoadFromMemory copies the bytes, so the caller may release them.
  static std::shared_ptr<const Font> LoadFromFile(std::string_view path, std::string* error);
  static std::shared_ptr<const Font> LoadFromMemory(std::string_view bytes, std::string* error);

  // Immutable after construction. The out-of-line destructor keeps the private
  // freetype/harfbuzz state out of this header.
  ~Font();
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  // Shaped advance width of `text_utf8` at `font_size_px`, in CSS pixels.
  // Empty text or a non-positive/non-finite size returns 0. Code points the font
  // cannot map use glyph 0 (.notdef), exactly as harfbuzz shapes them.
  // Deterministic for a given font and size.
  float MeasureTextWidth(std::string_view text_utf8, float font_size_px) const;

  // Line box height (ascent + descent + line gap) and ascent above the baseline
  // at `font_size_px`, in CSS pixels. Both return 0 for a non-positive size.
  float LineHeightPx(float font_size_px) const;
  float AscentPx(float font_size_px) const;

 private:
  // Private renderer bridge: paints shaped real-font text through the font
  // engine's private freetype state (see font_ttf_internal.h).
  friend struct internal::FontRasterizer;

  Font();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace toy::render
