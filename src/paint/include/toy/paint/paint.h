#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/render/font.h"
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

// Decoded images of one page keyed by DOM element node id, supplied by the
// caller (shell). Layout received the same keys as replaced-element sizes, so an
// inline replaced box and its image share a node id. The display list keeps a
// shared reference to each image, so the map may be destroyed while a built list
// is still alive.
using ImageMap = std::unordered_map<dom::NodeId, std::shared_ptr<const render::Image>>;

// One consecutive slice of a text box painted with one real font. `text` is a
// view into the laid-out text box's text and stays valid only for the duration of
// the BuildDisplayList call; `font` must be non-null and is kept by shared
// reference in the emitted command. Runs of one text box are emitted in order and
// must concatenate exactly to the box text.
struct FontRun {
  std::shared_ptr<const render::Font> font;
  std::string_view text;
};

// Resolves one text box (its computed style and its exact laid-out text) to the
// real-font runs to paint. The caller (shell) supplies it; the same selection
// logic must also drive layout measurement so widths and painting agree. Return
// an empty vector when no real font applies: that box keeps the bitmap path.
// An empty (null) provider makes every text box use the bitmap path, which
// reproduces the pre-M10 command list exactly.
using FontRunsProvider = std::function<std::vector<FontRun>(const style::ComputedStyle& style,
                                                            std::string_view text_utf8)>;

// Paints a laid-out page into a display list: it walks the box tree in pre-order
// and emits one background FillRect per element box whose computed background is
// not fully transparent, then one DrawImage per inline replaced box whose node id
// is present in `images` (after that box's background, before its children), then
// one text draw per text box.
//
// Text: paint asks `font_runs` for the real-font runs of each text box. With a
// non-empty run list (real font):
// - each run becomes a DrawTextWithFont command, in order;
// - the baseline y is `round(box.y) - viewport.scroll_y + round(box.baseline_offset_px)`
//   device pixels (layout computed the line's ascent, so text boxes on one line
//   share a baseline);
// - the run x is `round(pen_x)` with pen_x starting at box.x and advanced by
//   `font->MeasureTextWidth(run.text, style.font_size_px)` CSS pixels after each
//   run, so the runs stay contiguous and match layout's measured width.
// When the provider is null/empty, or returns an empty run vector for a box, that
// box keeps the frozen M7/M8a bitmap path: one DrawText command with
// x = round(box.x), y = round(box.y) - viewport.scroll_y. A null provider must
// reproduce the pre-M10 command list exactly (bitmap byte-compatibility gate).
//
// The list is pure data: paint never rasterizes and render never learns about
// HTML or CSS. Geometry is converted from CSS pixels to device pixels with
// std::lround (the same rounding as the box dump) and every command's y is then
// translated by -viewport.scroll_y; x is unchanged. Negative coordinates pass
// through and the rasterizer clips them. An empty `images` map produces exactly
// the M7 command list for the same page. Pure and deterministic: the same inputs
// always produce the same commands, and an empty box tree yields an empty list.
//
// The list is clipped to the viewport: when it is not empty, the first command
// is PushClip(0, 0, viewport.width, viewport.height), every page command follows
// with its translated y, and the last command is PopClip. This clipping is what
// stops scrolled content from painting outside the viewport; it is frozen paint
// behavior (M7) and asserted by the paint tests.
//
// Since M11 the list is culled: a background FillRect or DrawImage is emitted
// only when its device-pixel destination rectangle intersects the viewport, and
// a text box only when its box expanded by 2 * font_size_px on every side
// intersects it (a conservative margin covering glyph overhang, anti-aliasing
// and rounding). Culling is purely an optimization: every dropped command would
// have been rasterized to no pixels, so the rendered pixels are identical to an
// unculled list and the relative order of the emitted commands is unchanged.
render::DisplayList BuildDisplayList(const dom::Document& document, const style::StyleTree& styles,
                                     const layout::BoxTree& boxes, const Viewport& viewport,
                                     const ImageMap& images, const FontRunsProvider& font_runs);

}  // namespace toy::paint
