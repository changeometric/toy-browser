// font-family parsing (quoted names, generics, invalid values), inheritance,
// cascade/!important and the UA font rules added in M10.

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "support/check.h"
#include "toy/dom/node.h"
#include "toy/style/computed_style.h"

namespace {

using toy::dom::Document;
using toy::dom::NodeId;
using toy::dom::NodeType;
using toy::style::ComputedStyle;
using toy::style::FontStyle;
using toy::style::StyleTree;

std::vector<NodeId> CollectElements(const Document& document) {
  std::vector<NodeId> elements;
  if (document.root() == toy::dom::kInvalidNode) {
    return elements;
  }
  std::vector<NodeId> pending;
  pending.push_back(document.root());
  std::vector<NodeId> children;
  while (!pending.empty()) {
    const NodeId id = pending.back();
    pending.pop_back();
    if (document.Get(id).type == NodeType::kElement) {
      elements.push_back(id);
    }
    children.clear();
    for (NodeId child = document.Get(id).first_child; child != toy::dom::kInvalidNode;
         child = document.Get(child).next_sibling) {
      children.push_back(child);
    }
    for (std::size_t index = children.size(); index-- > 0;) {
      pending.push_back(children[index]);
    }
  }
  return elements;
}

std::string AttributeValue(const toy::dom::Node& node, std::string_view name) {
  for (const toy::dom::Attribute& attribute : node.attributes) {
    if (attribute.name == name) {
      return attribute.value;
    }
  }
  return std::string();
}

NodeId FindElementById(const Document& document, std::string_view id) {
  for (const NodeId element : CollectElements(document)) {
    if (AttributeValue(document.Get(element), "id") == id) {
      return element;
    }
  }
  return toy::dom::kInvalidNode;
}

std::string JoinFamilies(const std::vector<std::string>& families) {
  std::string joined;
  for (std::size_t index = 0; index < families.size(); ++index) {
    if (index != 0) {
      joined.append(" | ");
    }
    joined.append(families[index]);
  }
  return joined;
}

void ExpectFamilies(const Document& document, std::string_view id,
                    const std::vector<std::string>& expected, const char* description) {
  const StyleTree styles = toy::style::ComputeStyles(document);
  const NodeId node = FindElementById(document, id);
  if (node == toy::dom::kInvalidNode) {
    toy_test::Check(false, description);
    return;
  }
  const std::vector<std::string>& actual = styles.Get(node).font_family;
  const bool matches = actual == expected;
  if (!matches) {
    std::fprintf(stderr, "font-family test: %s\n  expected: %s\n  actual:   %s\n", description,
                 JoinFamilies(expected).c_str(), JoinFamilies(actual).c_str());
  }
  toy_test::Check(matches, description);
}

// A document with one `<div id=target>` (the parsed page wraps it in html/body).
// The style attribute uses single quotes so the declarations may contain
// double-quoted family names.
Document TargetPage(const std::string& declarations) {
  return toy::dom::ParseDocument("<div id=target style='" + declarations + "'>x</div>");
}

void TestParsing() {
  ExpectFamilies(TargetPage("font-family: \"Toy Test Font\""), "target", {"Toy Test Font"},
                 "a double-quoted family name keeps its spaces");
  ExpectFamilies(
      toy::dom::ParseDocument("<div id=target style=\"font-family: 'Toy Font'\">x</div>"), "target",
      {"Toy Font"}, "a single-quoted family name is accepted");
  ExpectFamilies(TargetPage("font-family: \"A, B\", serif"), "target", {"A, B", "serif"},
                 "a comma inside a quoted name does not split the list");
  ExpectFamilies(TargetPage("font-family: \"A\\\"B\""), "target", {"A\"B"},
                 "a backslash escapes a quote inside a quoted name");
  ExpectFamilies(TargetPage("font-family: \"A\\\\B\""), "target", {"A\\B"},
                 "a backslash escapes a backslash inside a quoted name");
  ExpectFamilies(
      TargetPage("font-family: SERIF, Sans-Serif, MONOSPACE, CURSIVE, FANTASY, SYSTEM-UI"),
      "target", {"serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui"},
      "generic families are matched case-insensitively and stored lowercase");
  ExpectFamilies(TargetPage("font-family: ToyFont, my-font_2, 42"), "target",
                 {"ToyFont", "my-font_2", "42"},
                 "non-generic names keep their spelling; digits, '-' and '_' are allowed");
  ExpectFamilies(TargetPage("font-family: Caf\xC3\xA9"), "target", {"Caf\xC3\xA9"},
                 "non-ASCII bytes are allowed in an unquoted family name");
  // Trimming and internal single spaces around the separators.
  ExpectFamilies(TargetPage("font-family:   serif ,   monospace  "), "target",
                 {"serif", "monospace"}, "whitespace around items and commas is trimmed");
}

void TestInvalidValues() {
  const std::vector<std::string> serif = {"serif"};
  ExpectFamilies(TargetPage("color: red"), "target", serif,
                 "without a font-family declaration the initial value is serif");
  ExpectFamilies(TargetPage("font-family: "), "target", serif, "an empty value is ignored");
  ExpectFamilies(TargetPage("font-family: serif,"), "target", serif,
                 "a trailing comma (empty item) invalidates the whole declaration");
  ExpectFamilies(TargetPage("font-family: serif,,monospace"), "target", serif,
                 "an empty item in the middle invalidates the whole declaration");
  ExpectFamilies(TargetPage("font-family: \"\""), "target", serif,
                 "an empty quoted name invalidates the declaration");
  ExpectFamilies(TargetPage("font-family: Toy Font"), "target", serif,
                 "an unquoted space invalidates the declaration");
  ExpectFamilies(TargetPage("font-family: \"unterminated"), "target", serif,
                 "an unterminated quoted name invalidates the declaration");
  ExpectFamilies(TargetPage("font-family: bad?name"), "target", serif,
                 "an invalid name byte invalidates the declaration");
  ExpectFamilies(TargetPage("font-family: monospace; font-family: bad?name"), "target",
                 {"monospace"}, "an invalid later declaration leaves the earlier value in place");
  // The M4 subset does not implement the CSS-wide keywords: they are ignored.
  ExpectFamilies(TargetPage("font-family: monospace; font-family: inherit"), "target",
                 {"monospace"}, "the unsupported inherit keyword is ignored");
  ExpectFamilies(TargetPage("font-family: monospace; font-family: initial"), "target",
                 {"monospace"}, "the unsupported initial keyword is ignored");
  ExpectFamilies(TargetPage("font-family: monospace; font-family: unset"), "target", {"monospace"},
                 "the unsupported unset keyword is ignored");
  // ...but a quoted "inherit" is an ordinary family name.
  ExpectFamilies(TargetPage("font-family: \"inherit\""), "target", {"inherit"},
                 "a quoted CSS-wide keyword is an ordinary family name");
}

void TestCascade() {
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { font-family: serif; } #target { font-family: monospace; }</style>"
        "<div id=target>x</div>");
    ExpectFamilies(document, "target", {"monospace"}, "the later declaration wins");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { font-family: serif !important; } "
        "#target { font-family: monospace; }</style><div id=target>x</div>");
    ExpectFamilies(document, "target", {"serif"}, "!important beats a later normal declaration");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>.c { font-family: serif; } div { font-family: monospace; }</style>"
        "<div id=target class=c>x</div>");
    ExpectFamilies(document, "target", {"serif"}, "a class rule beats a type rule");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { font-family: serif; }</style>"
        "<div id=target style=\"font-family: monospace\">x</div>");
    ExpectFamilies(document, "target", {"monospace"}, "an inline style beats a selector");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { font-family: serif !important; }</style>"
        "<div id=target style=\"font-family: monospace !important\">x</div>");
    ExpectFamilies(document, "target", {"monospace"},
                   "an inline !important declaration wins over a selector !important one");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { font-family: \"Toy Test Font\", sans-serif; }</style>"
        "<div id=target>x</div>");
    ExpectFamilies(document, "target", {"Toy Test Font", "sans-serif"},
                   "a quoted list survives the cascade as a list");
  }
}

void TestInheritance() {
  {
    const Document document = toy::dom::ParseDocument(
        "<div id=outer style=\"font-family: monospace\"><span id=inner>x</span></div>");
    ExpectFamilies(document, "inner", {"monospace"}, "font-family inherits from the parent");
    ExpectFamilies(toy::dom::ParseDocument("<p id=inner>x</p>"), "inner", {"serif"},
                   "the initial font-family is serif");
    ExpectFamilies(toy::dom::ParseDocument("<div id=outer style=\"font-family: monospace\">"
                                           "<span id=inner style=\"font-family: serif\">x</span>"
                                           "</div>"),
                   "inner", {"serif"}, "a child declaration overrides the inherited list");
  }
  {
    // Non-element nodes report the style of their nearest element ancestor.
    const Document document =
        toy::dom::ParseDocument("<div id=outer style=\"font-family: monospace\">text</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId outer = FindElementById(document, "outer");
    NodeId text = document.Get(outer).first_child;
    while (text != toy::dom::kInvalidNode && document.Get(text).type != NodeType::kText) {
      text = document.Get(text).next_sibling;
    }
    toy_test::Check(text != toy::dom::kInvalidNode, "the inheritance fixture has a text node");
    if (text != toy::dom::kInvalidNode) {
      toy_test::Check(styles.Get(text).font_family == std::vector<std::string>{"monospace"},
                      "a text node reports its parent element's font-family");
    }
  }
  {
    const Document document =
        toy::dom::ParseDocument("<body style=\"font-family: monospace\"><p id=child>x</p></body>");
    ExpectFamilies(document, "child", {"monospace"}, "descendants inherit from body");
    ExpectFamilies(document, "child", {"monospace"}, "the inherited list is a list, not a string");
  }
}

void TestUserAgentRules() {
  const Document document = toy::dom::ParseDocument(
      "<b id=b>1</b><strong id=strong>2</strong><i id=i>3</i><em id=em>4</em>"
      "<code id=code>5</code><kbd id=kbd>6</kbd><samp id=samp>7</samp><pre id=pre>8</pre>"
      "<pre><b id=pre_b>9</b></pre>");
  const StyleTree styles = toy::style::ComputeStyles(document);
  const auto weight = [&](std::string_view id) {
    const NodeId node = FindElementById(document, id);
    return node == toy::dom::kInvalidNode ? -1 : styles.Get(node).font_weight;
  };
  const auto italic = [&](std::string_view id) {
    const NodeId node = FindElementById(document, id);
    return node != toy::dom::kInvalidNode && styles.Get(node).font_style == FontStyle::kItalic;
  };
  const auto monospace = [&](std::string_view id) {
    const NodeId node = FindElementById(document, id);
    return node != toy::dom::kInvalidNode &&
           styles.Get(node).font_family == std::vector<std::string>{"monospace"};
  };
  toy_test::Check(weight("b") == 700, "the UA sheet makes b bold");
  toy_test::Check(weight("strong") == 700, "the UA sheet makes strong bold");
  toy_test::Check(italic("i"), "the UA sheet makes i italic");
  toy_test::Check(italic("em"), "the UA sheet makes em italic");
  toy_test::Check(monospace("code"), "the UA sheet makes code monospace");
  toy_test::Check(monospace("kbd"), "the UA sheet makes kbd monospace");
  toy_test::Check(monospace("samp"), "the UA sheet makes samp monospace");
  toy_test::Check(monospace("pre"), "the UA sheet makes pre monospace");
  toy_test::Check(monospace("pre_b"), "b inside pre inherits the monospace family");
  toy_test::Check(weight("pre_b") == 700, "b inside pre is still bold");
  // A non-matching element keeps the inherited initial family.
  toy_test::Check(!monospace("strong"), "strong does not become monospace");
}

}  // namespace

int main() {
  TestParsing();
  TestInvalidValues();
  TestCascade();
  TestInheritance();
  TestUserAgentRules();
  return toy_test::Finish("style font-family test");
}
