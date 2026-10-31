// Deterministic computed-style dump: one line per element node.
//
// Field order: "<tag> display=... color=... background-color=... font-size=...
// font-weight=... font-style=... font-family=... margin-*... padding-*...
// width=... height=..." followed since M14 by " min-width=... min-height=...
// max-width=... max-height=... line-height=... text-align=... visibility=...
// text-decoration=...". The font-family list is joined with ", "; an item that
// is not made only of [A-Za-z0-9_-] is double-quoted with '\' and '"' escaped by
// a backslash (e.g. font-family="Toy Test Font", sans-serif), so the line stays
// unambiguous and deterministic. Lengths print rounded CSS pixels ("12px") or
// rounded percentages ("50%"); auto width/height and auto margins print "auto",
// unconstrained min/max print "none". line-height prints "normal", a unitless
// multiplier (up to 3 decimals, trailing zeros trimmed) or "NNpx".
//
// Indentation is capped at kMaxIndentDepth levels, like the DOM dump. Without
// the cap a deeply nested document would make the dump grow quadratically
// (10k nested elements would mean ~100 MB of spaces). Normal pages stay far
// below the cap, so their output is unchanged.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "toy/dom/node.h"
#include "toy/style/computed_style.h"

namespace toy::style {
namespace {

// Maximum tree depth that contributes indentation. Deeper nodes keep the same
// indent, so the dump stays linear in the number of elements.
constexpr std::size_t kMaxIndentDepth = 64;

constexpr char kHexDigits[] = "0123456789abcdef";

std::string FormatColor(std::uint32_t color) {
  std::string text;
  text.reserve(7);
  text.push_back('#');
  for (int shift = 20; shift >= 0; shift -= 4) {
    text.push_back(kHexDigits[(color >> shift) & 0x0Fu]);
  }
  return text;
}

// Computed pixels are rounded to the nearest integer in the dump.
std::string FormatPxLength(float value_px) {
  return std::to_string(static_cast<long>(std::lround(value_px))) + "px";
}

// A computed style length: pixels or an unresolved percentage.
std::string FormatLength(const Length& length) {
  const long rounded = std::lround(length.value);
  if (length.unit == Length::Unit::kPercent) {
    return std::to_string(rounded) + "%";
  }
  return std::to_string(rounded) + "px";
}

// A width/height value: std::nullopt is `auto`.
std::string FormatAutoLength(const std::optional<Length>& value) {
  return value ? FormatLength(*value) : std::string("auto");
}

// A margin value: the literal `auto` or a length.
std::string FormatMargin(const AutoLength& value) {
  return value.is_auto ? std::string("auto") : FormatLength(value.length);
}

// A min/max constraint: `none` when unconstrained.
std::string FormatConstraint(const std::optional<Length>& value) {
  return value ? FormatLength(*value) : std::string("none");
}

// A line-height value: `normal`, a unitless multiplier with up to 3 decimals
// and trailing zeros trimmed, or absolute pixels.
std::string FormatLineHeight(const LineHeight& line_height) {
  switch (line_height.kind) {
    case LineHeight::Kind::kNormal:
      return "normal";
    case LineHeight::Kind::kPx:
      return std::to_string(std::lround(line_height.value)) + "px";
    case LineHeight::Kind::kNumber: {
      char buffer[32];
      std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(line_height.value));
      std::string text(buffer);
      const std::size_t dot = text.find('.');
      if (dot != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
          text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
          text.pop_back();
        }
      }
      return text;
    }
  }
  return "normal";
}

const char* TextAlignName(TextAlign text_align) {
  switch (text_align) {
    case TextAlign::kLeft:
      return "left";
    case TextAlign::kRight:
      return "right";
    case TextAlign::kCenter:
      return "center";
    case TextAlign::kJustify:
      return "justify";
  }
  return "left";
}

const char* VisibilityName(Visibility visibility) {
  switch (visibility) {
    case Visibility::kVisible:
      return "visible";
    case Visibility::kHidden:
      return "hidden";
  }
  return "visible";
}

const char* DisplayName(Display display) {
  switch (display) {
    case Display::kNone:
      return "none";
    case Display::kBlock:
      return "block";
    case Display::kInline:
      return "inline";
  }
  return "inline";
}

const char* FontStyleName(FontStyle font_style) {
  switch (font_style) {
    case FontStyle::kNormal:
      return "normal";
    case FontStyle::kItalic:
      return "italic";
  }
  return "normal";
}

std::string FormatBackgroundColor(std::uint32_t color) {
  if ((color & 0xFF000000u) == 0) {
    return "transparent";
  }
  return FormatColor(color & 0xFFFFFFu);
}

// An item is printed raw only when it is a non-empty run of [A-Za-z0-9_-].
bool IsPlainFontFamilyItem(const std::string& family) {
  if (family.empty()) {
    return false;
  }
  for (const char character : family) {
    const bool plain =
        (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '-' || character == '_';
    if (!plain) {
      return false;
    }
  }
  return true;
}

std::string FormatFontFamily(const std::vector<std::string>& families) {
  std::string text;
  for (std::size_t index = 0; index < families.size(); ++index) {
    if (index != 0) {
      text.append(", ");
    }
    if (IsPlainFontFamilyItem(families[index])) {
      text.append(families[index]);
      continue;
    }
    text.push_back('"');
    for (const char character : families[index]) {
      if (character == '\\' || character == '"') {
        text.push_back('\\');
      }
      text.push_back(character);
    }
    text.push_back('"');
  }
  return text;
}

std::string StyleLine(const dom::Node& node, const ComputedStyle& style) {
  std::string line;
  line.reserve(256);
  line.push_back('<');
  line.append(node.name);
  line.append("> display=");
  line.append(DisplayName(style.display));
  line.append(" color=");
  line.append(FormatColor(style.color));
  line.append(" background-color=");
  line.append(FormatBackgroundColor(style.background_color));
  line.append(" font-size=");
  line.append(FormatPxLength(style.font_size_px));
  line.append(" font-weight=");
  line.append(std::to_string(style.font_weight));
  line.append(" font-style=");
  line.append(FontStyleName(style.font_style));
  line.append(" font-family=");
  line.append(FormatFontFamily(style.font_family));
  line.append(" margin-top=");
  line.append(FormatMargin(style.margin_top));
  line.append(" margin-right=");
  line.append(FormatMargin(style.margin_right));
  line.append(" margin-bottom=");
  line.append(FormatMargin(style.margin_bottom));
  line.append(" margin-left=");
  line.append(FormatMargin(style.margin_left));
  line.append(" padding-top=");
  line.append(FormatLength(style.padding_top));
  line.append(" padding-right=");
  line.append(FormatLength(style.padding_right));
  line.append(" padding-bottom=");
  line.append(FormatLength(style.padding_bottom));
  line.append(" padding-left=");
  line.append(FormatLength(style.padding_left));
  line.append(" width=");
  line.append(FormatAutoLength(style.width));
  line.append(" height=");
  line.append(FormatAutoLength(style.height));
  // M14 fields are appended so pre-M14 regexes still match every prefix.
  line.append(" min-width=");
  line.append(FormatConstraint(style.min_width));
  line.append(" min-height=");
  line.append(FormatConstraint(style.min_height));
  line.append(" max-width=");
  line.append(FormatConstraint(style.max_width));
  line.append(" max-height=");
  line.append(FormatConstraint(style.max_height));
  line.append(" line-height=");
  line.append(FormatLineHeight(style.line_height));
  line.append(" text-align=");
  line.append(TextAlignName(style.text_align));
  line.append(" visibility=");
  line.append(VisibilityName(style.visibility));
  line.append(" text-decoration=");
  line.append(style.text_decoration_underline ? "underline" : "none");
  return line;
}

}  // namespace

std::string DumpStyleTree(const dom::Document& document, const StyleTree& styles) {
  std::string dump;
  const dom::NodeId root = document.root();
  if (root == dom::kInvalidNode) {
    return dump;
  }
  struct PendingNode {
    dom::NodeId id;
    std::size_t depth;
  };
  std::vector<PendingNode> pending;
  pending.push_back(PendingNode{root, 0});
  std::vector<dom::NodeId> children;
  while (!pending.empty()) {
    const PendingNode current = pending.back();
    pending.pop_back();
    const dom::Node& node = document.Get(current.id);
    if (node.type == dom::NodeType::kElement) {
      const std::size_t indent_depth =
          current.depth < kMaxIndentDepth ? current.depth : kMaxIndentDepth;
      dump.append(indent_depth * 2, ' ');
      dump.append(StyleLine(node, styles.Get(current.id)));
      dump.push_back('\n');
    }
    children.clear();
    for (dom::NodeId child = node.first_child; child != dom::kInvalidNode;
         child = document.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(PendingNode{children[index], current.depth + 1});
    }
  }
  return dump;
}

}  // namespace toy::style
