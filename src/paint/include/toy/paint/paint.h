#pragma once

#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/render/paint.h"
#include "toy/style/computed_style.h"

namespace toy::paint {

// Paints a laid-out page into a display list: it walks the box tree in pre-order
// and emits one background FillRect per element box whose computed background is
// not fully transparent, then one DrawText per text box.
//
// The list is pure data: paint never rasterizes and render never learns about
// HTML or CSS. Geometry is converted from CSS pixels to device pixels with
// std::lround (the same rounding as the box dump); negative coordinates pass
// through and the rasterizer clips them. Pure and deterministic: the same
// inputs always produce the same commands, and an empty box tree yields an
// empty list.
//
// M6 emits no PushClip/PopClip commands: the clip stack lives in render and is
// unit-tested there; overflow and scroll clipping arrive with a later milestone.
render::DisplayList BuildDisplayList(const dom::Document& document, const style::StyleTree& styles,
                                     const layout::BoxTree& boxes);

}  // namespace toy::paint
