// Display-list construction: the box tree becomes an ordered list of background
// fills and text draws. No rasterization happens here; render executes the list.

#include "toy/paint/paint.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/render/paint.h"
#include "toy/style/computed_style.h"

namespace toy::paint {
namespace {

// CSS px -> device px with the same std::lround the box dump uses, so a painted
// rectangle always matches the dumped box rectangle.
int ToDevicePixels(float css_px) { return static_cast<int>(std::lround(css_px)); }

// Element boxes paint their computed background unless it is fully transparent
// (alpha 0). Text never paints a background, and anonymous boxes have no
// DOM/style origin at all.
void AppendBackground(const dom::Document& document, const style::StyleTree& styles,
                      const layout::Box& box, render::DisplayList* list) {
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
  list->FillRect(ToDevicePixels(box.x), ToDevicePixels(box.y), ToDevicePixels(box.width),
                 ToDevicePixels(box.height), background);
}

// A text box keeps the inherited computed style; its color is forced opaque
// because style colors carry no alpha channel in M4.
void AppendText(const style::StyleTree& styles, const layout::Box& box, render::DisplayList* list) {
  const style::ComputedStyle& style = styles.Get(box.node);
  const std::uint32_t color = style.color | 0xFF000000u;
  const float font_size_px = style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
  list->DrawText(box.text, ToDevicePixels(box.x), ToDevicePixels(box.y), font_size_px, color);
}

}  // namespace

render::DisplayList BuildDisplayList(const dom::Document& document, const style::StyleTree& styles,
                                     const layout::BoxTree& boxes) {
  render::DisplayList list;
  const layout::BoxId root = boxes.root();
  if (root == layout::kInvalidBox) {
    return list;
  }
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
        AppendBackground(document, styles, box, &list);
        break;
      case layout::BoxKind::kText:
        AppendText(styles, box, &list);
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
  return list;
}

}  // namespace toy::paint
