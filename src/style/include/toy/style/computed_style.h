#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "toy/dom/node.h"

namespace toy::style {

// The computed `display` value. The MVP only knows these three.
enum class Display { kNone, kBlock, kInline };

enum class FontStyle { kNormal, kItalic };

// The computed style of one element. All lengths are CSS pixels; `width` and
// `height` are std::nullopt when their computed value is `auto`.
struct ComputedStyle {
  Display display = Display::kInline;
  // 0xRRGGBB; there is no alpha channel for text.
  std::uint32_t color = 0x000000;
  // 0xAARRGGBB; alpha 0 means transparent.
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
  float margin_top = 0;
  float margin_right = 0;
  float margin_bottom = 0;
  float margin_left = 0;
  float padding_top = 0;
  float padding_right = 0;
  float padding_bottom = 0;
  float padding_left = 0;
  std::optional<float> width;
  std::optional<float> height;
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
  std::vector<ComputedStyle> styles_;
};

// Resolves the UA sheet, author <style> rules and inline styles into a style
// tree for every node of `document`. `document` must outlive the returned tree
// only while ComputeStyles runs; the tree does not keep a reference to it.
StyleTree ComputeStyles(const dom::Document& document);

// Deterministic text dump of the computed styles, one line per element node:
// indent + "<tag> display=... color=... background-color=... ... width=...".
// Lengths are rounded to the nearest integer and end in "px". Used by
// --dump-style and the style tests.
std::string DumpStyleTree(const dom::Document& document, const StyleTree& styles);

}  // namespace toy::style
