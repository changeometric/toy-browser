// Selector matching for the style cascade. The CSS module has no DOM, so the
// matcher lives here: it walks the ancestor chain of an element.

#include "selector_matcher.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "toy/css/stylesheet.h"
#include "toy/dom/node.h"

namespace toy::style::detail {
namespace {

bool IsAsciiWhitespace(char character) {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
         character == '\f';
}

char ToLowerAscii(char character) {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<char>(character - 'A' + 'a');
  }
  return character;
}

bool EqualsIgnoreCaseAscii(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (ToLowerAscii(left[index]) != ToLowerAscii(right[index])) {
      return false;
    }
  }
  return true;
}

const dom::Attribute* FindAttribute(const dom::Node& node, std::string_view name) {
  for (const dom::Attribute& attribute : node.attributes) {
    if (attribute.name == name) {
      return &attribute;
    }
  }
  return nullptr;
}

// True when `token` is one of the space-separated class names in `value`.
bool HasClassToken(std::string_view value, std::string_view token) {
  std::size_t index = 0;
  while (index < value.size()) {
    while (index < value.size() && IsAsciiWhitespace(value[index])) {
      ++index;
    }
    const std::size_t start = index;
    while (index < value.size() && !IsAsciiWhitespace(value[index])) {
      ++index;
    }
    if (value.substr(start, index - start) == token) {
      return true;
    }
  }
  return false;
}

bool CompoundMatches(const css::CompoundSelector& compound, const dom::Document& document,
                     dom::NodeId element) {
  const dom::Node& node = document.Get(element);
  if (node.type != dom::NodeType::kElement) {
    return false;
  }
  for (const css::SimpleSelector& part : compound.parts) {
    switch (part.kind) {
      case css::SimpleKind::kUniversal:
        break;
      case css::SimpleKind::kType:
        // DOM tag names are lowercase; CSS type selectors are case-insensitive.
        if (!EqualsIgnoreCaseAscii(part.name, node.name)) {
          return false;
        }
        break;
      case css::SimpleKind::kClass: {
        const dom::Attribute* class_attribute = FindAttribute(node, "class");
        if (class_attribute == nullptr || !HasClassToken(class_attribute->value, part.name)) {
          return false;
        }
        break;
      }
      case css::SimpleKind::kId: {
        const dom::Attribute* id_attribute = FindAttribute(node, "id");
        if (id_attribute == nullptr || id_attribute->value != part.name) {
          return false;
        }
        break;
      }
    }
  }
  return true;
}

// Matches compounds[compound_index] against `element` and the compounds to its
// left against ancestors, walking right to left.
bool MatchCompoundAt(const css::ComplexSelector& selector, const dom::Document& document,
                     dom::NodeId element, std::size_t compound_index) {
  if (!CompoundMatches(selector.compounds[compound_index], document, element)) {
    return false;
  }
  if (compound_index == 0) {
    return true;
  }
  const css::Combinator combinator = selector.combinators[compound_index - 1];
  dom::NodeId ancestor = document.Get(element).parent;
  if (combinator == css::Combinator::kChild) {
    return ancestor != dom::kInvalidNode &&
           MatchCompoundAt(selector, document, ancestor, compound_index - 1);
  }
  while (ancestor != dom::kInvalidNode) {
    if (MatchCompoundAt(selector, document, ancestor, compound_index - 1)) {
      return true;
    }
    ancestor = document.Get(ancestor).parent;
  }
  return false;
}

}  // namespace

bool operator<(const Specificity& left, const Specificity& right) {
  if (left.inline_count != right.inline_count) {
    return left.inline_count < right.inline_count;
  }
  if (left.id_count != right.id_count) {
    return left.id_count < right.id_count;
  }
  if (left.class_count != right.class_count) {
    return left.class_count < right.class_count;
  }
  return left.type_count < right.type_count;
}

bool operator==(const Specificity& left, const Specificity& right) {
  return left.inline_count == right.inline_count && left.id_count == right.id_count &&
         left.class_count == right.class_count && left.type_count == right.type_count;
}

Specificity SelectorSpecificity(const css::ComplexSelector& selector) {
  Specificity specificity;
  for (const css::CompoundSelector& compound : selector.compounds) {
    for (const css::SimpleSelector& part : compound.parts) {
      switch (part.kind) {
        case css::SimpleKind::kUniversal:
          break;
        case css::SimpleKind::kType:
          ++specificity.type_count;
          break;
        case css::SimpleKind::kClass:
          ++specificity.class_count;
          break;
        case css::SimpleKind::kId:
          ++specificity.id_count;
          break;
      }
    }
  }
  return specificity;
}

bool MatchesComplexSelector(const css::ComplexSelector& selector, const dom::Document& document,
                            dom::NodeId element) {
  if (selector.compounds.empty()) {
    return false;
  }
  if (selector.combinators.size() + 1 != selector.compounds.size()) {
    return false;
  }
  if (element == dom::kInvalidNode || element >= document.size()) {
    return false;
  }
  return MatchCompoundAt(selector, document, element, selector.compounds.size() - 1);
}

}  // namespace toy::style::detail
