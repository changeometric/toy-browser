// Deterministic DOM dump: one line per node, two spaces of indent per level.
// Used by --dump-dom and by the parser tests, which compare the exact text.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "toy/dom/node.h"

namespace toy::dom {
namespace {

char HexDigit(unsigned int value) {
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + (value - 10));
}

// JSON-style escaping. C0 control bytes are written as \u00XX with uppercase
// hex digits; bytes >= 0x20 (including UTF-8 continuation bytes) stay as-is.
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

std::string NodeLine(const Node& node) {
  std::string line;
  switch (node.type) {
    case NodeType::kDocument:
      line = "#document";
      break;
    case NodeType::kDoctype:
      line = "<!doctype ";
      line.append(node.data);
      line.push_back('>');
      break;
    case NodeType::kElement:
      line.push_back('<');
      line.append(node.name);
      for (const Attribute& attribute : node.attributes) {
        line.push_back(' ');
        line.append(attribute.name);
        line.append("=\"");
        line.append(EscapeText(attribute.value));
        line.push_back('"');
      }
      line.push_back('>');
      break;
    case NodeType::kText:
      line.push_back('"');
      line.append(EscapeText(node.data));
      line.push_back('"');
      break;
    case NodeType::kComment:
      line.append("<!-- ");
      line.append(EscapeText(node.data));
      line.append(" -->");
      break;
  }
  return line;
}

}  // namespace

std::string DumpDocument(const Document& doc) {
  std::string dump;
  const NodeId root = doc.root();
  if (root == kInvalidNode) {
    return dump;
  }
  struct PendingNode {
    NodeId id;
    std::size_t depth;
  };
  std::vector<PendingNode> pending;
  pending.push_back(PendingNode{root, 0});
  std::vector<NodeId> children;
  while (!pending.empty()) {
    const PendingNode current = pending.back();
    pending.pop_back();
    const Node& node = doc.Get(current.id);
    if (!(node.type == NodeType::kText && node.data.empty())) {
      dump.append(current.depth * 2, ' ');
      dump.append(NodeLine(node));
      dump.push_back('\n');
    }
    children.clear();
    for (NodeId child = node.first_child; child != kInvalidNode;
         child = doc.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(PendingNode{children[index], current.depth + 1});
    }
  }
  return dump;
}

}  // namespace toy::dom
