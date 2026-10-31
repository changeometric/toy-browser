// Cascade: embedded user-agent sheet -> author style sheets in document order
// (<style> elements and caller-supplied external sheets at their <link>
// elements) -> inline styles, followed by inheritance for the inherited
// properties.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "property_parser.h"
#include "selector_matcher.h"
#include "toy/css/stylesheet.h"
#include "toy/dom/node.h"
#include "toy/style/computed_style.h"
#include "ua_stylesheet.h"

namespace toy::style {
namespace {

enum class Origin { kUserAgent, kAuthor };

struct RegisteredRule {
  css::Rule rule;
  Origin origin = Origin::kAuthor;
  // Global order of this rule's first declaration; gives "ties by source
  // order" a stable, declaration-level key across all sheets.
  std::size_t declaration_order_base = 0;
};

struct CascadeCandidate {
  int rank = 0;
  detail::Specificity specificity;
  std::size_t order = 0;
  const css::Declaration* declaration = nullptr;
};

// Cascade ranks, low to high: UA normal, author normal, author !important,
// UA !important. The embedded UA sheet uses no !important, but keeping the
// spec-like order costs nothing. Inline declarations keep their own
// importance flag and carry inline specificity.
int CascadeRank(Origin origin, bool important) {
  if (origin == Origin::kUserAgent) {
    return important ? 3 : 0;
  }
  return important ? 2 : 1;
}

bool CandidateLess(const CascadeCandidate& left, const CascadeCandidate& right) {
  if (left.rank != right.rank) {
    return left.rank < right.rank;
  }
  if (left.specificity < right.specificity) {
    return true;
  }
  if (right.specificity < left.specificity) {
    return false;
  }
  return left.order < right.order;
}

// Visits the document in tree order (parents before children), without
// recursion so deeply nested documents are safe.
template <typename Visitor>
void VisitPreOrder(const dom::Document& document, Visitor visitor) {
  const dom::NodeId root = document.root();
  if (root == dom::kInvalidNode) {
    return;
  }
  std::vector<dom::NodeId> pending;
  pending.push_back(root);
  std::vector<dom::NodeId> children;
  while (!pending.empty()) {
    const dom::NodeId id = pending.back();
    pending.pop_back();
    visitor(id);
    children.clear();
    for (dom::NodeId child = document.Get(id).first_child; child != dom::kInvalidNode;
         child = document.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(children[index]);
    }
  }
}

const dom::Attribute* FindAttribute(const dom::Node& node, std::string_view name) {
  for (const dom::Attribute& attribute : node.attributes) {
    if (attribute.name == name) {
      return &attribute;
    }
  }
  return nullptr;
}

// The text children of an element, concatenated in source order.
std::string CollectElementText(const dom::Document& document, dom::NodeId id) {
  std::string text;
  for (dom::NodeId child = document.Get(id).first_child; child != dom::kInvalidNode;
       child = document.Get(child).next_sibling) {
    const dom::Node& child_node = document.Get(child);
    if (child_node.type == dom::NodeType::kText) {
      text.append(child_node.data);
    }
  }
  return text;
}

// Author sheet texts in document order: <style> text interleaved with the
// external sheets registered at their <link> elements. External texts are
// already fetched by the caller; style never does I/O.
std::vector<std::string> CollectAuthorStyleSheets(
    const dom::Document& document, const std::vector<LinkedStyleSheet>& linked_sheets) {
  std::vector<std::string> texts;
  if (linked_sheets.empty()) {
    // Pre-M12 fast path: a document without external sheets pays no extra
    // per-element work.
    VisitPreOrder(document, [&](dom::NodeId id) {
      const dom::Node& node = document.Get(id);
      if (node.type != dom::NodeType::kElement || node.name != "style") {
        return;
      }
      std::string text = CollectElementText(document, id);
      if (!text.empty()) {
        texts.push_back(std::move(text));
      }
    });
    return texts;
  }
  VisitPreOrder(document, [&](dom::NodeId id) {
    const dom::Node& node = document.Get(id);
    if (node.type != dom::NodeType::kElement) {
      return;
    }
    if (node.name == "style") {
      std::string text = CollectElementText(document, id);
      if (!text.empty()) {
        texts.push_back(std::move(text));
      }
      return;
    }
    if (node.name != "link") {
      return;
    }
    // Entries that name another node (or a missing/invalid one) never match a
    // walked <link>, so they are ignored without a validation pass. Matching
    // entries keep their input order, and empty texts contribute no rules.
    for (const LinkedStyleSheet& linked : linked_sheets) {
      if (linked.link_node != id || linked.text.empty()) {
        continue;
      }
      texts.push_back(linked.text);
    }
  });
  return texts;
}

void RegisterStyleSheet(const css::StyleSheet& sheet, Origin origin,
                        std::vector<RegisteredRule>* rules, std::size_t* declaration_order) {
  for (const css::Rule& rule : sheet.rules) {
    RegisteredRule registered;
    registered.rule = rule;
    registered.origin = origin;
    registered.declaration_order_base = *declaration_order;
    *declaration_order += rule.declarations.size();
    rules->push_back(std::move(registered));
  }
}

// Highest specificity among the matching alternatives of a selector list; a
// comma group behaves like separate rules, so the most specific match wins.
bool MatchRuleSelector(const css::Rule& rule, const dom::Document& document, dom::NodeId element,
                       detail::Specificity* specificity) {
  bool matched = false;
  detail::Specificity best;
  for (const css::ComplexSelector& alternative : rule.selector.alternatives) {
    if (!detail::MatchesComplexSelector(alternative, document, element)) {
      continue;
    }
    const detail::Specificity alternative_specificity = detail::SelectorSpecificity(alternative);
    if (!matched || best < alternative_specificity) {
      best = alternative_specificity;
    }
    matched = true;
  }
  if (matched) {
    *specificity = best;
  }
  return matched;
}

}  // namespace

StyleTree ComputeStyles(const dom::Document& document,
                        const std::vector<LinkedStyleSheet>& linked_sheets) {
  StyleTree tree;
  tree.styles_.resize(document.size());

  std::vector<RegisteredRule> rules;
  std::size_t declaration_order = 0;
  RegisterStyleSheet(css::ParseStyleSheet(detail::UserAgentStyleSheet()), Origin::kUserAgent,
                     &rules, &declaration_order);
  for (const std::string& sheet_text : CollectAuthorStyleSheets(document, linked_sheets)) {
    RegisterStyleSheet(css::ParseStyleSheet(sheet_text), Origin::kAuthor, &rules,
                       &declaration_order);
  }

  VisitPreOrder(document, [&](dom::NodeId id) {
    const dom::Node& node = document.Get(id);
    if (node.type != dom::NodeType::kElement) {
      // Non-element nodes report the style that applies to them.
      tree.styles_[id] =
          node.parent != dom::kInvalidNode ? tree.styles_[node.parent] : ComputedStyle{};
      return;
    }
    const ComputedStyle parent_style =
        node.parent != dom::kInvalidNode ? tree.styles_[node.parent] : ComputedStyle{};

    std::vector<CascadeCandidate> candidates;
    for (const RegisteredRule& registered : rules) {
      detail::Specificity specificity;
      if (!MatchRuleSelector(registered.rule, document, id, &specificity)) {
        continue;
      }
      for (std::size_t index = 0; index < registered.rule.declarations.size(); ++index) {
        const css::Declaration& declaration = registered.rule.declarations[index];
        CascadeCandidate candidate;
        candidate.rank = CascadeRank(registered.origin, declaration.important);
        candidate.specificity = specificity;
        candidate.order = registered.declaration_order_base + index;
        candidate.declaration = &declaration;
        candidates.push_back(candidate);
      }
    }

    const dom::Attribute* style_attribute = FindAttribute(node, "style");
    std::vector<css::Declaration> inline_declarations;
    if (style_attribute != nullptr) {
      inline_declarations = css::ParseDeclarationList(style_attribute->value);
    }
    // Inline declarations come after every sheet and carry inline specificity.
    const std::size_t inline_order_base = declaration_order;
    for (std::size_t index = 0; index < inline_declarations.size(); ++index) {
      CascadeCandidate candidate;
      candidate.rank = CascadeRank(Origin::kAuthor, inline_declarations[index].important);
      candidate.specificity.inline_count = 1;
      candidate.order = inline_order_base + index;
      candidate.declaration = &inline_declarations[index];
      candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(), CandidateLess);

    // Inherited properties start from the parent; everything else starts at
    // its initial value (the defaults of ComputedStyle).
    ComputedStyle style;
    style.color = parent_style.color;
    style.font_size_px = parent_style.font_size_px;
    style.font_weight = parent_style.font_weight;
    style.font_style = parent_style.font_style;
    style.font_family = parent_style.font_family;

    // Font size first: the other longhands resolve `em` against it.
    for (const CascadeCandidate& candidate : candidates) {
      if (candidate.declaration->property == "font-size") {
        detail::ApplyFontSizeDeclaration(*candidate.declaration, parent_style.font_size_px, &style);
      }
    }
    for (const CascadeCandidate& candidate : candidates) {
      if (candidate.declaration->property == "font-size") {
        continue;
      }
      detail::ApplyDeclaration(*candidate.declaration, &style);
    }
    tree.styles_[id] = style;
  });

  return tree;
}

// One-argument overload: no external author sheets, so it is exactly the
// pre-M12 behavior.
StyleTree ComputeStyles(const dom::Document& document) {
  return ComputeStyles(document, {});
}

}  // namespace toy::style
