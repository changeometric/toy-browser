// Deterministic computed-style dump: one line per element node.
//
// Field order: "<tag> display=... color=... background-color=... font-size=...
// font-weight=... font-style=... font-family=... margin-*... padding-*...
// width=... height=...". The font-family list is joined with ", "; an item that
// is not made only of [A-Za-z0-9_-] is double-quoted with '\' and '"' escaped by
// a backslash (e.g. font-family="Toy Test Font", sans-serif), so the line stays
// unambiguous and deterministic.
//
// Indentation is capped at kMaxIndentDepth levels, like the DOM dump. Without
// the cap a deeply nested document would make the dump grow quadratically
// (10k nested elements would mean ~100 MB of spaces). Normal pages stay far
// below the cap, so their output is unchanged.

#include <cmath>
#include <cstddef>
#include <cstdint>
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

// Lengths are rounded to the nearest integer in the dump.
std::string FormatLength(float value_px) {
  return std::to_string(static_cast<long>(std::lround(value_px))) + "px";
}

std::string FormatAutoLength(const std::optional<float>& value) {
  return value ? FormatLength(*value) : std::string("auto");
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
  line.append(FormatLength(style.font_size_px));
  line.append(" font-weight=");
  line.append(std::to_string(style.font_weight));
  line.append(" font-style=");
  line.append(FontStyleName(style.font_style));
  line.append(" font-family=");
  line.append(FormatFontFamily(style.font_family));
  line.append(" margin-top=");
  line.append(FormatLength(style.margin_top));
  line.append(" margin-right=");
  line.append(FormatLength(style.margin_right));
  line.append(" margin-bottom=");
  line.append(FormatLength(style.margin_bottom));
  line.append(" margin-left=");
  line.append(FormatLength(style.margin_left));
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
