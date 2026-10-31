// Display-list construction: the box tree becomes an ordered list of background
// and border fills, image draws and real-font text runs, clipped to the viewport
// and scrolled vertically. Since M14b an element paints background -> borders ->
// image -> children, an overflow:hidden box clips its children to its padding
// box, and text decorations (underline/line-through/overline) follow each text
// command. Since M11, commands that cannot intersect the viewport
// after the scroll translation are culled (see IntersectsViewport), so a scroll
// or resize frame only pays for the visible part of a long page. No
// rasterization happens here; render executes the list.

#include "toy/paint/paint.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/render/font.h"
#include "toy/render/paint.h"
#include "toy/style/computed_style.h"

namespace toy::paint {
namespace {

// CSS px -> device px with the same std::lround the box dump uses, so a painted
// rectangle always matches the dumped box rectangle. The viewport scroll offset
// is already in device pixels, so it is subtracted after the rounding.
int ToDevicePixels(float css_px) { return static_cast<int>(std::lround(css_px)); }

int TranslatedY(const layout::Box& box, int scroll_y) { return ToDevicePixels(box.y) - scroll_y; }

// Viewport culling (M11). The whole display list is clipped to the viewport
// rectangle [0, viewport.width) x [0, viewport.height), so a command whose
// painted device-pixel rectangle does not intersect that clip contributes no
// pixel. Omitting it is pixel-identical to emitting it and is what makes a
// scroll/resize frame proportional to the visible part of the page instead of
// the whole page. Rectangles are half-open, exactly like the rasterizer's clip
// intersection; non-positive sizes paint nothing and are culled too.
bool IntersectsViewport(long long x, long long y, long long width, long long height,
                        const Viewport& viewport) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  return x < static_cast<long long>(viewport.width) && x + width > 0 &&
         y < static_cast<long long>(viewport.height) && y + height > 0;
}

// Text commands are placed at the rounded box origin, but the painted glyphs are
// not bounded exactly by the box rectangle: real-font ink can overhang the
// advance box (side bearings, italic overhang, anti-aliased coverage spilling
// past the last column) and fractional geometry rounds each edge by up to a
// device pixel. The culling test therefore expands the text box by
// 2 * font_size_px on every side, which is larger than any glyph overhang the
// frozen rasterizers can produce for a real face at that size; a text box whose
// expanded rectangle misses the viewport cannot paint a visible pixel. The
// margin is clamped so a hostile or non-finite CSS size cannot overflow the
// 64-bit arithmetic (the real-font path itself caps sizes at 4096 px).
long long TextCullingMarginPx(float font_size_px) {
  if (!(font_size_px > 0.0f)) {
    return 0;
  }
  const double margin = 2.0 * static_cast<double>(font_size_px);
  constexpr double kMaxMarginPx = 1.0e9;
  if (!(margin < kMaxMarginPx)) {  // Also catches infinity and NaN.
    return static_cast<long long>(kMaxMarginPx);
  }
  return static_cast<long long>(std::ceil(margin));
}

// Element boxes paint their computed background unless it is fully transparent
// (alpha 0). Text never paints a background, and anonymous boxes have no
// DOM/style origin at all. The integer rect is culled exactly against the
// viewport before the style is consulted.
void AppendBackground(const dom::Document& document, const style::StyleTree& styles,
                      const layout::Box& box, const Viewport& viewport, render::DisplayList* list) {
  if (box.kind != layout::BoxKind::kBlock && box.kind != layout::BoxKind::kInline) {
    return;
  }
  if (box.node == dom::kInvalidNode) {
    return;
  }
  if (document.Get(box.node).type != dom::NodeType::kElement) {
    return;
  }
  const int x = ToDevicePixels(box.x);
  const int y = TranslatedY(box, viewport.scroll_y);
  const int width = ToDevicePixels(box.width);
  const int height = ToDevicePixels(box.height);
  if (!IntersectsViewport(x, y, width, height, viewport)) {
    return;
  }
  const style::ComputedStyle& style = styles.Get(box.node);
  // visibility: hidden removes the background but never the layout space; the
  // pre-order walk still visits descendants, so a visible child paints normally.
  if (style.visibility == style::Visibility::kHidden) {
    return;
  }
  const std::uint32_t background = style.background_color;
  if ((background >> 24) == 0) {
    return;
  }
  list->FillRect(x, y, width, height, background);
}

// Element boxes paint their used border sides as up to four device-pixel
// rectangles between the background and the image/children, in top, right,
// bottom, left order. Each rectangle is culled exactly against the viewport like
// the background; non-positive sides (none/hidden style, zero width or a width
// that rounds to zero device pixels) emit nothing. All visible styles paint solid
// in this MVP. A hidden element paints no borders: visibility removes the whole
// box decoration (the layout space stays).
void AppendBorders(const dom::Document& document, const style::StyleTree& styles,
                   const layout::Box& box, const Viewport& viewport, render::DisplayList* list) {
  if (box.kind != layout::BoxKind::kBlock && box.kind != layout::BoxKind::kInline) {
    return;
  }
  if (box.node == dom::kInvalidNode) {
    return;
  }
  if (document.Get(box.node).type != dom::NodeType::kElement) {
    return;
  }
  const style::ComputedStyle& style = styles.Get(box.node);
  if (style.visibility == style::Visibility::kHidden) {
    return;
  }
  const float top_width = style::UsedBorderWidth(style.border_top);
  const float right_width = style::UsedBorderWidth(style.border_right);
  const float bottom_width = style::UsedBorderWidth(style.border_bottom);
  const float left_width = style::UsedBorderWidth(style.border_left);
  if (top_width <= 0.0f && right_width <= 0.0f && bottom_width <= 0.0f && left_width <= 0.0f) {
    return;
  }
  const int x = ToDevicePixels(box.x);
  const int width = ToDevicePixels(box.width);
  const int top_y = TranslatedY(box, viewport.scroll_y);
  const int top = ToDevicePixels(top_width);
  const int right = ToDevicePixels(right_width);
  const int bottom = ToDevicePixels(bottom_width);
  const int left = ToDevicePixels(left_width);
  const int box_right = ToDevicePixels(box.x + box.width);
  const int bottom_y = ToDevicePixels(box.y + box.height) - viewport.scroll_y - bottom;
  const int side_y = top_y + top;
  const int side_height = ToDevicePixels(box.height) - top - bottom;
  const auto fill = [&](int rect_x, int rect_y, int rect_width, int rect_height,
                        const style::BorderSide& side) {
    if (!IntersectsViewport(rect_x, rect_y, rect_width, rect_height, viewport)) {
      return;
    }
    list->FillRect(rect_x, rect_y, rect_width, rect_height,
                   style::UsedBorderColor(side, style.color));
  };
  if (top_width > 0.0f) {
    fill(x, top_y, width, top, style.border_top);
  }
  if (right_width > 0.0f) {
    fill(box_right - right, side_y, right, side_height, style.border_right);
  }
  if (bottom_width > 0.0f) {
    fill(x, bottom_y, width, bottom, style.border_bottom);
  }
  if (left_width > 0.0f) {
    fill(x, side_y, left, side_height, style.border_left);
  }
}

// Text decorations are one device-pixel FillRect per computed flag, emitted after
// that text box's text command(s) in the frozen underline, line-through, overline
// order. The text culling ran before this call (the same expanded text-box check
// as the text command), so a decoration can only be painted together with its
// text. Decorations are never painted for hidden text: callers return first.
void AppendDecorations(const style::ComputedStyle& style, const layout::Box& box,
                       const Viewport& viewport, render::DisplayList* list) {
  const int x = ToDevicePixels(box.x);
  const int width = ToDevicePixels(box.width);
  const int top_y = TranslatedY(box, viewport.scroll_y);
  const int baseline_y = top_y + ToDevicePixels(box.baseline_offset_px);
  const std::uint32_t color = style.color | 0xFF000000u;
  const auto emit = [&](int row_y) { list->FillRect(x, row_y, width, 1, color); };
  if (style.text_decoration_underline) {
    emit(baseline_y + 1);
  }
  if (style.text_decoration_line_through) {
    const int offset = std::max(1, ToDevicePixels(style.font_size_px / 3.0f));
    emit(baseline_y - offset);
  }
  if (style.text_decoration_overline) {
    emit(top_y);
  }
}

// True when `box` is an element box whose computed overflow hides content on
// either axis: its descendants are painted inside one clip pair for the padding
// box (visibility does not disable the clipping of a visible descendant).
bool ClipsChildren(const dom::Document& document, const style::StyleTree& styles,
                   const layout::Box& box) {
  if (box.kind != layout::BoxKind::kBlock && box.kind != layout::BoxKind::kInline) {
    return false;
  }
  if (box.node == dom::kInvalidNode) {
    return false;
  }
  if (document.Get(box.node).type != dom::NodeType::kElement) {
    return false;
  }
  const style::ComputedStyle& style = styles.Get(box.node);
  return style.overflow_x == style::Overflow::kHidden ||
         style.overflow_y == style::Overflow::kHidden;
}

// Emits the padding-box clip rectangle of an overflow:hidden box, translated by
// the scroll offset. The rasterizer intersects it with the viewport and its own
// clip, so a non-positive rect paints nothing; the pair is still emitted so the
// display-list shape stays deterministic.
void PushOverflowClip(const style::StyleTree& styles, const layout::Box& box,
                      const Viewport& viewport, render::DisplayList* list) {
  const style::ComputedStyle& style = styles.Get(box.node);
  const int x = ToDevicePixels(box.x + style::UsedBorderWidth(style.border_left));
  const int y =
      ToDevicePixels(box.y + style::UsedBorderWidth(style.border_top)) - viewport.scroll_y;
  const int right =
      ToDevicePixels(box.x + box.width - style::UsedBorderWidth(style.border_right));
  const int bottom =
      ToDevicePixels(box.y + box.height - style::UsedBorderWidth(style.border_bottom)) -
      viewport.scroll_y;
  list->PushClip(x, y, right - x, bottom - y);
}

// A text box keeps the inherited computed style; its color is forced opaque
// because style colors carry no alpha channel in M4.
//
// The caller's runs provider decides between real-font runs and the frozen
// M6/M7 bitmap command. A null provider, or an empty run vector for this box,
// keeps the bitmap path: y is the top of the glyph cell and x is round(box.x),
// exactly as before M10. With real-font runs every run becomes one
// DrawTextWithFont command in order: the baseline y is shared by the box (layout
// computed the line ascent into baseline_offset_px) and the x comes from a pen
// that starts at box.x and advances by each run's shaped width, so the runs stay
// contiguous and match layout's measured width.
void AppendText(const style::StyleTree& styles, const layout::Box& box, const Viewport& viewport,
                const FontRunsProvider& font_runs, render::DisplayList* list) {
  const style::ComputedStyle& style = styles.Get(box.node);
  // Hidden text paints nothing (and so does its underline); the layout space is
  // unchanged because layout already ran.
  if (style.visibility == style::Visibility::kHidden) {
    return;
  }
  const float font_size_px = style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
  // Cull before asking the runs provider: resolving/font-shaping a text box that
  // cannot be visible is the dominant per-frame cost on text-heavy pages.
  const long long margin = TextCullingMarginPx(font_size_px);
  const long long x = ToDevicePixels(box.x);
  const long long y = TranslatedY(box, viewport.scroll_y);
  const long long width = ToDevicePixels(box.width);
  const long long height = ToDevicePixels(box.height);
  if (!IntersectsViewport(x - margin, y - margin, width + 2 * margin, height + 2 * margin,
                          viewport)) {
    return;
  }
  const std::uint32_t color = style.color | 0xFF000000u;
  if (font_runs) {
    const std::vector<FontRun> runs = font_runs(style, box.text);
    if (!runs.empty()) {
      const int baseline_y = static_cast<int>(y) + ToDevicePixels(box.baseline_offset_px);
      float pen_x = box.x;
      for (const FontRun& run : runs) {
        assert(run.font != nullptr);
        if (!run.font) {
          continue;
        }
        list->DrawTextWithFont(run.font, run.text, ToDevicePixels(pen_x), baseline_y, font_size_px,
                               color);
        pen_x += run.font->MeasureTextWidth(run.text, font_size_px);
      }
      AppendDecorations(style, box, viewport, list);
      return;
    }
  }
  list->DrawText(box.text, static_cast<int>(x), static_cast<int>(y), font_size_px, color);
  AppendDecorations(style, box, viewport, list);
}

// An inline replaced box paints its image after its own background and before
// its children (it has none in this MVP). The image map is keyed by DOM node id,
// the same key layout used for the replaced size; a node without an entry paints
// nothing, which keeps image-less pages byte-identical to M7. A null image is
// treated as absent so the list only carries drawable commands. The destination
// rect is culled exactly against the viewport (the image only paints inside it).
void AppendImage(const style::StyleTree& styles, const layout::Box& box,
                 const Viewport& viewport, const ImageMap& images, render::DisplayList* list) {
  if (box.kind != layout::BoxKind::kInline || box.node == dom::kInvalidNode) {
    return;
  }
  const int x = ToDevicePixels(box.x);
  const int y = TranslatedY(box, viewport.scroll_y);
  const int width = ToDevicePixels(box.width);
  const int height = ToDevicePixels(box.height);
  if (!IntersectsViewport(x, y, width, height, viewport)) {
    return;
  }
  if (styles.Get(box.node).visibility == style::Visibility::kHidden) {
    return;
  }
  const auto entry = images.find(box.node);
  if (entry == images.end() || !entry->second) {
    return;
  }
  list->DrawImage(entry->second, x, y, width, height);
}

}  // namespace

render::DisplayList BuildDisplayList(const dom::Document& document, const style::StyleTree& styles,
                                     const layout::BoxTree& boxes, const Viewport& viewport,
                                     const ImageMap& images, const FontRunsProvider& font_runs) {
  assert(viewport.width > 0);
  assert(viewport.height > 0);
  assert(viewport.scroll_y >= 0);
  render::DisplayList list;
  const layout::BoxId root = boxes.root();
  if (root == layout::kInvalidBox) {
    return list;
  }
  // The whole page is clipped to the viewport, so scrolled content cannot paint
  // outside it; the rasterizer intersects this with its own surface clip. The
  // clip pair is emitted even when every page command is culled: a non-empty box
  // tree whose content is all off-screen keeps the frozen clip-pair shape.
  list.PushClip(0, 0, viewport.width, viewport.height);
  // Iterative pre-order walk, like the box dump: deep pages must not overflow
  // the C++ stack. Culling only drops commands whose rect misses the viewport,
  // so the relative order of the emitted commands is unchanged. An
  // overflow:hidden element emits its padding-box clip before its children and a
  // matching PopClip after the whole subtree (a marker entry on the stack).
  struct PendingEntry {
    layout::BoxId box = layout::kInvalidBox;
    bool pop_clip = false;
  };
  std::vector<PendingEntry> pending;
  pending.push_back(PendingEntry{root, false});
  std::vector<layout::BoxId> children;
  while (!pending.empty()) {
    const PendingEntry entry = pending.back();
    pending.pop_back();
    if (entry.pop_clip) {
      list.PopClip();
      continue;
    }
    const layout::Box& box = boxes.Get(entry.box);
    switch (box.kind) {
      case layout::BoxKind::kBlock:
      case layout::BoxKind::kInline:
        AppendBackground(document, styles, box, viewport, &list);
        AppendBorders(document, styles, box, viewport, &list);
        AppendImage(styles, box, viewport, images, &list);
        break;
      case layout::BoxKind::kText:
        AppendText(styles, box, viewport, font_runs, &list);
        break;
      case layout::BoxKind::kAnonymousBlock:
        break;
    }
    const bool clips = ClipsChildren(document, styles, box);
    children.clear();
    for (layout::BoxId child = box.first_child; child != layout::kInvalidBox;
         child = boxes.Get(child).next_sibling) {
      children.push_back(child);
    }
    // Push the pop marker first so it is processed after the whole subtree, then
    // the children in reverse order (pre-order walk).
    if (clips) {
      PushOverflowClip(styles, box, viewport, &list);
      pending.push_back(PendingEntry{layout::kInvalidBox, true});
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(PendingEntry{children[index], false});
    }
  }
  list.PopClip();
  return list;
}

}  // namespace toy::paint
