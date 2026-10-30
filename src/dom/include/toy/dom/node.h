#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace toy::dom {

// Arena-backed DOM. Nodes live in a std::vector and are addressed by NodeId;
// links are integer ids, so there are no owning pointers and no cycles.
using NodeId = std::uint32_t;

// Returned by links that point nowhere. Only a moved-from/empty Document has
// an invalid root.
inline constexpr NodeId kInvalidNode = 0xFFFFFFFFu;

enum class NodeType { kDocument, kDoctype, kElement, kText, kComment };

struct Attribute {
  std::string name;   // lowercase attribute name
  std::string value;  // entity-decoded attribute value
};

// One node in the arena. Which fields are meaningful depends on `type`:
// - kDocument: the single #document root; name/data/attributes are empty.
// - kDoctype:  name is the lowercase doctype name, data the raw trimmed content.
// - kElement:  name is the lowercase tag name, attributes keep source order.
// - kText:     data is the entity-decoded text.
// - kComment:  data is the comment content, verbatim (no entity decoding).
struct Node {
  NodeType type = NodeType::kText;
  std::string name;
  std::string data;
  std::vector<Attribute> attributes;
  NodeId parent = kInvalidNode;
  NodeId first_child = kInvalidNode;
  NodeId last_child = kInvalidNode;
  NodeId next_sibling = kInvalidNode;
  NodeId prev_sibling = kInvalidNode;
};

// A parsed HTML document. The node with id 0 is the #document root.
class Document {
 public:
  // The #document node. kInvalidNode only for a default-constructed Document;
  // ParseDocument always produces a root.
  NodeId root() const;
  // Returns the node with `id`. `id` must be a valid arena index (asserted).
  const Node& Get(NodeId id) const;
  std::size_t size() const;

 private:
  friend Document ParseDocument(std::string_view html);
  std::vector<Node> nodes_;
};

// Parses `html` into a Document. Error-tolerant: malformed input never fails.
// A leading UTF-8 BOM is stripped. Always produces one html element under
// #document with one head and one body child, in that order.
Document ParseDocument(std::string_view html);

// Deterministic text dump of the tree, one line per node with two spaces of
// indent per depth. Used by --dump-dom and the parser tests.
std::string DumpDocument(const Document& doc);

}  // namespace toy::dom
