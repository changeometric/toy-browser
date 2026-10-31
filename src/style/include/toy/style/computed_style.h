#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "toy/dom/node.h"

namespace toy::style {

// The computed `display` value. The MVP only knows these three; author values
// outside this set (flex, grid, inline-block, ...) are ignored for now (M15).
enum class Display { kNone, kBlock, kInline };

enum class FontStyle { kNormal, kItalic };

// A computed length: CSS pixels or a percentage of the property's basis. Style
// resolves font-relative units (`em`, `rem`, `%` font-size) before storing a
// value here; layout resolves percentages against the containing block content
// width for box properties (width, min/max-width, margins, padding), as CSS
// does. Percentage heights are not supported in M14 and are treated as auto by
// layout.
struct Length {
  enum class Unit { kPx, kPercent };
  float value = 0.0f;
  Unit unit = Unit::kPx;
};

// A computed margin value: a Length or the CSS `auto` keyword. Horizontal auto
// margins center a block box with a specified width (layout); vertical auto
// margins behave as 0 in this MVP.
struct AutoLength {
  bool is_auto = false;
  Length length;
};

// The computed line-height. `normal` uses the font's own line metrics; a number
// is a multiplier of the element's own computed font size; pixels are absolute.
// Inherited as computed, so a unitless number keeps multiplying a descendant's
// font size (normal CSS inheritance behavior for number line-heights).
struct LineHeight {
  enum class Kind { kNormal, kNumber, kPx };
  Kind kind = Kind::kNormal;
  float value = 0.0f;
};

// Computed text alignment. `justify` is accepted but laid out as left in this
// MVP; `start`/`end` map to left/right (horizontal writing mode only).
enum class TextAlign { kLeft, kRight, kCenter, kJustify };

enum class Visibility { kVisible, kHidden };

// The computed border style. The MVP paints every visible style as a solid line
// (dashed/dotted/double/groove/ridge/inset/outset are parsed and participate in
// the box model but render solid until a later round).
enum class BorderStyle {
  kNone,
  kHidden,
  kSolid,
  kDashed,
  kDotted,
  kDouble,
  kGroove,
  kRidge,
  kInset,
  kOutset
};

// One computed border side: a px width, a style and an optional color. A nullopt
// color means the CSS `currentColor` keyword: dump/paint resolve it to the
// element's computed text color (opaque). Border widths are px only; percentage
// border widths are ignored by the parser. The border participates in the box
// model only through UsedBorderWidth (0 for none/hidden).
struct BorderSide {
  Length width;
  BorderStyle style = BorderStyle::kNone;
  std::optional<std::uint32_t> color;
};

// The computed box-sizing value: content-box (width/height set the content box,
// the default) or border-box (width/height set the border box).
enum class BoxSizing { kContentBox, kBorderBox };

// The computed overflow values. M14b distinguishes only visible vs hidden for
// clipping; `overflow-x`/`overflow-y` are stored separately, but a box clips when
// either axis is hidden (MVP simplification, documented in the roadmap).
enum class Overflow { kVisible, kHidden };

// The border width that participates in layout and painting: 0 when the side's
// style is none/hidden, otherwise the computed px width.
inline float UsedBorderWidth(const BorderSide& side) {
  if (side.style == BorderStyle::kNone || side.style == BorderStyle::kHidden) {
    return 0.0f;
  }
  return side.width.value;
}

// The border color used by dump/paint: the explicit color, or the element's text
// color made opaque for `currentColor`.
inline std::uint32_t UsedBorderColor(const BorderSide& side, std::uint32_t text_color) {
  return side.color ? *side.color : (text_color | 0xFF000000u);
}

// The computed style of one element. Lengths are absolute CSS pixels or
// percentages resolved by layout; `width`/`height` are std::nullopt when auto,
// and the min/max constraints are std::nullopt when unconstrained (initial
// `none`). Inherited properties (`line_height`, `text_align`, `visibility`,
// `text_decoration_underline`, `text_decoration_line_through`,
// `text_decoration_overline`) start from the parent style; everything else
// starts at its initial value. Text decorations inherit in this MVP (CSS
// decorations propagate to descendants); M14b paints underline, line-through and
// overline.
struct ComputedStyle {
  Display display = Display::kInline;
  // 0xRRGGBB; text is always opaque (rgba()/hsla() alpha is dropped for text).
  std::uint32_t color = 0x000000;
  // 0xAARRGGBB; alpha from rgba()/hsla() colors is kept for backgrounds and
  // composited by the rasterizer's SrcOver formula.
  std::uint32_t background_color = 0x00000000;
  float font_size_px = 16.0f;
  int font_weight = 400;
  FontStyle font_style = FontStyle::kNormal;
  // Computed `font-family` list in cascade order. Generic families (serif,
  // sans-serif, monospace) stay lowercase; font family names keep their spelling.
  // The initial value is the user-agent standard font "serif"; the property is
  // inherited. The shell maps this list (with font_weight/font_style and the page
  // language) to real fonts through fontconfig.
  std::vector<std::string> font_family{"serif"};
  // Minimum/maximum box constraints; std::nullopt means no constraint (`none`).
  std::optional<Length> min_width;
  std::optional<Length> max_width;
  std::optional<Length> min_height;
  std::optional<Length> max_height;
  AutoLength margin_top;
  AutoLength margin_right;
  AutoLength margin_bottom;
  AutoLength margin_left;
  Length padding_top;
  Length padding_right;
  Length padding_bottom;
  Length padding_left;
  std::optional<Length> width;
  std::optional<Length> height;
  LineHeight line_height;
  TextAlign text_align = TextAlign::kLeft;
  Visibility visibility = Visibility::kVisible;
  bool text_decoration_underline = false;
  bool text_decoration_line_through = false;
  bool text_decoration_overline = false;
  // Border sides (initial: no style, so they occupy no space). Widths are px and
  // participate in the box model only when the style is not none/hidden; the
  // initial color is currentColor (see UsedBorderColor).
  BorderSide border_top;
  BorderSide border_right;
  BorderSide border_bottom;
  BorderSide border_left;
  // Box sizing and overflow clipping (M14b). Overflow is visible by default.
  BoxSizing box_sizing = BoxSizing::kContentBox;
  Overflow overflow_x = Overflow::kVisible;
  Overflow overflow_y = Overflow::kVisible;
};

// One external author style sheet fetched from a `<link rel="stylesheet">`
// element (M12). The shell owns discovery, filtering and fetching; this type
// only tells the cascade where the fetched text belongs. `link_node` must name
// a `<link>` element of the document passed to ComputeStyles; the sheet then
// joins the author cascade exactly at that element's position in document
// order, interleaved with the document's own `<style>` elements. Entries whose
// node is not a `<link>` element (or is absent) are ignored, and an empty
// `text` contributes no rules. The shell supplies at most one entry per link
// element; if several entries name the same element they apply in input order
// at that position. `text` is parsed exactly like `<style>` content.
struct LinkedStyleSheet {
  dom::NodeId link_node = dom::kInvalidNode;
  std::string text;
};

// Computed styles for a document, addressed like the DOM arena. The tree is
// flat: an element's style is stored under its own node id. Non-element nodes
// report the style of their nearest element ancestor (the default style when
// there is none), so text callers can ask for the style that applies to them.
class StyleTree {
 public:
  const ComputedStyle& Get(dom::NodeId id) const;

 private:
  friend StyleTree ComputeStyles(const dom::Document& document);
  friend StyleTree ComputeStyles(const dom::Document& document,
                                 const std::vector<LinkedStyleSheet>& linked_sheets);
  std::vector<ComputedStyle> styles_;
};

// Resolves the UA sheet, author <style> rules and inline styles into a style
// tree for every node of `document`. `document` must outlive the returned tree
// only while ComputeStyles runs; the tree does not keep a reference to it.
StyleTree ComputeStyles(const dom::Document& document);

// Like the one-argument overload, and additionally applies the external author
// sheets in `linked_sheets`, each at its `<link>` element's document position.
// This overload never fetches anything: the caller resolves `href`s and fetches
// the CSS text before calling. `document` and the `text`s must stay alive only
// while ComputeStyles runs.
StyleTree ComputeStyles(const dom::Document& document,
                        const std::vector<LinkedStyleSheet>& linked_sheets);

// Deterministic text dump of the computed styles, one line per element node:
// indent + "<tag> display=... color=... background-color=... ... width=...
// height=...". Since M14 the line continues with " min-width=... min-height=...
// max-width=... max-height=... line-height=... text-align=... visibility=...
// text-decoration=..." at the end (so pre-M14 regexes still match). Lengths
// print rounded CSS pixels ("12px"), percentages rounded with "%" ("50%"),
// "auto" for auto width/height/margins and "none" for unconstrained min/max.
// line-height prints "normal", a unitless number (up to 3 decimals with trailing
// zeros trimmed) or "NNpx"; text-decoration prints "none" or the space-joined
// tokens underline/line-through/overline in that order. Since M14b the line then
// continues with " border-top-width= border-right-width= border-bottom-width=
// border-left-width=" (USED widths in px, 0 for none/hidden), " border-top-style=
// border-right-style= border-bottom-style= border-left-style=" (none/hidden/
// solid/dashed/dotted/double/groove/ridge/inset/outset), a single
// " border-color=" (four resolved #rrggbb colors, currentColor resolved to the
// text color, alpha dropped), " box-sizing=content-box|border-box" and
// " overflow=visible|hidden" (hidden if either axis is hidden). All M14b fields
// are appended after the M14a fields, so the byte-compat normalization
// (strip from " min-width=") covers them. Used by --dump-style and the style
// tests.
std::string DumpStyleTree(const dom::Document& document, const StyleTree& styles);

}  // namespace toy::style
