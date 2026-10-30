#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace toy::css {

// One simple selector inside a compound selector, for example the "div", ".a"
// or "#b" part of "div.a#b".
enum class SimpleKind { kType, kClass, kId, kUniversal };

struct SimpleSelector {
  SimpleKind kind = SimpleKind::kUniversal;
  // Type name, class name or id value. Empty for kUniversal.
  std::string name;
};

// Selectors that apply to a single element, concatenated without whitespace.
struct CompoundSelector {
  // Parts in source order; at most one type/universal part, always first.
  std::vector<SimpleSelector> parts;
};

// Relationship between two adjacent compound selectors.
enum class Combinator {
  kDescendant,  // whitespace: "div p"
  kChild,       // '>': "div > p"
};

// A full selector such as "div.a > p" without commas.
struct ComplexSelector {
  std::vector<CompoundSelector> compounds;
  // Size is compounds.size() - 1 when the selector is valid.
  std::vector<Combinator> combinators;
};

// One "property: value" declaration, with an optional "!important" flag.
struct Declaration {
  // Lowercase property name.
  std::string property;
  // Trimmed value with internal whitespace collapsed to single spaces.
  std::string value;
  bool important = false;
};

// A comma-separated selector list, for example "h1, p em".
struct Selector {
  std::vector<ComplexSelector> alternatives;
};

// One style rule: a selector list plus its declaration block.
struct Rule {
  Selector selector;
  std::vector<Declaration> declarations;
};

// The rules of one style sheet, in source order. At-rules are skipped.
struct StyleSheet {
  std::vector<Rule> rules;
};

// Parses a style sheet. Only the deliberately small M4 subset is supported:
// type/class/id/universal compound selectors, descendant and child
// combinators, comma groups, plain declarations and !important. At-rules and
// unsupported selectors are skipped. Parsing is error-tolerant and never
// fails: malformed input yields whatever rules could be recovered.
StyleSheet ParseStyleSheet(std::string_view css);

// Parses a declaration list, for example the value of a style="" attribute.
// Malformed declarations are skipped; parsing never fails.
std::vector<Declaration> ParseDeclarationList(std::string_view text);

}  // namespace toy::css
