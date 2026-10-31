// Block/inline layout: turns the style tree into a box tree with absolute
// geometry.
//
// The algorithm is an explicit-stack depth-first layout (no C++ recursion on
// the DOM), so deeply nested documents cannot overflow the call stack. Each
// BlockFrame lays out the children of one block container: when the container
// holds only inline content, the lines are children of the block box; when it
// also holds block-level content, every maximal run of inline content is
// wrapped in an anonymous block and the block children interleave it in flow.
//
// Inline layout is a single pass over a flattened event stream (open/close
// inline element, text, break, replaced element, block child). Line breaking and
// inline-element fragmentation use the caller-supplied measurement callback,
// which receives the computed style of the measured text (layout never depends
// on render). Every line records the maximum text baseline of its items and
// stores it in `baseline_offset_px` of each text box on the line, so text boxes
// sharing a line share one baseline; no other geometry depends on the baseline.
//
// M14: layout resolves the computed Length/AutoLength/LineHeight/TextAlign
// values. Percentages of width/min-width/max-width and of every margin/padding
// resolve against the containing block content width (CSS's basis for all four
// sides); percentage heights are unsupported and behave like auto. Horizontal
// auto margins center a block box with a definite (specified or clamped) width.
// A text item's line-height contribution is the computed px value, the unitless
// number times its font size, or the measured line height for `normal`; the
// line box is as tall as the tallest contribution and half-leading positions the
// shared baseline. Text alignment shifts the boxes of a finalized line as a
// whole (`justify` lays out as left in this MVP). A page that uses none of these
// properties keeps byte-identical geometry: `normal` contributes exactly the
// measured line height (zero half-leading) and `left` alignment shifts nothing.
//
// M14b: the box model includes borders and `box-sizing`. A block box's rect is
// the border box: margins live outside it, then border widths, padding and the
// content box inward. `box-sizing: content-box` (initial) makes a specified
// `width`/`height` the content size; `box-sizing: border-box` makes it the
// border-box size (content = max(0, size - padding - borders)). The px min/max
// constraints follow the same sizing, percentages still resolve against the
// containing content width, and auto horizontal margins distribute against the
// final border box. Inline elements include their horizontal border widths in
// the left/right advances and fragment widths (vertical inline borders are
// ignored, the M5 inline-vertical limit); replaced elements keep ignoring
// padding/margins/borders (M8a limit). Overflow clipping and decorations never
// change layout. A page without borders or `box-sizing` keeps byte-identical
// geometry: the border widths are 0 and the content-box path is the M14a path.
//
// Text nodes are split into break units by text_break.h: non-CJK text keeps the
// pre-M10 word grouping, while every CJK code point is its own unit so Chinese,
// Japanese and Korean text wraps between characters without spaces (with a
// simplified approximation of the Unicode punctuation prohibitions).
//
// A replaced element (an <img> with a caller-supplied intrinsic size, see
// LayoutOptions::replaced_sizes) is an atomic inline-level box: it is placed like
// an unbreakable word, wraps as a whole, is top-aligned on its line and raises
// the line height to at least its own height. It has no children and its
// margins/padding are not applied in this MVP.

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "text_break.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/style/computed_style.h"

namespace toy::layout {
namespace {

constexpr dom::NodeId kNoNode = dom::kInvalidNode;

// ASCII whitespace as defined by CSS: space, tab, line feed, form feed, carriage
// return. Non-ASCII bytes (including UTF-8 continuation bytes) are word bytes.
bool IsAsciiWhitespace(char character) {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
         character == '\f';
}

// Padding is clamped to >= 0; margins are used as computed (negative allowed).
float ClampLength(float value) { return value > 0.0f ? value : 0.0f; }

// Resolves one computed length against its percentage basis, in CSS px. `kPx`
// values are already absolute; `kPercent` values are resolved by layout against
// the containing block content width (see the file comment).
float ResolveLength(const style::Length& length, float basis) {
  switch (length.unit) {
    case style::Length::Unit::kPx:
      return length.value;
    case style::Length::Unit::kPercent:
      return length.value / 100.0f * basis;
  }
  return 0.0f;
}

bool IsPxLength(const style::Length& length) {
  return length.unit == style::Length::Unit::kPx;
}

// Percentage padding uses the containing block content width and is clamped to
// >= 0 (CSS disallows negative padding). A percentage `height`/`min-height`/
// `max-height` is unsupported in M14: callers treat those as auto.
float ResolvePadding(const style::Length& length, float basis) {
  return ClampLength(ResolveLength(length, basis));
}

// A margin resolves to 0 when it is `auto` at this point; block boxes
// distribute horizontal auto margins once their width is known, and vertical
// auto margins behave as 0 in this MVP.
float ResolveMargin(const style::AutoLength& margin, float basis) {
  return margin.is_auto ? 0.0f : ResolveLength(margin.length, basis);
}

// The border width that occupies layout space: 0 for a none/hidden side,
// otherwise the computed px width (clamped to >= 0 like padding).
float ResolveBorder(const style::BorderSide& side) {
  return ClampLength(style::UsedBorderWidth(side));
}

// The line-height contribution of one text item, in CSS px: the measured line
// height for `normal` (pre-M14 behavior), otherwise the computed px value or the
// unitless number times the item's own computed font size. Never negative.
float ResolveLineHeight(const style::LineHeight& line_height, const style::ComputedStyle& style,
                        float measured_line_height) {
  switch (line_height.kind) {
    case style::LineHeight::Kind::kNormal:
      return measured_line_height;
    case style::LineHeight::Kind::kNumber:
      return ClampLength(line_height.value) * ClampLength(style.font_size_px);
    case style::LineHeight::Kind::kPx:
      return ClampLength(line_height.value);
  }
  return measured_line_height;
}

// Horizontal geometry of one block box after percentage resolution: the used
// margins, borders and padding plus the final content and border-box widths,
// with the box-sizing-aware min/max-width clamp and horizontal auto-margin
// distribution applied.
struct BlockHorizontal {
  float margin_left = 0.0f;
  float margin_right = 0.0f;
  float border_left = 0.0f;
  float border_right = 0.0f;
  float padding_left = 0.0f;
  float padding_right = 0.0f;
  float content_width = 0.0f;
  float border_box_width = 0.0f;
};

BlockHorizontal ResolveBlockHorizontal(const style::ComputedStyle& style,
                                       float containing_width) {
  BlockHorizontal horizontal;
  horizontal.border_left = ResolveBorder(style.border_left);
  horizontal.border_right = ResolveBorder(style.border_right);
  horizontal.padding_left = ResolvePadding(style.padding_left, containing_width);
  horizontal.padding_right = ResolvePadding(style.padding_right, containing_width);
  const bool auto_left = style.margin_left.is_auto;
  const bool auto_right = style.margin_right.is_auto;
  horizontal.margin_left = ResolveMargin(style.margin_left, containing_width);
  horizontal.margin_right = ResolveMargin(style.margin_right, containing_width);

  const bool width_specified = style.width.has_value();
  const bool border_box_sizing = style.box_sizing == style::BoxSizing::kBorderBox;
  // The horizontal space padding and borders take on top of the content width.
  const float horizontal_extra = horizontal.padding_left + horizontal.padding_right +
                                 horizontal.border_left + horizontal.border_right;

  // Tentative widths. content-box: `width` is the content width. border-box:
  // `width` is the border-box width, so the content follows from it. An auto
  // width fills the containing content width in both modes (auto margins count
  // as 0 for now).
  float tentative_content = 0.0f;
  float tentative_border_box = 0.0f;
  if (width_specified) {
    const float resolved = ClampLength(ResolveLength(*style.width, containing_width));
    if (border_box_sizing) {
      tentative_border_box = resolved;
      tentative_content = ClampLength(resolved - horizontal_extra);
    } else {
      tentative_content = resolved;
      tentative_border_box = resolved + horizontal_extra;
    }
  } else {
    // The borders are subtracted last so that the no-border expression and its
    // float rounding are exactly the pre-M14b auto-width computation.
    tentative_content = ClampLength(containing_width - horizontal.margin_left -
                                    horizontal.margin_right - horizontal.padding_left -
                                    horizontal.padding_right - horizontal.border_left -
                                    horizontal.border_right);
    tentative_border_box = tentative_content + horizontal_extra;
  }

  // CSS 10.4: min-width then max-width (percentages still use the containing
  // content width), never negative. The constraint applies to the content box
  // for content-box and to the border box for border-box; a min wider than the
  // max leaves the max winning.
  bool constraint_applied = false;
  if (border_box_sizing) {
    float border_box = tentative_border_box;
    if (style.min_width) {
      border_box = std::max(border_box, ResolveLength(*style.min_width, containing_width));
    }
    if (style.max_width) {
      border_box = std::min(border_box, ResolveLength(*style.max_width, containing_width));
    }
    border_box = ClampLength(border_box);
    constraint_applied = border_box != tentative_border_box;
    horizontal.content_width = ClampLength(border_box - horizontal_extra);
  } else {
    float content = tentative_content;
    if (style.min_width) {
      content = std::max(content, ResolveLength(*style.min_width, containing_width));
    }
    if (style.max_width) {
      content = std::min(content, ResolveLength(*style.max_width, containing_width));
    }
    content = ClampLength(content);
    constraint_applied = content != tentative_content;
    horizontal.content_width = content;
  }
  horizontal.border_box_width =
      horizontal.content_width + horizontal.padding_left + horizontal.padding_right +
      horizontal.border_left + horizontal.border_right;

  // CSS 10.3.3: a horizontal auto margin absorbs the remaining space when the
  // width is definite (specified, or made definite by the min/max clamp). With
  // an auto width and no clamp the box fills the containing block, so auto
  // margins are 0; an over-constrained box (no space left) keeps them at 0 too.
  // The remainder is measured against the final border box; the borders are
  // subtracted last so the no-border float result matches M14a exactly.
  if ((auto_left || auto_right) && (width_specified || constraint_applied)) {
    const float remaining = containing_width - horizontal.margin_left - horizontal.margin_right -
                            horizontal.padding_left - horizontal.padding_right -
                            horizontal.border_left - horizontal.border_right -
                            horizontal.content_width;
    if (remaining > 0.0f) {
      if (auto_left && auto_right) {
        // Both auto: share the remainder equally; the left one gets the extra
        // fraction on an odd remainder (float arithmetic, no rounding).
        horizontal.margin_left = remaining / 2.0f;
        horizontal.margin_right = remaining - horizontal.margin_left;
      } else if (auto_left) {
        horizontal.margin_left = remaining;
      } else {
        horizontal.margin_right = remaining;
      }
    }
  }
  return horizontal;
}

// Content box of a containing block. Margins and padding are resolved against
// the content width.
struct ContainingBlock {
  float content_x = 0;
  float content_y = 0;
  float content_width = 0;
};

// One inline element that is currently open during inline layout. `open`
// survives block boundaries (rule 10): fragments close at a boundary, the
// element itself stays open for its remaining content. The advances are already
// resolved to CSS px against the containing block content width.
struct OpenInline {
  dom::NodeId node = kNoNode;
  float border_left = 0;
  float border_right = 0;
  float padding_left = 0;
  float padding_right = 0;
  float left_advance = 0;   // margin-left + border-left + padding-left, before content
  float right_advance = 0;  // padding-right + border-right + margin-right, after content
  bool first_fragment_pending = true;
  bool has_fragment = false;
  BoxId current_fragment = kInvalidBox;
  float current_border_x = 0;
  BoxId last_fragment = kInvalidBox;
};

// A replaced box and its own (fixed) height on the current line. Every other
// line box gets the shared line height when the line is finalized; a replaced
// box keeps its replaced height and is only top-aligned.
struct ReplacedLineItem {
  BoxId box = kInvalidBox;
  float height = 0;
};

// Inline formatting state of one block container.
struct InlineState {
  BoxId container = kInvalidBox;
  float origin_x = 0;
  float origin_y = 0;
  float line_width = 0;
  float cursor_x = 0;
  float cursor_y = 0;
  float line_height = 0;
  // Maximum text baseline of the text items placed on the current line, in CSS
  // px (half-leading included). It becomes the baseline_offset_px of every text
  // box on the line when the line is finalized, so all text boxes of one line
  // share a baseline.
  float line_baseline = 0;
  // Text alignment of the block container that owns this line (inherited
  // property; `justify` lays out as left in this MVP).
  style::TextAlign text_align = style::TextAlign::kLeft;
  bool line_has_items = false;
  bool pending_space = false;
  float pending_space_advance = 0;
  std::vector<BoxId> line_boxes;
  std::vector<ReplacedLineItem> line_replaced;
  std::vector<OpenInline> open;
};

enum class StreamKind { kOpenInline, kCloseInline, kText, kBreak, kReplaced, kBlock };

// One child in the flattened child stream of a block container. Block children
// are not descended into; their own frame flattens them.
struct StreamItem {
  StreamKind kind = StreamKind::kText;
  dom::NodeId node = kNoNode;
};

// Layout state of one block container, including its persistent inline state.
struct BlockFrame {
  BoxId box = kInvalidBox;
  dom::NodeId element = kNoNode;
  float content_x = 0;
  float content_y = 0;
  float content_width = 0;
  float border_top = 0;
  float border_bottom = 0;
  float padding_top = 0;
  float padding_bottom = 0;
  bool border_box_sizing = false;
  bool has_fixed_height = false;
  float fixed_height = 0;
  // px-only min/max-height constraints (percentage constraints are unsupported
  // in M14 and behave as `none`).
  bool has_min_height = false;
  float min_height = 0;
  bool has_max_height = false;
  float max_height = 0;
  bool mixed = false;
  std::vector<StreamItem> stream;
  std::size_t next = 0;
  InlineState inline_state;
  float cursor = 0;
  bool in_run = false;
  BoxId current_anonymous = kInvalidBox;
  bool waiting_child = false;
  float child_margin_top = 0;
  float child_margin_bottom = 0;
};

class LayoutEngine {
 public:
  LayoutEngine(const dom::Document& document, const style::StyleTree& styles,
               const MeasureText& measure, std::vector<Box>& boxes)
      : document_(document), styles_(styles), measure_(measure), boxes_(boxes) {}

  void Run(const LayoutOptions& options);

 private:
  BoxId AddBox(BoxKind kind, dom::NodeId node);
  void AppendChild(BoxId parent, BoxId child);

  // Creates the block box of `element` in flow position `flow_y` (the parent's
  // content y plus the flow cursor) and appends it to `parent`.
  BoxId CreateBlockBox(dom::NodeId element, const ContainingBlock& containing, float flow_y,
                       BoxId parent);
  // `containing_width` is the containing block content width: it is the basis of
  // this block's percentage padding (all four sides).
  BlockFrame MakeFrame(BoxId box, dom::NodeId element, float containing_width);
  std::vector<StreamItem> FlattenChildren(dom::NodeId element) const;
  bool RunProducesBoxes(const std::vector<StreamItem>& stream, std::size_t begin) const;
  // Returns the intrinsic size of `node` when it is a replaced element (inline
  // display plus a valid entry), or null when it lays out as an ordinary box.
  const ReplacedSize* FindReplacedSize(dom::NodeId node) const;
  // The border-box size of a replaced element: CSS width/height when specified,
  // otherwise the intrinsic size; one specified side keeps the intrinsic ratio.
  // A percentage width resolves against `containing_width`; a percentage height
  // is unsupported and behaves like auto.
  ReplacedSize ComputeReplacedSize(dom::NodeId node, const ReplacedSize& intrinsic,
                                   float containing_width) const;

  void StartRun(BlockFrame& frame, std::size_t first_item);
  void FinishRun(BlockFrame& frame);
  void ProcessStreamItem(InlineState& state, const StreamItem& item);

  void BeginSession(InlineState& state, BoxId container, float origin_x, float origin_y,
                    float line_width, style::TextAlign text_align);
  float FinishSession(InlineState& state);
  void FinalizeLine(InlineState& state);
  void MarkSeparator(InlineState& state, const style::ComputedStyle& style);
  // Places one atomic inline-level item of `advance` width: wraps when it does
  // not fit, consumes a pending separator and materializes one fragment per open
  // inline element, outermost first. Returns the box the item is appended to.
  BoxId PlaceInlineItem(InlineState& state, float advance);
  void PlaceWord(InlineState& state, dom::NodeId text_node, const std::string& word,
                 const style::ComputedStyle& style);
  void PlaceReplaced(InlineState& state, dom::NodeId element, const ReplacedSize& intrinsic);
  void ProcessText(InlineState& state, dom::NodeId text_node);
  void ProcessBreak(InlineState& state, dom::NodeId br);
  void ProcessOpen(InlineState& state, dom::NodeId element);
  void ProcessClose(InlineState& state, dom::NodeId element);

  const dom::Document& document_;
  const style::StyleTree& styles_;
  const MeasureText& measure_;
  std::vector<Box>& boxes_;
  const ReplacedSizes* replaced_sizes_ = nullptr;
};

BoxId LayoutEngine::AddBox(BoxKind kind, dom::NodeId node) {
  const BoxId id = static_cast<BoxId>(boxes_.size());
  assert(boxes_.size() < static_cast<std::size_t>(kInvalidBox));
  boxes_.push_back(Box{});
  boxes_.back().kind = kind;
  boxes_.back().node = node;
  return id;
}

void LayoutEngine::AppendChild(BoxId parent, BoxId child) {
  Box& child_box = boxes_[child];
  child_box.parent = parent;
  Box& parent_box = boxes_[parent];
  if (parent_box.last_child == kInvalidBox) {
    parent_box.first_child = child;
  } else {
    boxes_[parent_box.last_child].next_sibling = child;
  }
  parent_box.last_child = child;
}

BoxId LayoutEngine::CreateBlockBox(dom::NodeId element, const ContainingBlock& containing,
                                   float flow_y, BoxId parent) {
  const style::ComputedStyle& style = styles_.Get(element);
  const BlockHorizontal horizontal = ResolveBlockHorizontal(style, containing.content_width);
  const float margin_top = ResolveMargin(style.margin_top, containing.content_width);
  const BoxId id = AddBox(BoxKind::kBlock, element);
  Box& box = boxes_[id];
  box.x = containing.content_x + horizontal.margin_left;
  box.y = flow_y + margin_top;
  box.width = horizontal.content_width + horizontal.padding_left + horizontal.padding_right +
              horizontal.border_left + horizontal.border_right;
  box.height = 0.0f;  // Finalized after the children are laid out.
  if (parent != kInvalidBox) {
    AppendChild(parent, id);
  }
  return id;
}

std::vector<StreamItem> LayoutEngine::FlattenChildren(dom::NodeId element) const {
  struct PendingEntry {
    dom::NodeId node = kNoNode;
    bool close = false;
  };
  std::vector<StreamItem> stream;
  std::vector<PendingEntry> pending;
  std::vector<dom::NodeId> children;
  for (dom::NodeId child = document_.Get(element).first_child; child != kNoNode;
       child = document_.Get(child).next_sibling) {
    children.push_back(child);
  }
  for (std::size_t index = children.size(); index-- > 0;) {
    pending.push_back(PendingEntry{children[index], false});
  }
  while (!pending.empty()) {
    const PendingEntry entry = pending.back();
    pending.pop_back();
    if (entry.close) {
      stream.push_back(StreamItem{StreamKind::kCloseInline, entry.node});
      continue;
    }
    const dom::Node& node = document_.Get(entry.node);
    switch (node.type) {
      case dom::NodeType::kElement: {
        const style::ComputedStyle& style = styles_.Get(entry.node);
        if (style.display == style::Display::kNone) {
          break;  // The whole subtree produces nothing.
        }
        // A <br> produces no box and always forces a line break, even if an
        // author rule gives it another display value: rule 9 wins over rule 2
        // (only display:none above hides it).
        if (node.name == "br") {
          stream.push_back(StreamItem{StreamKind::kBreak, entry.node});
          break;
        }
        if (style.display == style::Display::kBlock) {
          // Laid out by its own frame; do not descend here. A replaced element
          // with display:block is not supported and falls through to this path.
          stream.push_back(StreamItem{StreamKind::kBlock, entry.node});
          break;
        }
        // An inline element with a caller-supplied intrinsic size is an atomic
        // replaced box: it produces no children and its content is not laid out.
        if (FindReplacedSize(entry.node) != nullptr) {
          stream.push_back(StreamItem{StreamKind::kReplaced, entry.node});
          break;
        }
        stream.push_back(StreamItem{StreamKind::kOpenInline, entry.node});
        pending.push_back(PendingEntry{entry.node, true});
        children.clear();
        for (dom::NodeId child = node.first_child; child != kNoNode;
             child = document_.Get(child).next_sibling) {
          children.push_back(child);
        }
        for (std::size_t index = children.size(); index-- > 0;) {
          pending.push_back(PendingEntry{children[index], false});
        }
        break;
      }
      case dom::NodeType::kText:
        if (!node.data.empty()) {
          stream.push_back(StreamItem{StreamKind::kText, entry.node});
        }
        break;
      case dom::NodeType::kComment:
      case dom::NodeType::kDoctype:
      case dom::NodeType::kDocument:
        break;
    }
  }
  return stream;
}

const ReplacedSize* LayoutEngine::FindReplacedSize(dom::NodeId node) const {
  if (replaced_sizes_ == nullptr) {
    return nullptr;
  }
  const auto entry = replaced_sizes_->find(node);
  if (entry == replaced_sizes_->end()) {
    return nullptr;
  }
  // Non-positive intrinsic sides mean "not a replaced element" (NaN fails the
  // comparison too, so a malformed size cannot reach geometry).
  if (!(entry->second.width_px > 0.0f) || !(entry->second.height_px > 0.0f)) {
    return nullptr;
  }
  return &entry->second;
}

ReplacedSize LayoutEngine::ComputeReplacedSize(dom::NodeId node, const ReplacedSize& intrinsic,
                                               float containing_width) const {
  const style::ComputedStyle& style = styles_.Get(node);
  // A percentage height is unsupported in M14: treat it as auto. A percentage
  // width resolves against the containing block content width.
  const bool has_width = style.width.has_value();
  const bool has_height = style.height && IsPxLength(*style.height);
  if (has_width && has_height) {
    return ReplacedSize{ClampLength(ResolveLength(*style.width, containing_width)),
                        ClampLength(style.height->value)};
  }
  if (has_width) {
    // One specified side keeps the intrinsic aspect ratio.
    const float width = ClampLength(ResolveLength(*style.width, containing_width));
    return ReplacedSize{width, width * intrinsic.height_px / intrinsic.width_px};
  }
  if (has_height) {
    const float height = ClampLength(style.height->value);
    return ReplacedSize{height * intrinsic.width_px / intrinsic.height_px, height};
  }
  return intrinsic;
}

bool LayoutEngine::RunProducesBoxes(const std::vector<StreamItem>& stream,
                                    std::size_t begin) const {
  for (std::size_t index = begin; index < stream.size(); ++index) {
    const StreamItem& item = stream[index];
    switch (item.kind) {
      case StreamKind::kBlock:
        return false;
      case StreamKind::kBreak:
        return true;
      case StreamKind::kReplaced:
        return true;
      case StreamKind::kText:
        for (const char character : document_.Get(item.node).data) {
          if (!IsAsciiWhitespace(character)) {
            return true;
          }
        }
        break;
      case StreamKind::kOpenInline:
      case StreamKind::kCloseInline:
        break;
    }
  }
  return false;
}

BlockFrame LayoutEngine::MakeFrame(BoxId box, dom::NodeId element, float containing_width) {
  const style::ComputedStyle& style = styles_.Get(element);
  const float border_left = ResolveBorder(style.border_left);
  const float border_right = ResolveBorder(style.border_right);
  const float padding_left = ResolvePadding(style.padding_left, containing_width);
  const float padding_right = ResolvePadding(style.padding_right, containing_width);
  BlockFrame frame;
  frame.box = box;
  frame.element = element;
  frame.border_top = ResolveBorder(style.border_top);
  frame.border_bottom = ResolveBorder(style.border_bottom);
  frame.border_box_sizing = style.box_sizing == style::BoxSizing::kBorderBox;
  frame.padding_top = ResolvePadding(style.padding_top, containing_width);
  frame.padding_bottom = ResolvePadding(style.padding_bottom, containing_width);
  // Percentage heights and min/max-heights are unsupported in M14: only px
  // values participate (percentages behave like auto/no constraint).
  frame.has_fixed_height = style.height && IsPxLength(*style.height);
  frame.fixed_height = frame.has_fixed_height ? ClampLength(style.height->value) : 0.0f;
  frame.has_min_height = style.min_height && IsPxLength(*style.min_height);
  frame.min_height = frame.has_min_height ? ClampLength(style.min_height->value) : 0.0f;
  frame.has_max_height = style.max_height && IsPxLength(*style.max_height);
  frame.max_height = frame.has_max_height ? ClampLength(style.max_height->value) : 0.0f;
  const Box& box_ref = boxes_[box];
  frame.content_x = box_ref.x + border_left + padding_left;
  frame.content_y = box_ref.y + frame.border_top + frame.padding_top;
  frame.content_width = box_ref.width - padding_left - padding_right - border_left - border_right;
  frame.stream = FlattenChildren(element);
  for (const StreamItem& item : frame.stream) {
    if (item.kind == StreamKind::kBlock) {
      frame.mixed = true;
      break;
    }
  }
  if (!frame.mixed) {
    StartRun(frame, 0);
  }
  return frame;
}

void LayoutEngine::BeginSession(InlineState& state, BoxId container, float origin_x, float origin_y,
                                float line_width, style::TextAlign text_align) {
  // `open` is deliberately kept: an inline element may continue across a block
  // child (rule 10).
  state.container = container;
  state.origin_x = origin_x;
  state.origin_y = origin_y;
  state.line_width = line_width;
  state.cursor_x = origin_x;
  state.cursor_y = 0.0f;
  state.line_height = 0.0f;
  state.line_baseline = 0.0f;
  state.text_align = text_align;
  state.line_has_items = false;
  state.pending_space = false;
  state.pending_space_advance = 0.0f;
  state.line_boxes.clear();
  state.line_replaced.clear();
}

float LayoutEngine::FinishSession(InlineState& state) {
  if (state.line_has_items) {
    FinalizeLine(state);
  }
  // A trailing line with no items is dropped (rule 9).
  state.pending_space = false;
  return state.cursor_y;
}

void LayoutEngine::FinalizeLine(InlineState& state) {
  const float line_y = state.origin_y + state.cursor_y;
  // Still-open inline elements end their current fragment at the line end; the
  // padding-right of a last fragment is added when the element actually closes.
  for (OpenInline& open : state.open) {
    if (open.current_fragment == kInvalidBox) {
      continue;
    }
    boxes_[open.current_fragment].width = state.cursor_x - open.current_border_x;
    open.last_fragment = open.current_fragment;
    open.current_fragment = kInvalidBox;
  }
  // text-align: after line breaking (and after the inline margins/padding already
  // added to the cursor), shift every box of the line as a whole. `justify` lays
  // out as left this round; an overlong line (used width > line width) stays
  // left-aligned instead of getting a negative offset.
  const float used_width = state.cursor_x - state.origin_x;
  float align_offset = 0.0f;
  if (state.text_align == style::TextAlign::kRight) {
    align_offset = state.line_width - used_width;
  } else if (state.text_align == style::TextAlign::kCenter) {
    align_offset = (state.line_width - used_width) / 2.0f;
  }
  if (!(align_offset > 0.0f)) {
    align_offset = 0.0f;
  }
  for (const BoxId id : state.line_boxes) {
    Box& box = boxes_[id];
    box.x += align_offset;
    box.y = line_y;
    box.height = state.line_height;
    // Every text box of the line shares the line's maximum item baseline as its
    // baseline; other box kinds keep the default baseline_offset_px of 0.
    if (box.kind == BoxKind::kText) {
      box.baseline_offset_px = state.line_baseline;
    }
  }
  // A replaced box is top-aligned and keeps its own height even when the line is
  // taller because of another item.
  for (const ReplacedLineItem& item : state.line_replaced) {
    boxes_[item.box].height = item.height;
  }
  state.cursor_y += state.line_height;
  state.line_boxes.clear();
  state.line_replaced.clear();
  state.cursor_x = state.origin_x;
  state.line_has_items = false;
  state.line_height = 0.0f;
  state.line_baseline = 0.0f;
  state.pending_space = false;
  state.pending_space_advance = 0.0f;
}

void LayoutEngine::MarkSeparator(InlineState& state, const style::ComputedStyle& style) {
  // Separators collapse: none at the start of a line, never two in a row.
  if (!state.line_has_items || state.pending_space) {
    return;
  }
  const TextMetrics metrics = measure_(std::string_view{" "}, style);
  state.pending_space = true;
  state.pending_space_advance = metrics.advance_px;
}

BoxId LayoutEngine::PlaceInlineItem(InlineState& state, float advance) {
  const float pending_advance = state.pending_space ? state.pending_space_advance : 0.0f;
  float left_advance = 0.0f;
  for (const OpenInline& open : state.open) {
    if (open.first_fragment_pending) {
      left_advance += open.left_advance;
    }
  }
  // An item never spans a line: an item that does not fit wraps as a whole, and
  // an overlong first item overflows (rule 5).
  if (state.line_has_items && state.cursor_x + pending_advance + left_advance + advance >
                                  state.origin_x + state.line_width) {
    FinalizeLine(state);  // The pending separator is dropped with the line.
  }
  if (state.pending_space && state.line_has_items) {
    state.cursor_x += state.pending_space_advance;
  }
  state.pending_space = false;
  state.pending_space_advance = 0.0f;

  // Materialize one fragment per open inline element, outermost first, so that
  // an element split across lines yields sibling fragments.
  BoxId parent = state.container;
  for (OpenInline& open : state.open) {
    const bool first_fragment = open.first_fragment_pending;
    if (first_fragment) {
      state.cursor_x += open.left_advance;
      open.first_fragment_pending = false;
    }
    if (open.current_fragment == kInvalidBox) {
      const BoxId fragment = AddBox(BoxKind::kInline, open.node);
      Box& fragment_box = boxes_[fragment];
      // The fragment rect is the border box: on the first fragment it starts a
      // margin before the content (left border and padding are inside it), on a
      // continuation fragment it starts at the current cursor.
      fragment_box.x = first_fragment
                           ? state.cursor_x - open.padding_left - open.border_left
                           : state.cursor_x;
      AppendChild(parent, fragment);
      open.current_fragment = fragment;
      open.current_border_x = fragment_box.x;
      open.has_fragment = true;
      state.line_boxes.push_back(fragment);
    }
    parent = open.current_fragment;
  }
  return parent;
}

void LayoutEngine::PlaceWord(InlineState& state, dom::NodeId text_node, const std::string& word,
                             const style::ComputedStyle& style) {
  const TextMetrics metrics = measure_(word, style);
  const BoxId parent = PlaceInlineItem(state, metrics.advance_px);
  const float word_x = state.cursor_x;
  state.cursor_x += metrics.advance_px;
  state.line_has_items = true;
  // The line box is as tall as the tallest line-height contribution. Half-leading
  // centers the measured font box inside that line box: `normal` contributes
  // exactly the measured line height, so the half-leading is exactly 0 and the
  // pre-M14 geometry is preserved; the text baseline is the half-leading plus the
  // measured ascent.
  const float contribution = ResolveLineHeight(style.line_height, style, metrics.line_height_px);
  const float half_leading = (contribution - metrics.line_height_px) / 2.0f;
  state.line_height = std::max(state.line_height, contribution);
  state.line_baseline = std::max(state.line_baseline, half_leading + metrics.ascent_px);

  const BoxId text_box = AddBox(BoxKind::kText, text_node);
  Box& text_box_ref = boxes_[text_box];
  text_box_ref.x = word_x;
  text_box_ref.width = metrics.advance_px;
  text_box_ref.text = word;
  AppendChild(parent, text_box);
  state.line_boxes.push_back(text_box);
}

void LayoutEngine::PlaceReplaced(InlineState& state, dom::NodeId element,
                                 const ReplacedSize& intrinsic) {
  const ReplacedSize size = ComputeReplacedSize(element, intrinsic, state.line_width);
  const BoxId parent = PlaceInlineItem(state, size.width_px);
  const BoxId box_id = AddBox(BoxKind::kInline, element);
  Box& box = boxes_[box_id];
  box.x = state.cursor_x;
  box.width = size.width_px;
  box.height = size.height_px;
  AppendChild(parent, box_id);
  state.cursor_x += size.width_px;
  state.line_has_items = true;
  state.line_height = std::max(state.line_height, size.height_px);
  state.line_boxes.push_back(box_id);
  state.line_replaced.push_back(ReplacedLineItem{box_id, size.height_px});
}

void LayoutEngine::ProcessText(InlineState& state, dom::NodeId text_node) {
  const std::string& data = document_.Get(text_node).data;
  // Text nodes report the style of their nearest element ancestor, so the
  // measurer sees the computed style that paints this text.
  const style::ComputedStyle& style = styles_.Get(text_node);
  // Every break unit becomes an ordinary word-level text box; U+3000 and ASCII
  // whitespace runs collapse into the pending separator. CJK text therefore
  // wraps between characters without inserting spaces, while Latin text keeps
  // its pre-M10 word grouping.
  for (const detail::TextPiece& piece : detail::SplitBreakUnits(data)) {
    if (piece.kind == detail::TextPieceKind::kSeparator) {
      MarkSeparator(state, style);
    } else {
      PlaceWord(state, text_node, piece.text, style);
    }
  }
}

void LayoutEngine::ProcessBreak(InlineState& state, dom::NodeId br) {
  if (state.line_has_items) {
    FinalizeLine(state);
    return;
  }
  const style::ComputedStyle& br_style = styles_.Get(br);
  // An empty line forced by <br> is as tall as the br style's line-height
  // contribution when the author specified one (px or number); otherwise it
  // keeps the pre-M14 behavior: the empty-text metrics (advance 0, line height
  // and ascent) of the br's own computed style.
  float line_height = 0.0f;
  if (br_style.line_height.kind == style::LineHeight::Kind::kNormal) {
    const TextMetrics metrics = measure_(std::string_view{}, br_style);
    line_height = metrics.line_height_px;
  } else {
    line_height = ResolveLineHeight(br_style.line_height, br_style, 0.0f);
  }
  state.cursor_y += line_height;
  state.cursor_x = state.origin_x;
  state.pending_space = false;
  state.pending_space_advance = 0.0f;
}

void LayoutEngine::ProcessOpen(InlineState& state, dom::NodeId element) {
  const style::ComputedStyle& style = styles_.Get(element);
  // Percentage inline margins/padding use the containing block content width,
  // which is the line width of this inline formatting context. Horizontal
  // borders add to the advances exactly like horizontal padding (vertical inline
  // borders are ignored, the M5 inline-vertical limit).
  const float basis = state.line_width;
  OpenInline open;
  open.node = element;
  open.border_left = ResolveBorder(style.border_left);
  open.border_right = ResolveBorder(style.border_right);
  open.padding_left = ResolvePadding(style.padding_left, basis);
  open.padding_right = ResolvePadding(style.padding_right, basis);
  open.left_advance =
      ResolveMargin(style.margin_left, basis) + open.padding_left + open.border_left;
  open.right_advance =
      open.padding_right + open.border_right + ResolveMargin(style.margin_right, basis);
  state.open.push_back(open);
}

void LayoutEngine::ProcessClose(InlineState& state, dom::NodeId element) {
  (void)element;
  assert(!state.open.empty());
  assert(state.open.back().node == element);
  if (state.open.empty()) {
    return;
  }
  const OpenInline open = state.open.back();
  state.open.pop_back();
  if (open.current_fragment != kInvalidBox) {
    // The fragment ends on the current line and its border box includes the
    // element's right padding and border.
    boxes_[open.current_fragment].width = (state.cursor_x - open.current_border_x) +
                                          open.padding_right + open.border_right;
    state.cursor_x += open.right_advance;
  } else if (open.has_fragment) {
    // The last fragment ended at a line break; its border box still includes
    // padding-right and border-right. margin-right would sit after a flushed line
    // and is dropped.
    boxes_[open.last_fragment].width += open.padding_right + open.border_right;
  }
}

void LayoutEngine::ProcessStreamItem(InlineState& state, const StreamItem& item) {
  switch (item.kind) {
    case StreamKind::kOpenInline:
      ProcessOpen(state, item.node);
      break;
    case StreamKind::kCloseInline:
      ProcessClose(state, item.node);
      break;
    case StreamKind::kText:
      ProcessText(state, item.node);
      break;
    case StreamKind::kBreak:
      ProcessBreak(state, item.node);
      break;
    case StreamKind::kReplaced: {
      const ReplacedSize* intrinsic = FindReplacedSize(item.node);
      assert(intrinsic != nullptr);  // Flattening only emits replaced items for entries.
      if (intrinsic != nullptr) {
        PlaceReplaced(state, item.node, *intrinsic);
      }
      break;
    }
    case StreamKind::kBlock:
      break;  // Handled by the frame loop.
  }
}

void LayoutEngine::StartRun(BlockFrame& frame, std::size_t first_item) {
  BoxId container = frame.box;
  if (frame.mixed && RunProducesBoxes(frame.stream, first_item)) {
    const BoxId anonymous = AddBox(BoxKind::kAnonymousBlock, kNoNode);
    Box& anonymous_box = boxes_[anonymous];
    anonymous_box.x = frame.content_x;
    anonymous_box.y = frame.content_y + frame.cursor;
    anonymous_box.width = frame.content_width;
    anonymous_box.height = 0.0f;
    AppendChild(frame.box, anonymous);
    frame.current_anonymous = anonymous;
    container = anonymous;
  }
  frame.in_run = true;
  const style::TextAlign text_align = styles_.Get(frame.element).text_align;
  BeginSession(frame.inline_state, container, frame.content_x, frame.content_y + frame.cursor,
               frame.content_width, text_align);
}

void LayoutEngine::FinishRun(BlockFrame& frame) {
  if (!frame.in_run) {
    return;
  }
  const float height = FinishSession(frame.inline_state);
  if (frame.current_anonymous != kInvalidBox) {
    boxes_[frame.current_anonymous].height = height;
    frame.cursor += height;
    frame.current_anonymous = kInvalidBox;
  }
  frame.in_run = false;
}

void LayoutEngine::Run(const LayoutOptions& options) {
  replaced_sizes_ = options.replaced_sizes;
  const dom::NodeId document_root = document_.root();
  if (document_root == kNoNode) {
    return;
  }
  dom::NodeId html = kNoNode;
  for (dom::NodeId child = document_.Get(document_root).first_child; child != kNoNode;
       child = document_.Get(child).next_sibling) {
    const dom::Node& node = document_.Get(child);
    if (node.type == dom::NodeType::kElement && node.name == "html") {
      html = child;
      break;
    }
  }
  if (html == kNoNode || styles_.Get(html).display == style::Display::kNone) {
    return;
  }

  ContainingBlock viewport;
  viewport.content_x = 0.0f;
  viewport.content_y = 0.0f;
  viewport.content_width = options.viewport_width_px;
  const BoxId root_box = CreateBlockBox(html, viewport, 0.0f, kInvalidBox);

  std::vector<BlockFrame> stack;
  stack.push_back(MakeFrame(root_box, html, viewport.content_width));
  while (!stack.empty()) {
    BlockFrame& frame = stack.back();
    if (frame.next < frame.stream.size()) {
      const StreamItem item = frame.stream[frame.next++];
      if (item.kind == StreamKind::kBlock) {
        FinishRun(frame);
        const style::ComputedStyle& child_style = styles_.Get(item.node);
        ContainingBlock containing;
        containing.content_x = frame.content_x;
        containing.content_y = frame.content_y;
        containing.content_width = frame.content_width;
        const BoxId child =
            CreateBlockBox(item.node, containing, frame.content_y + frame.cursor, frame.box);
        frame.waiting_child = true;
        // Percentage child margins use the containing block content width (the
        // parent's content width), like every other box property.
        frame.child_margin_top = ResolveMargin(child_style.margin_top, frame.content_width);
        frame.child_margin_bottom =
            ResolveMargin(child_style.margin_bottom, frame.content_width);
        const float child_containing_width = frame.content_width;
        stack.push_back(MakeFrame(child, item.node, child_containing_width));
        continue;
      }
      if (!frame.in_run) {
        StartRun(frame, frame.next - 1);
      }
      ProcessStreamItem(frame.inline_state, item);
      continue;
    }

    // All children are laid out: finalize the block height and resume the
    // parent's flow cursor. In content-box the px height is the content height;
    // in border-box it is the border-box height (the content follows from it).
    // The px min/max-height constraints follow the same box-sizing rule and are
    // clamped min first, then max, never negative. Percentage height constraints
    // are unsupported in M14 (they behave as auto/no constraint).
    float content_height = 0.0f;
    if (frame.mixed) {
      FinishRun(frame);
      content_height = frame.cursor;
    } else {
      content_height = FinishSession(frame.inline_state);
    }
    if (frame.border_box_sizing) {
      const float vertical_extra = frame.padding_top + frame.padding_bottom +
                                   frame.border_top + frame.border_bottom;
      float border_height =
          frame.has_fixed_height ? frame.fixed_height : content_height + vertical_extra;
      if (frame.has_min_height) {
        border_height = std::max(border_height, frame.min_height);
      }
      if (frame.has_max_height) {
        border_height = std::min(border_height, frame.max_height);
      }
      border_height = ClampLength(border_height);
      content_height = ClampLength(border_height - vertical_extra);
    } else {
      if (frame.has_fixed_height) {
        content_height = frame.fixed_height;
      }
      if (frame.has_min_height) {
        content_height = std::max(content_height, frame.min_height);
      }
      if (frame.has_max_height) {
        content_height = std::min(content_height, frame.max_height);
      }
      content_height = ClampLength(content_height);
    }
    const BoxId finished_box = frame.box;
    const float padding_top = frame.padding_top;
    const float padding_bottom = frame.padding_bottom;
    const float border_top = frame.border_top;
    const float border_bottom = frame.border_bottom;
    stack.pop_back();
    boxes_[finished_box].height = content_height + padding_top + padding_bottom + border_top +
                                  border_bottom;
    if (!stack.empty()) {
      BlockFrame& parent = stack.back();
      if (parent.waiting_child) {
        parent.cursor +=
            parent.child_margin_top + boxes_[finished_box].height + parent.child_margin_bottom;
        parent.waiting_child = false;
      }
    }
  }
}

}  // namespace

BoxTree LayoutDocument(const dom::Document& document, const style::StyleTree& styles,
                       const LayoutOptions& options, const MeasureText& measure) {
  assert(measure);
  BoxTree tree;
  LayoutEngine engine(document, styles, measure, tree.boxes_);
  engine.Run(options);
  return tree;
}

}  // namespace toy::layout
