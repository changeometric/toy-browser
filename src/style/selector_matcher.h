#pragma once

#include "toy/css/stylesheet.h"
#include "toy/dom/node.h"

namespace toy::style::detail {

// Cascade specificity, keeping the inline-style flag as the leading component
// so that inline (1,0,0,0) beats any selector specificity. Comparisons are
// lexicographic: (inline, #id, #class, #type).
struct Specificity {
  int inline_count = 0;
  int id_count = 0;
  int class_count = 0;
  int type_count = 0;
};

bool operator<(const Specificity& left, const Specificity& right);
bool operator==(const Specificity& left, const Specificity& right);

// Specificity of one complex selector such as "div.a > p#x".
Specificity SelectorSpecificity(const css::ComplexSelector& selector);

// True when `element` matches the selector. Type names are compared
// case-insensitively (DOM tag names are lowercase); class and id values are
// compared byte-for-byte, as in HTML. Invalid selectors never match.
bool MatchesComplexSelector(const css::ComplexSelector& selector, const dom::Document& document,
                            dom::NodeId element);

}  // namespace toy::style::detail
