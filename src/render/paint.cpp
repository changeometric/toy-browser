#include "toy/render/paint.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "font_internal.h"
#include "toy/render/font.h"

namespace toy::render {

void DisplayList::FillRect(int x, int y, int width, int height, Argb color) {
  Command command;
  command.kind = CommandKind::kFillRect;
  command.x = x;
  command.y = y;
  command.width = width;
  command.height = height;
  command.color = color;
  commands_.push_back(command);
}

void DisplayList::DrawText(std::string_view text_utf8, int x, int y, float font_size_px,
                           Argb color) {
  Command command;
  command.kind = CommandKind::kDrawText;
  command.x = x;
  command.y = y;
  command.color = color;
  command.text = std::string(text_utf8);
  command.font_size_px = font_size_px;
  commands_.push_back(command);
}

void DisplayList::PushClip(int x, int y, int width, int height) {
  Command command;
  command.kind = CommandKind::kPushClip;
  command.x = x;
  command.y = y;
  command.width = width;
  command.height = height;
  commands_.push_back(command);
}

void DisplayList::PopClip() {
  Command command;
  command.kind = CommandKind::kPopClip;
  commands_.push_back(command);
}

const std::vector<Command>& DisplayList::commands() const { return commands_; }

std::size_t DisplayList::size() const { return commands_.size(); }

namespace {

// Axis-aligned integer rectangle in device pixels, top-left origin.
struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

bool IsEmpty(const Rect& rect) { return rect.width <= 0 || rect.height <= 0; }

// Intersects two rectangles. The result is empty (zero width and height) when
// the rectangles do not overlap; the 64-bit intermediate prevents overflow in
// x + width and y + height.
Rect Intersect(const Rect& lhs, const Rect& rhs) {
  const long long left = std::max(static_cast<long long>(lhs.x), static_cast<long long>(rhs.x));
  const long long top = std::max(static_cast<long long>(lhs.y), static_cast<long long>(rhs.y));
  const long long right = std::min(static_cast<long long>(lhs.x) + lhs.width,
                                   static_cast<long long>(rhs.x) + rhs.width);
  const long long bottom = std::min(static_cast<long long>(lhs.y) + lhs.height,
                                    static_cast<long long>(rhs.y) + rhs.height);
  if (right <= left || bottom <= top) {
    return Rect{};
  }
  return Rect{static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left),
              static_cast<int>(bottom - top)};
}

// Returns the row pointer of `surface` at `y`; `y` must be in bounds.
Argb* RowOf(Surface& surface, int y) {
  return surface.pixels() +
         static_cast<std::size_t>(y) * static_cast<std::size_t>(surface.stride());
}

// One 8-bit SrcOver channel over an opaque destination, using the frozen
// integer formula (source * alpha + destination * (255 - alpha) + 127) / 255.
unsigned int BlendChannel(unsigned int source, unsigned int destination, unsigned int alpha,
                          unsigned int inverse_alpha) {
  return (source * alpha + destination * inverse_alpha + 127u) / 255u;
}

void PaintFillRect(const Command& command, Surface& surface, const Rect& clip) {
  if (command.width <= 0 || command.height <= 0) {
    return;
  }
  const unsigned int alpha = (command.color >> 24) & 0xFFu;
  if (alpha == 0) {
    return;
  }
  const Rect area = Intersect(clip, Rect{command.x, command.y, command.width, command.height});
  if (IsEmpty(area)) {
    return;
  }

  if (alpha == 255) {
    for (int row = area.y; row < area.y + area.height; ++row) {
      Argb* row_start = RowOf(surface, row);
      std::fill(row_start + area.x, row_start + area.x + area.width, command.color);
    }
    return;
  }

  const unsigned int source_red = (command.color >> 16) & 0xFFu;
  const unsigned int source_green = (command.color >> 8) & 0xFFu;
  const unsigned int source_blue = command.color & 0xFFu;
  const unsigned int inverse_alpha = 255u - alpha;
  for (int row = area.y; row < area.y + area.height; ++row) {
    Argb* row_start = RowOf(surface, row);
    for (int column = area.x; column < area.x + area.width; ++column) {
      const Argb destination = row_start[column];
      const unsigned int red =
          BlendChannel(source_red, (destination >> 16) & 0xFFu, alpha, inverse_alpha);
      const unsigned int green =
          BlendChannel(source_green, (destination >> 8) & 0xFFu, alpha, inverse_alpha);
      const unsigned int blue =
          BlendChannel(source_blue, destination & 0xFFu, alpha, inverse_alpha);
      // The blend result is always an opaque pixel, per the frozen semantics.
      row_start[column] = 0xFF000000u | (red << 16) | (green << 8) | blue;
    }
  }
}

// Upper bound for the rasterization cell. Page CSS can name an arbitrarily
// large px size; beyond this cap the cell is unrepresentable in a bounded
// rasterizer, and for any surface smaller than cap/8 pixels per axis the
// nearest-neighbour result is identical (visible pixels all sample design
// column/row 0). The cap also keeps the 64-bit products far from overflow.
constexpr float kMaxRasterFontSizePx = 1.0e9f;

// Paints one code point's 8x16 design cell at the device-pixel column `cell_x`
// and row `cell_y`. Only the part of the cell inside `clip` is visited, so the
// work is bounded by the clip area (the surface), never by the cell size. The
// source mapping uses 64-bit math so large cells cannot overflow. Glyph bits
// are mapped with integer nearest-neighbour scaling; unset bits leave the
// destination untouched.
void PaintGlyph(char32_t code_point, long long cell_x, long long cell_y, long long cell_width,
                long long cell_height, Argb color, Surface& surface, const Rect& clip) {
  const long long clip_right = static_cast<long long>(clip.x) + clip.width;
  const long long clip_bottom = static_cast<long long>(clip.y) + clip.height;
  const long long first_py = std::max<long long>(0, static_cast<long long>(clip.y) - cell_y);
  const long long last_py = std::min<long long>(cell_height, clip_bottom - cell_y);
  if (first_py >= last_py) {
    return;
  }
  const long long first_px = std::max<long long>(0, static_cast<long long>(clip.x) - cell_x);
  const long long last_px = std::min<long long>(cell_width, clip_right - cell_x);
  if (first_px >= last_px) {
    return;
  }

  const std::uint8_t* glyph = internal::GlyphRows(code_point);
  // Glyph pixels are fully opaque; only the low 24 bits of `color` are used.
  const Argb opaque = 0xFF000000u | (color & 0x00FFFFFFu);
  for (long long py = first_py; py < last_py; ++py) {
    const long long source_y = py * internal::kGlyphDesignHeight / cell_height;
    const std::uint8_t row_bits = glyph[source_y];
    // The visible rows and columns are inside the clip, which is always inside
    // the surface, so the narrowed coordinates fit a device-pixel index.
    Argb* row_start = RowOf(surface, static_cast<int>(cell_y + py));
    for (long long px = first_px; px < last_px; ++px) {
      const long long source_x = px * internal::kGlyphDesignWidth / cell_width;
      if ((row_bits & (0x80u >> source_x)) == 0) {
        continue;
      }
      row_start[static_cast<int>(cell_x + px)] = opaque;
    }
  }
}

void PaintText(const Command& command, Surface& surface, const Rect& clip) {
  // Non-finite sizes (inf and NaN) must never reach std::llround.
  if (!(std::isfinite(command.font_size_px) && command.font_size_px > 0.0f)) {
    return;
  }
  const float raster_size_px = std::min(command.font_size_px, kMaxRasterFontSizePx);
  const long long cell_width = std::max<long long>(1, std::llround(0.5f * raster_size_px));
  const long long cell_height = std::max<long long>(1, std::llround(raster_size_px));
  const float advance_px = command.font_size_px * (static_cast<float>(kGlyphAdvanceUnits) /
                                                   static_cast<float>(kFontDesignSizePx));
  const long long clip_right = static_cast<long long>(clip.x) + clip.width;
  float pen = static_cast<float>(command.x);
  std::size_t offset = 0;
  while (offset < command.text.size()) {
    // The pen only moves right, so once a cell origin is past the clip no
    // later glyph can be visible. Breaking here also keeps the pen small
    // enough for a defined std::llround.
    if (static_cast<double>(pen) > static_cast<double>(clip_right) + 1.0) {
      break;
    }
    char32_t code_point = 0;
    offset += internal::DecodeUtf8(command.text, offset, &code_point);
    const long long cell_x = std::llround(pen);
    PaintGlyph(code_point, cell_x, command.y, cell_width, cell_height, command.color, surface,
               clip);
    pen += advance_px;
  }
}

}  // namespace

void DrawDisplayList(const DisplayList& list, Surface& surface) {
  Rect clip{0, 0, surface.width(), surface.height()};
  std::vector<Rect> clip_stack;
  clip_stack.push_back(clip);
  for (const Command& command : list.commands()) {
    switch (command.kind) {
      case CommandKind::kFillRect:
        PaintFillRect(command, surface, clip);
        break;
      case CommandKind::kDrawText:
        PaintText(command, surface, clip);
        break;
      case CommandKind::kPushClip:
        clip = Intersect(clip, Rect{command.x, command.y, command.width, command.height});
        clip_stack.push_back(clip);
        break;
      case CommandKind::kPopClip:
        // The bottom entry is the initial whole-surface clip; it is never
        // popped, so a balanced-looking list cannot lose the initial clip and
        // a pop on an empty stack stays a no-op.
        if (clip_stack.size() > 1) {
          clip_stack.pop_back();
          clip = clip_stack.back();
        }
        break;
    }
  }
}

}  // namespace toy::render
