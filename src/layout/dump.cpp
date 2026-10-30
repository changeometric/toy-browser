// Deterministic box-tree dump: one line per box in pre-order, with two spaces
// of indent per depth. Used by --dump-boxes and the layout tests, which compare
// the exact text.
//
// Indentation is capped at kMaxIndentDepth levels, like the DOM and style
// dumps. Without the cap a deeply nested document would make the dump grow
// quadratically; normal pages stay far below the cap.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "toy/dom/node.h"
#include "toy/layout/box.h"

namespace toy::layout {
namespace {

// Maximum tree depth that contributes indentation. Deeper boxes keep the same
// indent, so the dump stays linear in the number of boxes.
constexpr std::size_t kMaxIndentDepth = 64;

char HexDigit(unsigned int value) {
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + (value - 10));
}

// JSON-style escaping, identical to dom::DumpDocument. C0 control bytes are
// written as \u00XX with uppercase hex digits; bytes >= 0x20 (including UTF-8
// continuation bytes) stay as-is.
std::string EscapeText(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char character : text) {
    const unsigned char byte = static_cast<unsigned char>(character);
    switch (character) {
      case '"':
        escaped.append("\\\"");
        break;
      case '\\':
        escaped.append("\\\\");
        break;
      case '\n':
        escaped.append("\\n");
        break;
      case '\t':
        escaped.append("\\t");
        break;
      case '\r':
        escaped.append("\\r");
        break;
      default:
        if (byte < 0x20) {
          escaped.append("\\u00");
          escaped.push_back(HexDigit(byte >> 4));
          escaped.push_back(HexDigit(byte & 0x0F));
        } else {
          escaped.push_back(character);
        }
        break;
    }
  }
  return escaped;
}

// Geometry is rounded to the nearest integer (negative values allowed).
std::string FormatInt(float value) { return std::to_string(std::lround(value)); }

void AppendRect(std::string& line, const Box& box) {
  line.append(" x=");
  line.append(FormatInt(box.x));
  line.append(" y=");
  line.append(FormatInt(box.y));
  line.append(" width=");
  line.append(FormatInt(box.width));
  line.append(" height=");
  line.append(FormatInt(box.height));
}

std::string BoxLine(const dom::Document& document, const Box& box) {
  std::string line;
  line.reserve(64);
  switch (box.kind) {
    case BoxKind::kBlock:
    case BoxKind::kInline:
      line.push_back('<');
      line.append(document.Get(box.node).name);
      line.push_back('>');
      line.push_back(' ');
      line.append(box.kind == BoxKind::kBlock ? "block" : "inline");
      break;
    case BoxKind::kAnonymousBlock:
      line.append("anonymous block");
      break;
    case BoxKind::kText:
      line.append("text \"");
      line.append(EscapeText(box.text));
      line.push_back('"');
      break;
  }
  AppendRect(line, box);
  return line;
}

}  // namespace

std::string DumpBoxTree(const dom::Document& document, const BoxTree& boxes) {
  std::string dump;
  const BoxId root = boxes.root();
  if (root == kInvalidBox) {
    return dump;
  }
  struct PendingBox {
    BoxId id;
    std::size_t depth;
  };
  std::vector<PendingBox> pending;
  pending.push_back(PendingBox{root, 0});
  std::vector<BoxId> children;
  while (!pending.empty()) {
    const PendingBox current = pending.back();
    pending.pop_back();
    const Box& box = boxes.Get(current.id);
    const std::size_t indent_depth =
        current.depth < kMaxIndentDepth ? current.depth : kMaxIndentDepth;
    dump.append(indent_depth * 2, ' ');
    dump.append(BoxLine(document, box));
    dump.push_back('\n');
    children.clear();
    for (BoxId child = box.first_child; child != kInvalidBox;
         child = boxes.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(PendingBox{children[index], current.depth + 1});
    }
  }
  return dump;
}

}  // namespace toy::layout
