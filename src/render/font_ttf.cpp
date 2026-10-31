#include <ft2build.h>

#include "toy/render/font.h"
#include FT_FREETYPE_H

#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "font_cache_internal.h"
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

// One freetype 8-bit coverage bitmap copied out of a glyph slot: the bytes and
// the placement fields the frozen rasterizer formula needs. A glyph that cannot
// be rasterized (load failure, empty bitmap, unsupported pixel mode) is cached
// with `usable == false`, so repeated draws skip the freetype call exactly as
// they did before and caching never changes the painted pixels.
struct GlyphCoverage {
  bool usable = false;
  int bitmap_left = 0;
  int bitmap_top = 0;
  int width = 0;
  int rows = 0;
  int pitch = 0;
  std::vector<unsigned char> coverage;
};

// Cache budgets (M11). Both caches are private to one Font and single-threaded,
// like the freetype face itself.
//
// Shaped runs: 16 MiB / 16384 entries. A run costs its key (the UTF-8 text plus
// the 26.6 size, stored by the map) and 20 bytes per shaped glyph, so ordinary
// pages stay far below the byte budget; the entry budget bounds bookkeeping on
// pages with tens of thousands of distinct short strings. Runs are the biggest
// measured hotspot: re-shaping each call also defeats harfbuzz's shape-plan
// cache, so hits avoid the whole shaping pipeline for MeasureTextWidth and for
// DrawText.
//
// Glyph coverage: 32 MiB / 16384 entries. A body-size Latin glyph costs a few
// hundred bytes; the biggest legal glyph (4096 px effective size) can cost
// megabytes, so the byte budget is the primary bound and a single bitmap larger
// than the whole budget is simply not cached. 32 MiB covers thousands of CJK
// glyphs at body sizes.
constexpr std::size_t kShapedRunCacheBytes = 16u * 1024u * 1024u;
constexpr std::size_t kShapedRunCacheEntries = 16384u;
constexpr std::size_t kGlyphCoverageCacheBytes = 32u * 1024u * 1024u;
constexpr std::size_t kGlyphCoverageCacheEntries = 16384u;

// Approximate per-entry bookkeeping overhead charged against the byte budgets.
constexpr std::size_t kCacheEntryOverhead = 96u;

// Key for the shaped-run cache: the 26.6 fixed-point size (four bytes) followed
// by the exact UTF-8 bytes of the text. One map for every size instead of a
// nested size -> text map. Building the key can throw std::bad_alloc; callers
// treat that as a cache miss.
std::string ShapedRunKey(std::int32_t size_26_6, std::string_view text) {
  std::string key;
  key.resize(sizeof(size_26_6) + text.size());
  std::memcpy(key.data(), &size_26_6, sizeof(size_26_6));
  if (!text.empty()) {
    std::memcpy(key.data() + sizeof(size_26_6), text.data(), text.size());
  }
  return key;
}

// Key for the glyph coverage cache: (glyph id, size 26.6). Freetype's rendered
// coverage depends only on the face size (which the 26.6 scale fully determines)
// and the glyph id, so this key is exact.
std::uint64_t GlyphCoverageKey(std::int32_t size_26_6, std::uint32_t glyph_id) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(size_26_6)) << 32) | glyph_id;
}

// Bytes charged for one shaped-run cache entry (the key is stored twice: in the
// node and in the map index).
std::size_t ShapedRunCost(const std::string& key, const ShapedRun& run) {
  return 2u * key.size() + run.glyphs.capacity() * sizeof(ShapedGlyph) + kCacheEntryOverhead;
}

// Whether a text of `text_bytes` can ever fit the shaped-run byte budget. Used
// to skip key building entirely for enormous unique runs.
bool ShapedRunCanBeCached(std::size_t text_bytes) {
  return text_bytes <= (kShapedRunCacheBytes - kCacheEntryOverhead) / 2u;
}

// Bytes charged for one glyph coverage cache entry.
std::size_t GlyphCoverageCost(std::size_t bitmap_bytes) {
  return bitmap_bytes + kCacheEntryOverhead;
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

// Paints one 8-bit coverage bitmap with the frozen per-glyph integer placement
// and SrcOver compositing. `bitmap_left`/`bitmap_top` are freetype's placement
// relative to the glyph origin, the origin is already rounded to device pixels,
// and (clip_x, clip_y, clip_right, clip_bottom) is the current clip. The same
// helper serves a freshly loaded freetype slot and a cached copy, so a cache hit
// and a cache miss paint exactly the same pixels.
void PaintCoverage(const unsigned char* coverage, int pitch, int width, int rows, int bitmap_left,
                   int bitmap_top, long long glyph_origin_x, long long glyph_baseline_y,
                   unsigned int source_red, unsigned int source_green, unsigned int source_blue,
                   Argb color, Surface& surface, int clip_x, int clip_y, long long clip_right,
                   long long clip_bottom) {
  const long long bitmap_x = glyph_origin_x + bitmap_left;
  const long long bitmap_y = glyph_baseline_y - bitmap_top;

  // Visible part of the coverage bitmap, in bitmap pixel coordinates.
  const long long visible_left = std::max<long long>(0, static_cast<long long>(clip_x) - bitmap_x);
  const long long visible_right =
      std::min<long long>(static_cast<long long>(width), clip_right - bitmap_x);
  const long long visible_top = std::max<long long>(0, static_cast<long long>(clip_y) - bitmap_y);
  const long long visible_bottom =
      std::min<long long>(static_cast<long long>(rows), clip_bottom - bitmap_y);
  if (visible_left >= visible_right || visible_top >= visible_bottom) {
    return;
  }

  for (long long bitmap_row = visible_top; bitmap_row < visible_bottom; ++bitmap_row) {
    const unsigned char* coverage_row = coverage + bitmap_row * pitch;
    Argb* surface_row =
        surface.pixels() + static_cast<std::size_t>(bitmap_y + bitmap_row) * surface.stride();
    for (long long bitmap_column = visible_left; bitmap_column < visible_right; ++bitmap_column) {
      const unsigned int pixel_coverage = coverage_row[bitmap_column];
      if (pixel_coverage == 0) {
        continue;
      }
      Argb* pixel = surface_row + static_cast<int>(bitmap_x + bitmap_column);
      if (pixel_coverage == 255) {
        *pixel = 0xFF000000u | (color & 0x00FFFFFFu);
        continue;
      }
      const Argb destination = *pixel;
      const unsigned int red =
          BlendChannel(source_red, (destination >> 16) & 0xFFu, pixel_coverage);
      const unsigned int green =
          BlendChannel(source_green, (destination >> 8) & 0xFFu, pixel_coverage);
      const unsigned int blue = BlendChannel(source_blue, destination & 0xFFu, pixel_coverage);
      // The blend result is always an opaque pixel, like fills and images.
      *pixel = 0xFF000000u | (red << 16) | (green << 8) | blue;
    }
  }
}

}  // namespace

// Private font state. Member order matters for destruction: the destructor body
// releases the harfbuzz objects and the face while the library and the font
// bytes are still alive, then the library; the byte vector is destroyed last.
struct Font::Impl {
  ~Impl() {
    // Destroying the hb_font releases the hb_face and the reference it holds on
    // the freetype face, so it must happen before FT_Done_Face.
    if (hb_buffer != nullptr) {
      hb_buffer_destroy(hb_buffer);
    }
    if (hb_font != nullptr) {
      hb_font_destroy(hb_font);
    }
    if (face != nullptr) {
      FT_Done_Face(face);
    }
    if (library != nullptr) {
      FT_Done_FreeType(library);
    }
  }

  // Sets the freetype face to `size_26_6` at 72 dpi, skipping the call when the
  // face already has that size. Returns false when freetype rejects the size.
  bool EnsureCharSize(FT_F26Dot6 size_26_6) {
    if (face == nullptr) {
      return false;
    }
    if (face_size_26_6 == size_26_6) {
      return true;
    }
    if (FT_Set_Char_Size(face, size_26_6, size_26_6, 0, 0) != 0) {
      face_size_26_6 = 0;  // unknown; force a retry on the next call
      return false;
    }
    face_size_26_6 = size_26_6;
    return true;
  }

  // Creates the one harfbuzz font and buffer this Font owns. Both are created
  // lazily so loading a Font stays as cheap and as failure-free as before; a
  // failure here is a shaping failure, exactly like a failed hb_font_create in
  // the pre-M11 per-call implementation.
  bool EnsureHarfbuzz() {
    if (hb_font == nullptr) {
      hb_font = hb_ft_font_create_referenced(face);
      if (hb_font == nullptr) {
        return false;
      }
    }
    if (hb_buffer == nullptr) {
      hb_buffer = hb_buffer_create();
      if (hb_buffer == nullptr) {
        return false;
      }
    }
    return true;
  }

  // Shapes one UTF-8 run at `size_26_6`: sizes the face, shapes with the one
  // reused harfbuzz font/buffer and copies the glyph ids, advances and offsets
  // out. Returns false when the size cannot be applied, harfbuzz fails or the
  // glyph vector cannot be allocated; callers treat that as an empty run. This
  // is the pre-M11 ShapeText with the per-call hb_font/hb_buffer removed: the
  // hb_font's scale is refreshed from the resized freetype face with
  // hb_ft_font_changed (the documented "FT_Face size changed" update), which
  // produces exactly the scale/ppem of a freshly created hb_ft_font.
  bool ShapeRun(FT_F26Dot6 size_26_6, std::string_view text_utf8, ShapedRun* run) {
    run->glyphs.clear();
    run->direction = HB_DIRECTION_INVALID;
    if (text_utf8.empty() || !EnsureCharSize(size_26_6)) {
      return false;
    }
    if (!EnsureHarfbuzz()) {
      return false;
    }
    hb_ft_font_changed(hb_font);

    // Reusing the buffer is safe because every run restores the properties a
    // freshly created buffer would have before adding its own text.
    hb_buffer_reset(hb_buffer);
    hb_buffer_set_direction(hb_buffer, HB_DIRECTION_INVALID);
    hb_buffer_set_script(hb_buffer, HB_SCRIPT_INVALID);
    hb_buffer_set_language(hb_buffer, HB_LANGUAGE_INVALID);
    // hb_buffer_add_utf8 takes an int length; clamp enormous input defensively
    // (the renderer and MeasureTextWidth both use this same truncation).
    const std::size_t max_length = static_cast<std::size_t>(std::numeric_limits<int>::max());
    const int length = static_cast<int>(std::min(text_utf8.size(), max_length));
    hb_buffer_add_utf8(hb_buffer, text_utf8.data(), length, 0, -1);
    hb_buffer_guess_segment_properties(hb_buffer);
    hb_shape(hb_font, hb_buffer, nullptr, 0);

    unsigned glyph_count = 0;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buffer, &glyph_count);
    unsigned position_count = 0;
    const hb_glyph_position_t* positions =
        hb_buffer_get_glyph_positions(hb_buffer, &position_count);
    const unsigned count = std::min(glyph_count, position_count);
    run->direction = hb_buffer_get_direction(hb_buffer);
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
      return false;
    }
    return true;
  }

  // Cache lookups/builders. A failed key allocation is an ordinary miss, and a
  // text longer than the whole cache budget is never keyed at all: copying an
  // enormous unique run into a key just to exceed the budget would only add
  // memory churn to a guaranteed miss.
  const ShapedRun* FindShapedRun(std::int32_t size_26_6, std::string_view text_utf8) {
    if (!ShapedRunCanBeCached(text_utf8.size())) {
      return nullptr;
    }
    try {
      return shaped_runs.Get(ShapedRunKey(size_26_6, text_utf8));
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }

  // Stores a copy of `run`. Best effort: an allocation failure only loses a
  // future cache hit.
  void StoreShapedRun(std::int32_t size_26_6, std::string_view text_utf8, const ShapedRun& run) {
    if (!ShapedRunCanBeCached(text_utf8.size())) {
      return;
    }
    try {
      std::string key = ShapedRunKey(size_26_6, text_utf8);
      (void)shaped_runs.Put(key, run, ShapedRunCost(key, run));
    } catch (const std::bad_alloc&) {
    }
  }

  const GlyphCoverage* FindGlyphCoverage(std::int32_t size_26_6, std::uint32_t glyph_id) {
    return glyph_coverage.Get(GlyphCoverageKey(size_26_6, glyph_id));
  }

  // Caches that a glyph has no paintable coverage at this size, so repeated
  // draws skip the freetype load exactly like the pre-M11 code skipped the
  // failed/empty slot.
  void StoreMissingGlyph(std::int32_t size_26_6, std::uint32_t glyph_id) {
    try {
      GlyphCoverage coverage;  // usable stays false
      (void)glyph_coverage.Put(GlyphCoverageKey(size_26_6, glyph_id), std::move(coverage),
                               GlyphCoverageCost(0));
    } catch (const std::bad_alloc&) {
    }
  }

  // Copies a freshly rendered freetype slot into the coverage cache. Best
  // effort: the caller has already painted from the slot, so a failed copy only
  // costs a future cache miss.
  void StoreGlyphCoverage(std::int32_t size_26_6, std::uint32_t glyph_id, const FT_Bitmap& bitmap,
                          int bitmap_left, int bitmap_top) {
    const std::size_t bitmap_bytes =
        static_cast<std::size_t>(bitmap.rows) * static_cast<std::size_t>(bitmap.pitch);
    try {
      GlyphCoverage coverage;
      coverage.usable = true;
      coverage.bitmap_left = bitmap_left;
      coverage.bitmap_top = bitmap_top;
      coverage.width = static_cast<int>(bitmap.width);
      coverage.rows = static_cast<int>(bitmap.rows);
      coverage.pitch = bitmap.pitch;
      coverage.coverage.assign(bitmap.buffer, bitmap.buffer + bitmap_bytes);
      (void)glyph_coverage.Put(GlyphCoverageKey(size_26_6, glyph_id), std::move(coverage),
                               GlyphCoverageCost(bitmap_bytes));
    } catch (const std::bad_alloc&) {
    }
  }

  std::vector<unsigned char> bytes;
  FT_Library library = nullptr;
  FT_Face face = nullptr;
  // The single harfbuzz font (and through it the single hb_face) of this Font:
  // this is what makes harfbuzz's shape-plan cache effective across calls.
  hb_font_t* hb_font = nullptr;
  hb_buffer_t* hb_buffer = nullptr;
  // Current freetype face size in 26.6; 0 when no size has been set successfully.
  FT_F26Dot6 face_size_26_6 = 0;
  internal::LruCache<std::string, ShapedRun> shaped_runs{kShapedRunCacheBytes,
                                                         kShapedRunCacheEntries};
  internal::LruCache<std::uint64_t, GlyphCoverage> glyph_coverage{kGlyphCoverageCacheBytes,
                                                                  kGlyphCoverageCacheEntries};
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
  FT_F26Dot6 scale = 0;
  if (!ComputeScale(effective_size, &scale)) {
    return 0.0f;
  }
  // The effective size is clamped to kMaxRealFontSizePx, so its 26.6 value
  // always fits the 32-bit cache-key half.
  const auto size_26_6 = static_cast<std::int32_t>(scale);
  const ShapedRun* run = impl_->FindShapedRun(size_26_6, text_utf8);
  ShapedRun local_run;
  if (run == nullptr) {
    if (!impl_->ShapeRun(scale, text_utf8, &local_run)) {
      return 0.0f;
    }
    impl_->StoreShapedRun(size_26_6, text_utf8, local_run);
    run = &local_run;
  }
  // The frozen contract sums the shaped x advances in 26.6 and divides by 64.
  long long total_26_6 = 0;
  for (const ShapedGlyph& glyph : run->glyphs) {
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
//
// M11 caches only the two expensive inputs of that formula: the shaped run
// (keyed by effective size and UTF-8 text) and the freetype coverage bitmap
// (keyed by glyph id and size). A cache hit replays the same slots and the same
// frozen loop, so cached and uncached draws are pixel-identical; eviction can
// only make a draw slower, never different.
void FontRasterizer::DrawText(const Font& font, std::string_view text_utf8, int x, int baseline_y,
                              float font_size_px, Argb color, Surface& surface, int clip_x,
                              int clip_y, int clip_width, int clip_height) {
  const float effective_size = EffectiveSize(font_size_px);
  if (text_utf8.empty() || clip_width <= 0 || clip_height <= 0 || effective_size <= 0.0f ||
      font.impl_ == nullptr || font.impl_->face == nullptr) {
    return;
  }
  Font::Impl& impl = *font.impl_;
  FT_F26Dot6 scale = 0;
  if (!ComputeScale(effective_size, &scale)) {
    return;
  }
  // The effective size is clamped to kMaxRealFontSizePx, so its 26.6 value
  // always fits the 32-bit cache-key half.
  const auto size_26_6 = static_cast<std::int32_t>(scale);
  const ShapedRun* run = impl.FindShapedRun(size_26_6, text_utf8);
  ShapedRun local_run;
  if (run == nullptr) {
    if (!impl.ShapeRun(scale, text_utf8, &local_run)) {
      return;
    }
    impl.StoreShapedRun(size_26_6, text_utf8, local_run);
    run = &local_run;
  }
  if (run->glyphs.empty()) {
    return;
  }

  const FT_Face face = impl.face;
  // Bounded work: for a left-to-right run whose advances never move backwards,
  // once the leftmost ink any glyph can paint is past the clip's right edge, no
  // later glyph can be visible. The bound is conservative: face->bbox.xMin is
  // the minimum x over all glyphs in font units, converted to 26.6 and widened
  // by one pixel for hinting/rounding shifts. Other runs are iterated in full
  // (still only per glyph; pixels are clipped per glyph below).
  bool advances_non_negative = true;
  for (const ShapedGlyph& glyph : run->glyphs) {
    if (glyph.x_advance < 0) {
      advances_non_negative = false;
      break;
    }
  }
  bool can_break_early =
      run->direction == HB_DIRECTION_LTR && advances_non_negative && face->units_per_EM > 0;
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
  for (const ShapedGlyph& glyph : run->glyphs) {
    if (can_break_early && pen_x_26_6 + glyph.x_offset + minimum_ink_x_26_6 > pen_start_limit) {
      break;
    }
    const long long position_x_26_6 = pen_x_26_6 + glyph.x_offset;
    const long long position_y_26_6 = pen_y_26_6 + glyph.y_offset;
    pen_x_26_6 += glyph.x_advance;
    pen_y_26_6 += glyph.y_advance;
    // Keep the placement arithmetic in 64 bits: only coordinates that pass the
    // clip test below are narrowed back to device-pixel indices.
    const long long glyph_origin_x = static_cast<long long>(x) + Round26_6(position_x_26_6);
    const long long glyph_baseline_y =
        static_cast<long long>(baseline_y) - Round26_6(position_y_26_6);

    const GlyphCoverage* cached = impl.FindGlyphCoverage(size_26_6, glyph.glyph_id);
    if (cached != nullptr) {
      if (!cached->usable) {
        continue;
      }
      PaintCoverage(cached->coverage.data(), cached->pitch, cached->width, cached->rows,
                    cached->bitmap_left, cached->bitmap_top, glyph_origin_x, glyph_baseline_y,
                    source_red, source_green, source_blue, color, surface, clip_x, clip_y,
                    clip_right, clip_bottom);
      continue;
    }

    // Cache miss: load and render the glyph through freetype exactly like the
    // pre-M11 path, paint from the slot, then cache a copy for later draws.
    if (!impl.EnsureCharSize(scale) ||
        FT_Load_Glyph(face, glyph.glyph_id, FT_LOAD_DEFAULT | FT_LOAD_RENDER) != 0) {
      impl.StoreMissingGlyph(size_26_6, glyph.glyph_id);
      continue;
    }
    const FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.pixel_mode != FT_PIXEL_MODE_GRAY ||
        bitmap.pitch <= 0) {
      impl.StoreMissingGlyph(size_26_6, glyph.glyph_id);
      continue;
    }
    PaintCoverage(bitmap.buffer, bitmap.pitch, static_cast<int>(bitmap.width),
                  static_cast<int>(bitmap.rows), slot->bitmap_left, slot->bitmap_top,
                  glyph_origin_x, glyph_baseline_y, source_red, source_green, source_blue, color,
                  surface, clip_x, clip_y, clip_right, clip_bottom);
    impl.StoreGlyphCoverage(size_26_6, glyph.glyph_id, bitmap, slot->bitmap_left, slot->bitmap_top);
  }
}

}  // namespace internal

}  // namespace toy::render
