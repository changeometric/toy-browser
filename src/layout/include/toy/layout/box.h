#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
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
// the top-left; x/y is the border-box top-left (borders do not exist in M4, so
// border box = padding box), margins live outside the rect, padding inside.
struct Box {
  BoxKind kind = BoxKind::kBlock;
  dom::NodeId node = dom::kInvalidNode;       // source element/text; kInvalidNode for anonymous
  float x = 0, y = 0, width = 0, height = 0;  // absolute CSS px, border-box rect
  std::string text;                           // kText only: the word placed on this line
  BoxId parent = kInvalidBox, first_child = kInvalidBox, last_child = kInvalidBox,
        next_sibling = kInvalidBox;
};

// Metrics for one string at one font size. `advance_px` is the width of the
// text, `line_height_px` the height of a line containing it.
struct TextMetrics {
  float advance_px = 0.0f;
  float line_height_px = 0.0f;
};

// Supplied by the caller (shell adapts the font); layout never depends on
// render. Must be deterministic and return finite non-negative values. Text is
// UTF-8; layout calls it for words, for the single space " " and for empty text
// "" (which must return advance 0 and the line height).
using MeasureText = std::function<TextMetrics(std::string_view text_utf8, float font_size_px)>;

struct LayoutOptions {
  float viewport_width_px = 800.0f;
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

// Deterministic text dump of the box tree, one line per box in pre-order, with
// two spaces of indent per depth (capped at 64). Used by --dump-boxes and the
// layout tests. An empty tree dumps as "".
std::string DumpBoxTree(const dom::Document& document, const BoxTree& boxes);

}  // namespace toy::layout
