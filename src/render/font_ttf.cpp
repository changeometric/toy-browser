#include <ft2build.h>

#include "toy/render/font.h"
#include FT_FREETYPE_H

#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "font_ttf_internal.h"
#include "toy/render/surface.h"

namespace toy::render {

namespace {

void SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

bool IsValidFontSize(float font_size_px) {
  return std::isfinite(font_size_px) && font_size_px > 0.0f;
}

// Effective size used to shape, measure and rasterize real-font text, in CSS
// pixels: 0 for an unusable size (non-positive or non-finite), otherwise the
// requested size clamped to the frozen kMaxRealFontSizePx bound. Clamping bounds
// freetype's per-glyph coverage bitmap for adversarial CSS font sizes: every size
// above the bound behaves exactly like the bound, in metrics and in pixels.
float EffectiveSize(float font_size_px) {
  if (!IsValidFontSize(font_size_px)) {
    return 0.0f;
  }
  return std::min(font_size_px, kMaxRealFontSizePx);
}

// Freetype sizes and positions glyphs in 26.6 fixed point: one device pixel is
// 64 units. The frozen sizing rule is scale = max(1, llround(size * 64)) applied
// to the effective (clamped) size, with a defensive clamp at the FT_F26Dot6
// range (only hostile CSS reaches it).
bool ComputeScale(float font_size_px, FT_F26Dot6* scale) {
  if (!IsValidFontSize(font_size_px)) {
    return false;
  }
  const long double max_scale = static_cast<long double>(std::numeric_limits<FT_F26Dot6>::max());
  const long double scaled = static_cast<long double>(font_size_px) * 64.0L;
  if (scaled >= max_scale) {
    *scale = std::numeric_limits<FT_F26Dot6>::max();
    return true;
  }
  const long long rounded = std::llround(static_cast<double>(font_size_px) * 64.0);
  *scale = static_cast<FT_F26Dot6>(rounded > 1 ? rounded : 1);
  return true;
}

// Sets the freetype face to `font_size_px` at 72 dpi. Returns false when the
// size is unusable (non-positive, non-finite) or freetype rejects it.
bool SetCharSize(FT_Face face, float font_size_px) {
  FT_F26Dot6 scale = 0;
  if (!ComputeScale(font_size_px, &scale)) {
    return false;
  }
  return FT_Set_Char_Size(face, scale, scale, 0, 0) == 0;
}

// One shaped glyph in harfbuzz order. Advances, offsets are 26.6 device pixels
// (their native harfbuzz unit); offsets are relative to the pen and y grows up.
struct ShapedGlyph {
  std::uint32_t glyph_id = 0;
  int x_advance = 0;
  int y_advance = 0;
  int x_offset = 0;
  int y_offset = 0;
};

struct ShapedRun {
  std::vector<ShapedGlyph> glyphs;
  hb_direction_t direction = HB_DIRECTION_INVALID;
};

// Shapes one UTF-8 run at the effective `font_size_px` (the caller has already
// clamped it with EffectiveSize): sizes the face, shapes with a harfbuzz font
// backed by the freetype face, and copies the glyph ids, advances and offsets
// out. Returns false when the size cannot be applied; callers treat that as an
// empty run. Resizing the shared freetype face is internal mutation: Font is
// logically immutable, but the render path is single-threaded by design (see
// docs/architecture.md), so one face per Font is safe here.
bool ShapeText(FT_Face face, std::string_view text_utf8, float font_size_px, ShapedRun* run,
               FT_F26Dot6* out_scale) {
  run->glyphs.clear();
  run->direction = HB_DIRECTION_INVALID;
  if (text_utf8.empty() || !SetCharSize(face, font_size_px)) {
    return false;
  }
  if (!ComputeScale(font_size_px, out_scale)) {
    return false;
  }

  // A harfbuzz font is created per call on purpose: it snapshots the face size
  // (set above), so a later size change cannot leave a stale scale behind.
  hb_font_t* hb_font = hb_ft_font_create_referenced(face);
  if (hb_font == nullptr) {
    return false;
  }
  hb_buffer_t* buffer = hb_buffer_create();
  if (buffer == nullptr) {
    hb_font_destroy(hb_font);
    return false;
  }
  // hb_buffer_add_utf8 takes an int length; clamp enormous input defensively
  // (the renderer and MeasureTextWidth both use this same truncation).
  const std::size_t max_length = static_cast<std::size_t>(std::numeric_limits<int>::max());
  const int length = static_cast<int>(std::min(text_utf8.size(), max_length));
  hb_buffer_add_utf8(buffer, text_utf8.data(), length, 0, -1);
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(hb_font, buffer, nullptr, 0);

  unsigned glyph_count = 0;
  const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
  unsigned position_count = 0;
  const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &position_count);
  const unsigned count = std::min(glyph_count, position_count);
  run->direction = hb_buffer_get_direction(buffer);
  try {
    run->glyphs.reserve(count);
    for (unsigned index = 0; index < count; ++index) {
      ShapedGlyph glyph;
      glyph.glyph_id = infos[index].codepoint;
      glyph.x_advance = positions[index].x_advance;
      glyph.y_advance = positions[index].y_advance;
      glyph.x_offset = positions[index].x_offset;
      glyph.y_offset = positions[index].y_offset;
      run->glyphs.push_back(glyph);
    }
  } catch (const std::bad_alloc&) {
    run->glyphs.clear();
    hb_buffer_destroy(buffer);
    hb_font_destroy(hb_font);
    return false;
  }
  hb_buffer_destroy(buffer);
  hb_font_destroy(hb_font);
  return true;
}

// Rounds a 26.6 value to the nearest device pixel, ties away from zero, which
// is std::llround's rule. This is the per-glyph integer rounding of the frozen
// placement formula.
long long Round26_6(long long value_26_6) {
  return std::llround(static_cast<double>(value_26_6) / 64.0);
}

// One 8-bit channel of the frozen SrcOver formula with `coverage` in place of
// alpha: (source * coverage + destination * (255 - coverage) + 127) / 255.
unsigned int BlendChannel(unsigned int source, unsigned int destination, unsigned int coverage) {
  return (source * coverage + destination * (255u - coverage) + 127u) / 255u;
}

}  // namespace

// Private font state. Member order matters for destruction: the destructor body
// releases the face (while the library and the font bytes are still alive),
// then the library; the byte vector is destroyed last.
struct Font::Impl {
  ~Impl() {
    if (face != nullptr) {
      FT_Done_Face(face);
    }
    if (library != nullptr) {
      FT_Done_FreeType(library);
    }
  }

  std::vector<unsigned char> bytes;
  FT_Library library = nullptr;
  FT_Face face = nullptr;
};

Font::Font() : impl_(std::make_unique<Impl>()) {}

Font::~Font() = default;

std::shared_ptr<const Font> Font::LoadFromMemory(std::string_view bytes, std::string* error) {
  if (bytes.empty()) {
    SetError(error, "empty font data");
    return nullptr;
  }
  std::shared_ptr<Font> font;
  try {
    font = std::shared_ptr<Font>(new Font());
  } catch (const std::bad_alloc&) {
    SetError(error, "out of memory while creating the font");
    return nullptr;
  }
  Impl& impl = *font->impl_;
  try {
    // freetype keeps a pointer into this buffer for the face's whole lifetime.
    impl.bytes.assign(bytes.begin(), bytes.end());
  } catch (const std::bad_alloc&) {
    SetError(error, "out of memory while copying the font data");
    return nullptr;
  }
  if (impl.bytes.size() > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max())) {
    SetError(error, "font data is too large");
    return nullptr;
  }
  if (FT_Init_FreeType(&impl.library) != 0) {
    SetError(error, "freetype initialization failed");
    return nullptr;
  }
  const FT_Error face_error = FT_New_Memory_Face(
      impl.library, impl.bytes.data(), static_cast<FT_Long>(impl.bytes.size()), 0, &impl.face);
  if (face_error != 0) {
    SetError(error, "not a supported font (freetype error " + std::to_string(face_error) + ")");
    return nullptr;
  }
  return font;
}

std::shared_ptr<const Font> Font::LoadFromFile(std::string_view path, std::string* error) {
  std::string bytes;
  try {
    std::ifstream input(std::string(path), std::ios::binary);
    if (!input) {
      SetError(error, "cannot open font file '" + std::string(path) + "'");
      return nullptr;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad()) {
      SetError(error, "cannot read font file '" + std::string(path) + "'");
      return nullptr;
    }
  } catch (const std::bad_alloc&) {
    SetError(error, "out of memory while reading font file '" + std::string(path) + "'");
    return nullptr;
  }
  if (bytes.empty()) {
    SetError(error, "font file is empty: '" + std::string(path) + "'");
    return nullptr;
  }
  std::string load_error;
  const std::shared_ptr<const Font> font = LoadFromMemory(bytes, &load_error);
  if (font == nullptr) {
    SetError(error, "cannot load font file '" + std::string(path) + "': " + load_error);
  }
  return font;
}

float Font::MeasureTextWidth(std::string_view text_utf8, float font_size_px) const {
  // The `!(value > 0)` style guard inside EffectiveSize also catches NaN.
  const float effective_size = EffectiveSize(font_size_px);
  if (impl_ == nullptr || impl_->face == nullptr || text_utf8.empty() || effective_size <= 0.0f) {
    return 0.0f;
  }
  ShapedRun run;
  FT_F26Dot6 scale = 0;
  if (!ShapeText(impl_->face, text_utf8, effective_size, &run, &scale)) {
    return 0.0f;
  }
  // The frozen contract sums the shaped x advances in 26.6 and divides by 64.
  long long total_26_6 = 0;
  for (const ShapedGlyph& glyph : run.glyphs) {
    total_26_6 += glyph.x_advance;
  }
  return static_cast<float>(total_26_6) / 64.0f;
}

float Font::AscentPx(float font_size_px) const {
  const float effective_size = EffectiveSize(font_size_px);
  if (impl_ == nullptr || impl_->face == nullptr || effective_size <= 0.0f) {
    return 0.0f;
  }
  const FT_Face face = impl_->face;
  if (face->units_per_EM <= 0) {
    return 0.0f;
  }
  // Report-only metric straight from the design units, at the effective size:
  // ascender / unitsPerEm.
  return static_cast<float>(static_cast<double>(effective_size) * face->ascender /
                            face->units_per_EM);
}

float Font::LineHeightPx(float font_size_px) const {
  const float effective_size = EffectiveSize(font_size_px);
  if (impl_ == nullptr || impl_->face == nullptr || effective_size <= 0.0f) {
    return 0.0f;
  }
  const FT_Face face = impl_->face;
  if (face->units_per_EM <= 0) {
    return 0.0f;
  }
  // Report-only metric straight from the design units, at the effective size:
  // face->height / unitsPerEm (ascent + descent + line gap in the hhea/OS2 sense
  // freetype exposes).
  return static_cast<float>(static_cast<double>(effective_size) * face->height /
                            face->units_per_EM);
}

namespace internal {

// Frozen anti-aliased placement and compositing (published for paint/tests):
//
// `font_size_px` is first passed through EffectiveSize(): non-positive and
// non-finite sizes paint nothing (as does empty text or an empty clip), and a
// size above the frozen kMaxRealFontSizePx behaves exactly like that bound, so
// freetype's coverage bitmaps stay bounded for adversarial CSS sizes.
//
//   pen_x_26_6 = 0, pen_y_26_6 = 0
//   for each shaped glyph g (harfbuzz order):
//     position_x_26_6 = pen_x_26_6 + g.x_offset
//     position_y_26_6 = pen_y_26_6 + g.y_offset
//     glyph_origin_x      = x + Round26_6(position_x_26_6)
//     glyph_baseline_y    = baseline_y - Round26_6(position_y_26_6)
//     bitmap_left_px      = glyph_origin_x + slot->bitmap_left
//     bitmap_top_px       = glyph_baseline_y - slot->bitmap_top
//     pen_x_26_6 += g.x_advance; pen_y_26_6 += g.y_advance
//
// Round26_6 rounds to the nearest device pixel (ties away from zero). A coverage
// byte at bitmap position (bx, by) writes device pixel
// (bitmap_left_px + bx, bitmap_top_px + by). Each covered pixel is composited as
// the opaque text color over the destination with coverage in place of alpha:
//   channel = (color_channel * coverage + dst_channel * (255 - coverage) + 127) / 255
// and the written pixel is 0xFF000000 | rgb (coverage 0 is a no-op, coverage 255
// writes 0xFF000000 | rgb directly).
void FontRasterizer::DrawText(const Font& font, std::string_view text_utf8, int x, int baseline_y,
                              float font_size_px, Argb color, Surface& surface, int clip_x,
                              int clip_y, int clip_width, int clip_height) {
  const float effective_size = EffectiveSize(font_size_px);
  if (text_utf8.empty() || clip_width <= 0 || clip_height <= 0 || effective_size <= 0.0f ||
      font.impl_ == nullptr || font.impl_->face == nullptr) {
    return;
  }
  FT_Face face = font.impl_->face;
  ShapedRun run;
  FT_F26Dot6 scale = 0;
  if (!ShapeText(face, text_utf8, effective_size, &run, &scale)) {
    return;
  }
  if (run.glyphs.empty()) {
    return;
  }

  // Bounded work: for a left-to-right run whose advances never move backwards,
  // once the leftmost ink any glyph can paint is past the clip's right edge, no
  // later glyph can be visible. The bound is conservative: face->bbox.xMin is
  // the minimum x over all glyphs in font units, converted to 26.6 and widened
  // by one pixel for hinting/rounding shifts. Other runs are iterated in full
  // (still only per glyph; pixels are clipped per glyph below).
  bool advances_non_negative = true;
  for (const ShapedGlyph& glyph : run.glyphs) {
    if (glyph.x_advance < 0) {
      advances_non_negative = false;
      break;
    }
  }
  bool can_break_early =
      run.direction == HB_DIRECTION_LTR && advances_non_negative && face->units_per_EM > 0;
  long long minimum_ink_x_26_6 = 0;
  if (can_break_early) {
    const long double minimum_ink = std::floor(static_cast<long double>(face->bbox.xMin) * scale /
                                               static_cast<long double>(face->units_per_EM));
    // Clamp before the integer conversion so an absurd font size or bbox cannot
    // overflow; the sum below then stays far from the long long limits.
    constexpr long double kInkLimit = 4611686018427387904.0L;  // 2^62
    minimum_ink_x_26_6 =
        static_cast<long long>(std::clamp(minimum_ink, -kInkLimit, kInkLimit)) - 64;
  }
  const long long clip_right = static_cast<long long>(clip_x) + clip_width;
  const long long clip_bottom = static_cast<long long>(clip_y) + clip_height;
  const long long pen_start_limit = (clip_right - static_cast<long long>(x)) * 64;

  const unsigned int source_red = (color >> 16) & 0xFFu;
  const unsigned int source_green = (color >> 8) & 0xFFu;
  const unsigned int source_blue = color & 0xFFu;
  long long pen_x_26_6 = 0;
  long long pen_y_26_6 = 0;
  for (const ShapedGlyph& glyph : run.glyphs) {
    if (can_break_early && pen_x_26_6 + glyph.x_offset + minimum_ink_x_26_6 > pen_start_limit) {
      break;
    }
    const long long position_x_26_6 = pen_x_26_6 + glyph.x_offset;
    const long long position_y_26_6 = pen_y_26_6 + glyph.y_offset;
    pen_x_26_6 += glyph.x_advance;
    pen_y_26_6 += glyph.y_advance;

    if (FT_Load_Glyph(face, glyph.glyph_id, FT_LOAD_DEFAULT | FT_LOAD_RENDER) != 0) {
      continue;
    }
    const FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.pixel_mode != FT_PIXEL_MODE_GRAY ||
        bitmap.pitch <= 0) {
      continue;
    }
    // Keep the placement arithmetic in 64 bits: only coordinates that pass the
    // clip test below are narrowed back to device-pixel indices.
    const long long glyph_origin_x = static_cast<long long>(x) + Round26_6(position_x_26_6);
    const long long glyph_baseline_y =
        static_cast<long long>(baseline_y) - Round26_6(position_y_26_6);
    const long long bitmap_x = glyph_origin_x + slot->bitmap_left;
    const long long bitmap_y = glyph_baseline_y - slot->bitmap_top;

    // Visible part of the coverage bitmap, in bitmap pixel coordinates.
    const long long visible_left =
        std::max<long long>(0, static_cast<long long>(clip_x) - bitmap_x);
    const long long visible_right = std::min<long long>(bitmap.width, clip_right - bitmap_x);
    const long long visible_top = std::max<long long>(0, static_cast<long long>(clip_y) - bitmap_y);
    const long long visible_bottom = std::min<long long>(bitmap.rows, clip_bottom - bitmap_y);
    if (visible_left >= visible_right || visible_top >= visible_bottom) {
      continue;
    }

    for (long long bitmap_row = visible_top; bitmap_row < visible_bottom; ++bitmap_row) {
      const unsigned char* coverage_row = bitmap.buffer + bitmap_row * bitmap.pitch;
      Argb* surface_row =
          surface.pixels() + static_cast<std::size_t>(bitmap_y + bitmap_row) * surface.stride();
      for (long long bitmap_column = visible_left; bitmap_column < visible_right; ++bitmap_column) {
        const unsigned int coverage = coverage_row[bitmap_column];
        if (coverage == 0) {
          continue;
        }
        Argb* pixel = surface_row + static_cast<int>(bitmap_x + bitmap_column);
        if (coverage == 255) {
          *pixel = 0xFF000000u | (color & 0x00FFFFFFu);
          continue;
        }
        const Argb destination = *pixel;
        const unsigned int red = BlendChannel(source_red, (destination >> 16) & 0xFFu, coverage);
        const unsigned int green = BlendChannel(source_green, (destination >> 8) & 0xFFu, coverage);
        const unsigned int blue = BlendChannel(source_blue, destination & 0xFFu, coverage);
        // The blend result is always an opaque pixel, like fills and images.
        *pixel = 0xFF000000u | (red << 16) | (green << 8) | blue;
      }
    }
  }
}

}  // namespace internal

}  // namespace toy::render
