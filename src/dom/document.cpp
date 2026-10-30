// DOM arena accessors.

#include <cassert>

#include "toy/dom/node.h"

namespace toy::dom {

NodeId Document::root() const {
  if (nodes_.empty()) {
    return kInvalidNode;
  }
  return 0;
}

const Node& Document::Get(NodeId id) const {
  assert(id < nodes_.size());
  return nodes_[id];
}

std::size_t Document::size() const { return nodes_.size(); }

}  // namespace toy::dom
