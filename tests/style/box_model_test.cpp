// Tests for the M14b box-sizing and overflow properties: shorthands,
// longhands, invalid values, cascade and non-inheritance.

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
using toy::style::BoxSizing;
using toy::style::ComputedStyle;
using toy::style::Overflow;
using toy::style::StyleTree;

std::vector<NodeId> CollectElements(const Document& document) {
  std::vector<NodeId> elements;
  const NodeId root = document.root();
  if (root == toy::dom::kInvalidNode) {
    return elements;
  }
  std::vector<NodeId> pending;
  pending.push_back(root);
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

NodeId FindElementById(const Document& document, std::string_view id) {
  for (const NodeId element : CollectElements(document)) {
    for (const toy::dom::Attribute& attribute : document.Get(element).attributes) {
      if (attribute.name == "id" && attribute.value == id) {
        return element;
      }
    }
  }
  return toy::dom::kInvalidNode;
}

Document TargetPage(const std::string& declarations) {
  return toy::dom::ParseDocument("<div id=target style='" + declarations + "'>x</div>");
}

void Fail(const char* description, const std::string& expected, const std::string& actual) {
  std::fprintf(stderr, "style box model test failed: %s\n  expected: %s\n  actual:   %s\n",
               description, expected.c_str(), actual.c_str());
  ++toy_test::failures;
}

const char* BoxSizingName(BoxSizing box_sizing) {
  return box_sizing == BoxSizing::kBorderBox ? "border-box" : "content-box";
}

const char* OverflowName(Overflow overflow) {
  return overflow == Overflow::kHidden ? "hidden" : "visible";
}

void ExpectBoxSizing(const StyleTree& styles, NodeId id, BoxSizing expected,
                     const char* description) {
  const BoxSizing actual = styles.Get(id).box_sizing;
  if (actual != expected) {
    Fail(description, BoxSizingName(expected), BoxSizingName(actual));
  }
}

void ExpectOverflow(const StyleTree& styles, NodeId id, Overflow expected_x, Overflow expected_y,
                    const char* description) {
  const ComputedStyle& style = styles.Get(id);
  if (style.overflow_x != expected_x || style.overflow_y != expected_y) {
    Fail(description,
         std::string("x=") + OverflowName(expected_x) + " y=" + OverflowName(expected_y),
         std::string("x=") + OverflowName(style.overflow_x) + " y=" +
             OverflowName(style.overflow_y));
  }
}

void TestBoxSizing() {
  {
    const Document document = TargetPage("");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBoxSizing(styles, FindElementById(document, "target"), BoxSizing::kContentBox,
                    "the initial box-sizing is content-box");
  }
  {
    const Document document = TargetPage("box-sizing: border-box");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBoxSizing(styles, FindElementById(document, "target"), BoxSizing::kBorderBox,
                    "box-sizing: border-box");
  }
  {
    const Document document = TargetPage("box-sizing: content-box");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBoxSizing(styles, FindElementById(document, "target"), BoxSizing::kContentBox,
                    "box-sizing: content-box");
  }
  {
    const Document document = TargetPage("box-sizing: BORDER-BOX");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBoxSizing(styles, FindElementById(document, "target"), BoxSizing::kBorderBox,
                    "box-sizing keywords are case-insensitive");
  }
  {
    const Document document = TargetPage("box-sizing: border-box; box-sizing: padding-box");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBoxSizing(styles, FindElementById(document, "target"), BoxSizing::kBorderBox,
                    "an unknown box-sizing is ignored");
  }
}

void TestOverflowLonghands() {
  {
    const Document document = TargetPage("");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kVisible,
                   Overflow::kVisible, "the initial overflow is visible on both axes");
  }
  {
    const Document document = TargetPage("overflow-x: hidden");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kHidden,
                   Overflow::kVisible, "overflow-x only hides the x axis");
  }
  {
    const Document document = TargetPage("overflow-y: hidden");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kVisible,
                   Overflow::kHidden, "overflow-y only hides the y axis");
  }
  {
    const Document document = TargetPage("overflow-x: HIDDEN; overflow-y: VISIBLE");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kHidden,
                   Overflow::kVisible, "overflow longhand keywords are case-insensitive");
  }
  {
    const Document document =
        TargetPage("overflow-x: hidden; overflow-x: scroll; overflow-y: auto");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kHidden,
                   Overflow::kVisible, "unsupported overflow keywords are ignored");
  }
}

void TestOverflowShorthand() {
  {
    const Document document = TargetPage("overflow: hidden");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kHidden,
                   Overflow::kHidden, "a one-value overflow sets both axes");
  }
  {
    const Document document = TargetPage("overflow: hidden visible");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kHidden,
                   Overflow::kVisible, "a two-value overflow is x then y");
  }
  {
    const Document document = TargetPage("overflow: visible hidden");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kVisible,
                   Overflow::kHidden, "a two-value overflow is x then y");
  }
  {
    const Document document = TargetPage("overflow: HIDDEN");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kHidden,
                   Overflow::kHidden, "overflow shorthand keywords are case-insensitive");
  }
  {
    // An unsupported keyword makes the whole shorthand a no-op.
    const Document document = TargetPage("overflow: hidden; overflow: scroll");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kHidden,
                   Overflow::kHidden, "an unsupported overflow shorthand is a no-op");
  }
  {
    const Document document = TargetPage("overflow: visible; overflow: hidden scroll");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kVisible,
                   Overflow::kVisible, "an invalid value makes the overflow shorthand a no-op");
  }
  {
    const Document document = TargetPage("overflow: visible; overflow: hidden visible extra");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kVisible,
                   Overflow::kVisible, "a three-value overflow is invalid");
  }
  {
    // The longhands and the shorthand share one cascade.
    const Document document = TargetPage("overflow: hidden; overflow-y: visible");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kHidden,
                   Overflow::kVisible, "a later overflow-y longhand beats the shorthand");
  }
}

void TestNonInheritance() {
  const Document document = toy::dom::ParseDocument(
      "<div id=outer style='box-sizing: border-box; overflow: hidden'>"
      "<span id=inner>x</span></div>");
  const StyleTree styles = toy::style::ComputeStyles(document);
  const NodeId inner = FindElementById(document, "inner");
  ExpectBoxSizing(styles, inner, BoxSizing::kContentBox, "box-sizing is not inherited");
  ExpectOverflow(styles, inner, Overflow::kVisible, Overflow::kVisible,
                 "overflow is not inherited");
}

void TestCascade() {
  {
    const Document document = toy::dom::ParseDocument(
        "<style>.c { box-sizing: border-box; } div { box-sizing: content-box; }</style>"
        "<div id=target class=c>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBoxSizing(styles, FindElementById(document, "target"), BoxSizing::kBorderBox,
                    "a class rule beats a type rule for box-sizing");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { box-sizing: border-box !important; }</style>"
        "<div id=target style='box-sizing: content-box'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBoxSizing(styles, FindElementById(document, "target"), BoxSizing::kBorderBox,
                    "a selector !important box-sizing beats normal inline");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { overflow: hidden; }</style>"
        "<div id=target style='overflow: visible'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kVisible,
                   Overflow::kVisible, "an inline overflow beats a selector");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { overflow-x: hidden !important; }</style>"
        "<div id=target style='overflow-x: visible !important'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOverflow(styles, FindElementById(document, "target"), Overflow::kVisible,
                   Overflow::kVisible, "an inline !important overflow beats a selector one");
  }
}

}  // namespace

int main() {
  TestBoxSizing();
  TestOverflowLonghands();
  TestOverflowShorthand();
  TestNonInheritance();
  TestCascade();
  return toy_test::Finish("style box model test");
}
