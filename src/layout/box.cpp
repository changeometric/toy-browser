// BoxTree accessors.

#include "toy/layout/box.h"

#include <cassert>

namespace toy::layout {

BoxId BoxTree::root() const { return boxes_.empty() ? kInvalidBox : BoxId{0}; }

const Box& BoxTree::Get(BoxId id) const {
  assert(id < boxes_.size());
  return boxes_[id];
}

std::size_t BoxTree::size() const { return boxes_.size(); }

}  // namespace toy::layout
