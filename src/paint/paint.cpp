// Display-list construction: the box tree becomes an ordered list of background
// fills, image draws and real-font text runs, clipped to the viewport and
// scrolled vertically. No rasterization happens here; render executes the list.

#include "toy/paint/paint.h"

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

// Element boxes paint their computed background unless it is fully transparent
// (alpha 0). Text never paints a background, and anonymous boxes have no
// DOM/style origin at all.
void AppendBackground(const dom::Document& document, const style::StyleTree& styles,
                      const layout::Box& box, int scroll_y, render::DisplayList* list) {
  if (box.kind != layout::BoxKind::kBlock && box.kind != layout::BoxKind::kInline) {
    return;
  }
  if (box.node == dom::kInvalidNode) {
    return;
  }
  if (document.Get(box.node).type != dom::NodeType::kElement) {
    return;
  }
  const std::uint32_t background = styles.Get(box.node).background_color;
  if ((background >> 24) == 0) {
    return;
  }
  list->FillRect(ToDevicePixels(box.x), TranslatedY(box, scroll_y), ToDevicePixels(box.width),
                 ToDevicePixels(box.height), background);
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
void AppendText(const style::StyleTree& styles, const layout::Box& box, int scroll_y,
                const FontRunsProvider& font_runs, render::DisplayList* list) {
  const style::ComputedStyle& style = styles.Get(box.node);
  const std::uint32_t color = style.color | 0xFF000000u;
  const float font_size_px = style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
  if (font_runs) {
    const std::vector<FontRun> runs = font_runs(style, box.text);
    if (!runs.empty()) {
      const int baseline_y = TranslatedY(box, scroll_y) + ToDevicePixels(box.baseline_offset_px);
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
      return;
    }
  }
  list->DrawText(box.text, ToDevicePixels(box.x), TranslatedY(box, scroll_y), font_size_px, color);
}

// An inline replaced box paints its image after its own background and before
// its children (it has none in this MVP). The image map is keyed by DOM node id,
// the same key layout used for the replaced size; a node without an entry paints
// nothing, which keeps image-less pages byte-identical to M7. A null image is
// treated as absent so the list only carries drawable commands.
void AppendImage(const layout::Box& box, int scroll_y, const ImageMap& images,
                 render::DisplayList* list) {
  if (box.kind != layout::BoxKind::kInline || box.node == dom::kInvalidNode) {
    return;
  }
  const auto entry = images.find(box.node);
  if (entry == images.end() || !entry->second) {
    return;
  }
  list->DrawImage(entry->second, ToDevicePixels(box.x), TranslatedY(box, scroll_y),
                  ToDevicePixels(box.width), ToDevicePixels(box.height));
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
  // outside it; the rasterizer intersects this with its own surface clip.
  list.PushClip(0, 0, viewport.width, viewport.height);
  // Iterative pre-order walk, like the box dump: deep pages must not overflow
  // the C++ stack.
  std::vector<layout::BoxId> pending;
  pending.push_back(root);
  std::vector<layout::BoxId> children;
  while (!pending.empty()) {
    const layout::BoxId id = pending.back();
    pending.pop_back();
    const layout::Box& box = boxes.Get(id);
    switch (box.kind) {
      case layout::BoxKind::kBlock:
      case layout::BoxKind::kInline:
        AppendBackground(document, styles, box, viewport.scroll_y, &list);
        AppendImage(box, viewport.scroll_y, images, &list);
        break;
      case layout::BoxKind::kText:
        AppendText(styles, box, viewport.scroll_y, font_runs, &list);
        break;
      case layout::BoxKind::kAnonymousBlock:
        break;
    }
    children.clear();
    for (layout::BoxId child = box.first_child; child != layout::kInvalidBox;
         child = boxes.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(children[index]);
    }
  }
  list.PopClip();
  return list;
}

}  // namespace toy::paint
