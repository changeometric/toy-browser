#pragma once

#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/render/paint.h"
#include "toy/style/computed_style.h"

namespace toy::paint {

// The visible part of a laid-out page: the viewport rectangle in device pixels
// and the vertical scroll offset in device pixels (1 CSS px = 1 device px).
// width and height must be positive and scroll_y must be >= 0 (asserted in
// debug builds). The scroll offset is an integer because device pixels are
// integers: the caller clamps and rounds the CSS-pixel scroll position.
struct Viewport {
  int width = 0;
  int height = 0;
  int scroll_y = 0;
};

// Paints a laid-out page into a display list: it walks the box tree in pre-order
// and emits one background FillRect per element box whose computed background is
// not fully transparent, then one DrawText per text box.
//
// The list is pure data: paint never rasterizes and render never learns about
// HTML or CSS. Geometry is converted from CSS pixels to device pixels with
// std::lround (the same rounding as the box dump) and every command's y is then
// translated by -viewport.scroll_y; x is unchanged. Negative coordinates pass
// through and the rasterizer clips them. Pure and deterministic: the same inputs
// always produce the same commands, and an empty box tree yields an empty list.
//
// The list is clipped to the viewport: when it is not empty, the first command
// is PushClip(0, 0, viewport.width, viewport.height), every page command follows
// with its translated y, and the last command is PopClip. This clipping is what
// stops scrolled content from painting outside the viewport; it is frozen paint
// behavior (M7) and asserted by the paint tests.
render::DisplayList BuildDisplayList(const dom::Document& document, const style::StyleTree& styles,
                                     const layout::BoxTree& boxes, const Viewport& viewport);

}  // namespace toy::paint
