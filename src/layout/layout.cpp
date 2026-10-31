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
// inline-element fragmentation use the caller-supplied measurement callback;
// layout never depends on render.
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

float NonNegativeFontSize(const style::ComputedStyle& style) {
  return style.font_size_px > 0.0f ? style.font_size_px : 0.0f;
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
// element itself stays open for its remaining content.
struct OpenInline {
  dom::NodeId node = kNoNode;
  float padding_left = 0;
  float padding_right = 0;
  float left_advance = 0;   // margin-left + padding-left, before first content
  float right_advance = 0;  // padding-right + margin-right, after last content
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
  float padding_top = 0;
  float padding_bottom = 0;
  bool has_fixed_height = false;
  float fixed_height = 0;
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
  BlockFrame MakeFrame(BoxId box, dom::NodeId element);
  std::vector<StreamItem> FlattenChildren(dom::NodeId element) const;
  bool RunProducesBoxes(const std::vector<StreamItem>& stream, std::size_t begin) const;
  // Returns the intrinsic size of `node` when it is a replaced element (inline
  // display plus a valid entry), or null when it lays out as an ordinary box.
  const ReplacedSize* FindReplacedSize(dom::NodeId node) const;
  // The border-box size of a replaced element: CSS width/height when specified,
  // otherwise the intrinsic size; one specified side keeps the intrinsic ratio.
  ReplacedSize ComputeReplacedSize(dom::NodeId node, const ReplacedSize& intrinsic) const;

  void StartRun(BlockFrame& frame, std::size_t first_item);
  void FinishRun(BlockFrame& frame);
  void ProcessStreamItem(InlineState& state, const StreamItem& item);

  void BeginSession(InlineState& state, BoxId container, float origin_x, float origin_y,
                    float line_width);
  float FinishSession(InlineState& state);
  void FinalizeLine(InlineState& state);
  void MarkSeparator(InlineState& state, float font_size);
  // Places one atomic inline-level item of `advance` width: wraps when it does
  // not fit, consumes a pending separator and materializes one fragment per open
  // inline element, outermost first. Returns the box the item is appended to.
  BoxId PlaceInlineItem(InlineState& state, float advance);
  void PlaceWord(InlineState& state, dom::NodeId text_node, const std::string& word,
                 float font_size);
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
  const float padding_left = ClampLength(style.padding_left);
  const float padding_right = ClampLength(style.padding_right);
  const float margin_left = style.margin_left;
  const float margin_right = style.margin_right;
  // Width: the specified content width, or the containing content width minus
  // horizontal margins and padding (both already clamped to >= 0 where needed).
  const float content_width = style.width
                                  ? ClampLength(*style.width)
                                  : ClampLength(containing.content_width - margin_left -
                                                margin_right - padding_left - padding_right);
  const BoxId id = AddBox(BoxKind::kBlock, element);
  Box& box = boxes_[id];
  box.x = containing.content_x + margin_left;
  box.y = flow_y + style.margin_top;
  box.width = content_width + padding_left + padding_right;
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

ReplacedSize LayoutEngine::ComputeReplacedSize(dom::NodeId node,
                                               const ReplacedSize& intrinsic) const {
  const style::ComputedStyle& style = styles_.Get(node);
  if (style.width && style.height) {
    return ReplacedSize{ClampLength(*style.width), ClampLength(*style.height)};
  }
  if (style.width) {
    // One specified side keeps the intrinsic aspect ratio.
    const float width = ClampLength(*style.width);
    return ReplacedSize{width, width * intrinsic.height_px / intrinsic.width_px};
  }
  if (style.height) {
    const float height = ClampLength(*style.height);
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

BlockFrame LayoutEngine::MakeFrame(BoxId box, dom::NodeId element) {
  const style::ComputedStyle& style = styles_.Get(element);
  const float padding_left = ClampLength(style.padding_left);
  const float padding_right = ClampLength(style.padding_right);
  BlockFrame frame;
  frame.box = box;
  frame.element = element;
  frame.padding_top = ClampLength(style.padding_top);
  frame.padding_bottom = ClampLength(style.padding_bottom);
  frame.has_fixed_height = style.height.has_value();
  frame.fixed_height = style.height ? ClampLength(*style.height) : 0.0f;
  const Box& box_ref = boxes_[box];
  frame.content_x = box_ref.x + padding_left;
  frame.content_y = box_ref.y + frame.padding_top;
  frame.content_width = box_ref.width - padding_left - padding_right;
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
                                float line_width) {
  // `open` is deliberately kept: an inline element may continue across a block
  // child (rule 10).
  state.container = container;
  state.origin_x = origin_x;
  state.origin_y = origin_y;
  state.line_width = line_width;
  state.cursor_x = origin_x;
  state.cursor_y = 0.0f;
  state.line_height = 0.0f;
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
  for (const BoxId id : state.line_boxes) {
    boxes_[id].y = line_y;
    boxes_[id].height = state.line_height;
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
  state.pending_space = false;
  state.pending_space_advance = 0.0f;
}

void LayoutEngine::MarkSeparator(InlineState& state, float font_size) {
  // Separators collapse: none at the start of a line, never two in a row.
  if (!state.line_has_items || state.pending_space) {
    return;
  }
  const TextMetrics metrics = measure_(std::string_view{" "}, font_size);
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
      fragment_box.x = first_fragment ? state.cursor_x - open.padding_left : state.cursor_x;
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
                             float font_size) {
  const TextMetrics metrics = measure_(word, font_size);
  const BoxId parent = PlaceInlineItem(state, metrics.advance_px);
  const float word_x = state.cursor_x;
  state.cursor_x += metrics.advance_px;
  state.line_has_items = true;
  state.line_height = std::max(state.line_height, metrics.line_height_px);

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
  const ReplacedSize size = ComputeReplacedSize(element, intrinsic);
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
  const float font_size = NonNegativeFontSize(styles_.Get(text_node));
  std::string word;
  for (const char character : data) {
    if (IsAsciiWhitespace(character)) {
      if (!word.empty()) {
        PlaceWord(state, text_node, word, font_size);
        word.clear();
      }
      MarkSeparator(state, font_size);
    } else {
      word.push_back(character);
    }
  }
  if (!word.empty()) {
    PlaceWord(state, text_node, word, font_size);
  }
}

void LayoutEngine::ProcessBreak(InlineState& state, dom::NodeId br) {
  if (state.line_has_items) {
    FinalizeLine(state);
    return;
  }
  // An empty line forced by <br> uses the empty-text line height of the br's
  // font size.
  const TextMetrics metrics = measure_(std::string_view{}, NonNegativeFontSize(styles_.Get(br)));
  state.cursor_y += metrics.line_height_px;
  state.cursor_x = state.origin_x;
  state.pending_space = false;
  state.pending_space_advance = 0.0f;
}

void LayoutEngine::ProcessOpen(InlineState& state, dom::NodeId element) {
  const style::ComputedStyle& style = styles_.Get(element);
  OpenInline open;
  open.node = element;
  open.padding_left = ClampLength(style.padding_left);
  open.padding_right = ClampLength(style.padding_right);
  open.left_advance = style.margin_left + open.padding_left;
  open.right_advance = open.padding_right + style.margin_right;
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
    // element's right padding.
    boxes_[open.current_fragment].width =
        (state.cursor_x - open.current_border_x) + open.padding_right;
    state.cursor_x += open.right_advance;
  } else if (open.has_fragment) {
    // The last fragment ended at a line break; its border box still includes
    // padding-right. margin-right would sit after a flushed line and is dropped.
    boxes_[open.last_fragment].width += open.padding_right;
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
  BeginSession(frame.inline_state, container, frame.content_x, frame.content_y + frame.cursor,
               frame.content_width);
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
  stack.push_back(MakeFrame(root_box, html));
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
        frame.child_margin_top = child_style.margin_top;
        frame.child_margin_bottom = child_style.margin_bottom;
        stack.push_back(MakeFrame(child, item.node));
        continue;
      }
      if (!frame.in_run) {
        StartRun(frame, frame.next - 1);
      }
      ProcessStreamItem(frame.inline_state, item);
      continue;
    }

    // All children are laid out: finalize the block height and resume the
    // parent's flow cursor.
    float content_height = 0.0f;
    if (frame.mixed) {
      FinishRun(frame);
      content_height = frame.cursor;
    } else {
      content_height = FinishSession(frame.inline_state);
    }
    const BoxId finished_box = frame.box;
    const bool has_fixed_height = frame.has_fixed_height;
    const float fixed_height = frame.fixed_height;
    const float padding_top = frame.padding_top;
    const float padding_bottom = frame.padding_bottom;
    stack.pop_back();
    boxes_[finished_box].height =
        (has_fixed_height ? fixed_height : content_height) + padding_top + padding_bottom;
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
