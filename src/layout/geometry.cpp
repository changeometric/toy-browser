// Box-tree geometry queries: hit-testing and scrollable content height. Both are
// pure functions of the laid-out rectangles (no DOM or style knowledge) and both
// are iterative and linear in the number of boxes.

#include <cstddef>
#include <vector>

#include "toy/layout/box.h"

namespace toy::layout {

BoxId HitTest(const BoxTree& boxes, float x, float y) {
  const BoxId root = boxes.root();
  if (root == kInvalidBox) {
    return kInvalidBox;
  }
  // Iterative pre-order walk. A later match overwrites an earlier one, so the
  // result is the box painted last: children and later siblings win over their
  // ancestors. Rectangles are half-open and a non-positive size never matches.
  BoxId hit = kInvalidBox;
  std::vector<BoxId> pending;
  pending.push_back(root);
  std::vector<BoxId> children;
  while (!pending.empty()) {
    const BoxId id = pending.back();
    pending.pop_back();
    const Box& box = boxes.Get(id);
    if (box.width > 0.0f && box.height > 0.0f && box.x <= x && x < box.x + box.width &&
        box.y <= y && y < box.y + box.height) {
      hit = id;
    }
    children.clear();
    for (BoxId child = box.first_child; child != kInvalidBox;
         child = boxes.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(children[index]);
    }
  }
  return hit;
}

float ContentHeight(const BoxTree& boxes) {
  // The tree is an arena, so one pass over all boxes sees every box once. A
  // child may overflow its parent (fixed heights), so this is a max over boxes,
  // not the root's bottom edge; the result is clamped to >= 0.
  float height = 0.0f;
  for (std::size_t index = 0; index < boxes.size(); ++index) {
    const Box& box = boxes.Get(static_cast<BoxId>(index));
    const float bottom = box.y + box.height;
    if (bottom > height) {
      height = bottom;
    }
  }
  return height;
}

}  // namespace toy::layout
