// Style tree accessors.

#include "toy/style/computed_style.h"

namespace toy::style {
namespace {

const ComputedStyle& DefaultStyle() {
  static const ComputedStyle kDefaultStyle;
  return kDefaultStyle;
}

}  // namespace

const ComputedStyle& StyleTree::Get(dom::NodeId id) const {
  if (id < styles_.size()) {
    return styles_[id];
  }
  return DefaultStyle();
}

}  // namespace toy::style
