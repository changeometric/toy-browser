#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "toy/dom/node.h"
#include "toy/style/computed_style.h"

namespace toy::layout {

// Arena-backed box tree. Boxes live in a std::vector and are addressed by
// BoxId; links are integer ids, so there are no owning pointers and no cycles.
using BoxId = std::uint32_t;

// Returned by links that point nowhere. Only an empty BoxTree has an invalid
// root.
inline constexpr BoxId kInvalidBox = 0xFFFFFFFFu;

enum class BoxKind { kBlock, kInline, kText, kAnonymousBlock };

// One box in the arena. All geometry is absolute CSS pixels, viewport origin at
// the top-left; x/y and width/height describe the BORDER box (M14b; before that
// borders did not exist, so the border box equaled the padding box). Margins live
// outside the rect; border widths and padding are inside it, and the content box
// starts at x + border-left + padding-left, y + border-top + padding-top. The
// border box of an inline fragment is its horizontal extent on the line; vertical
// inline borders and padding are ignored by layout (M5 limit).
struct Box {
  BoxKind kind = BoxKind::kBlock;
  dom::NodeId node = dom::kInvalidNode;       // source element/text; kInvalidNode for anonymous
  float x = 0, y = 0, width = 0, height = 0;  // absolute CSS px, border-box rect
  std::string text;                           // kText only: the word placed on this line
  // kText only: distance from the box top to the text baseline in CSS px. Layout
  // sets it to the line's maximum item baseline (the maximum, over the line's
  // text items, of half-leading + measured ascent; half-leading is 0 when the
  // authored line-height is `normal`), so all text boxes of one line share a
  // baseline; 0 for non-text boxes. Paint uses it for real-font text; the bitmap
  // path ignores it.
  float baseline_offset_px = 0.0f;
  BoxId parent = kInvalidBox, first_child = kInvalidBox, last_child = kInvalidBox,
        next_sibling = kInvalidBox;
};

// Metrics for one string at one computed text style. `advance_px` is the width
// of the text, `line_height_px` the height of a line containing it, and
// `ascent_px` its distance above the baseline (used for per-line baseline
// alignment; line_height_px >= ascent_px). All values are CSS pixels.
struct TextMetrics {
  float advance_px = 0.0f;
  float line_height_px = 0.0f;
  float ascent_px = 0.0f;
};

// Supplied by the caller (shell resolves the computed style to a font or to the
// built-in bitmap metrics); layout never depends on render. Must be deterministic
// and return finite non-negative values. Text is UTF-8; layout calls it for
// words, for the single space " " and for empty text "" (which must return
// advance 0 and the line height/ascent of the style's font).
using MeasureText =
    std::function<TextMetrics(std::string_view text_utf8, const style::ComputedStyle& style)>;

// Intrinsic size of a replaced element (currently an <img>) in CSS pixels, as
// reported by the caller from decoded image metadata. Layout consumes it only
// through LayoutOptions, so layout stays independent of render and of images.
struct ReplacedSize {
  float width_px = 0.0f;
  float height_px = 0.0f;
};

// Replaced-element intrinsic sizes keyed by DOM element node id. Entries with a
// non-positive side, and elements without an entry, are not replaced elements.
using ReplacedSizes = std::unordered_map<dom::NodeId, ReplacedSize>;

struct LayoutOptions {
  float viewport_width_px = 800.0f;
  // Intrinsic sizes of replaced elements, or null when the page has none. An
  // entry makes that inline element an atomic inline-level replaced box:
  // - the box size is the computed CSS width/height when specified, otherwise
  //   the intrinsic size; one specified side alone keeps the intrinsic aspect
  //   ratio. A percentage width resolves against the containing block content
  //   width; percentage heights are unsupported and behave like auto (M14);
  // - the box is unbreakable, contributes its height to the line height and is
  //   top-aligned on its line like every other inline-level box (no baseline
  //   alignment in this MVP);
  // - it has no children and its content is not laid out;
  // - its margins, padding, borders and box-sizing are ignored (the M8a and
  //   M14b replaced-element limit), like before.
  // A replaced element with display:block is not supported yet: it lays out as
  // an ordinary empty block (MVP limit, documented in the roadmap).
  const ReplacedSizes* replaced_sizes = nullptr;
};

// A laid-out page: a tree of boxes with absolute geometry.
class BoxTree {
 public:
  // The root box, or kInvalidBox for an empty tree.
  BoxId root() const;
  // Returns the box with `id`. `id` must be a valid arena index (asserted).
  const Box& Get(BoxId id) const;
  std::size_t size() const;

 private:
  friend BoxTree LayoutDocument(const dom::Document&, const style::StyleTree&, const LayoutOptions&,
                                const MeasureText&);
  std::vector<Box> boxes_;
};

// Turns a style tree into a box tree. Pure function of its inputs, never fails:
// an empty document yields an empty BoxTree. `measure` is required (asserted
// non-empty in debug).
BoxTree LayoutDocument(const dom::Document& document, const style::StyleTree& styles,
                       const LayoutOptions& options, const MeasureText& measure);

// Returns the box painted last in box-tree pre-order whose border-box rectangle
// contains the point, or kInvalidBox when no box does. Coordinates are CSS px in
// page space (absolute layout coordinates; the caller adds any scroll offset).
// Rectangles are half-open: a box contains a point when
// box.x <= x < box.x + box.width and box.y <= y < box.y + box.height; boxes with
// a non-positive width or height never contain a point. The result may be any box
// kind (including anonymous boxes without a DOM origin); callers walk up to find
// what they need. Deterministic and side-effect free.
BoxId HitTest(const BoxTree& boxes, float x, float y);

// Scrollable content height of the page: the maximum of box.y + box.height over
// all boxes, clamped to >= 0 (CSS px); 0 for an empty tree. The scrollable range
// for a viewport of height H is [0, max(0, ContentHeight(boxes) - H)].
float ContentHeight(const BoxTree& boxes);

// Deterministic text dump of the box tree, one line per box in pre-order, with
// two spaces of indent per depth (capped at 64). Used by --dump-boxes and the
// layout tests. An empty tree dumps as "".
std::string DumpBoxTree(const dom::Document& document, const BoxTree& boxes);

}  // namespace toy::layout
