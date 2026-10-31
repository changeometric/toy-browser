// Tests for the M14a typography properties: line-height values and inheritance,
// text-align, visibility and text-decoration (MVP inherited underline).

#include <cmath>
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
using toy::style::LineHeight;
using toy::style::StyleTree;
using toy::style::TextAlign;
using toy::style::Visibility;

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
  std::fprintf(stderr, "style typography test failed: %s\n  expected: %s\n  actual:   %s\n",
               description, expected.c_str(), actual.c_str());
  ++toy_test::failures;
}

const char* LineHeightKindName(LineHeight::Kind kind) {
  switch (kind) {
    case LineHeight::Kind::kNormal:
      return "normal";
    case LineHeight::Kind::kNumber:
      return "number";
    case LineHeight::Kind::kPx:
      return "px";
  }
  return "?";
}

void ExpectLineHeight(const StyleTree& styles, NodeId id, LineHeight::Kind kind, float expected,
                      const char* description) {
  const LineHeight& actual = styles.Get(id).line_height;
  if (actual.kind != kind ||
      (kind != LineHeight::Kind::kNormal && std::fabs(actual.value - expected) > 0.01f)) {
    Fail(description, std::string(LineHeightKindName(kind)) + " " + std::to_string(expected),
         std::string(LineHeightKindName(actual.kind)) + " " + std::to_string(actual.value));
  }
}

void ExpectTextAlign(const StyleTree& styles, NodeId id, TextAlign expected,
                     const char* description) {
  if (styles.Get(id).text_align != expected) {
    Fail(description, "the expected text-align", "another text-align");
  }
}

void ExpectVisibility(const StyleTree& styles, NodeId id, Visibility expected,
                      const char* description) {
  if (styles.Get(id).visibility != expected) {
    Fail(description, "the expected visibility", "another visibility");
  }
}

void ExpectDecorations(const StyleTree& styles, NodeId id, bool underline, bool line_through,
                       bool overline, const char* description) {
  const ComputedStyle& style = styles.Get(id);
  if (style.text_decoration_underline == underline &&
      style.text_decoration_line_through == line_through &&
      style.text_decoration_overline == overline) {
    return;
  }
  const std::string expected = std::string("underline=") + (underline ? "1" : "0") +
                               " line-through=" + (line_through ? "1" : "0") +
                               " overline=" + (overline ? "1" : "0");
  const std::string actual = std::string("underline=") +
                             (style.text_decoration_underline ? "1" : "0") + " line-through=" +
                             (style.text_decoration_line_through ? "1" : "0") + " overline=" +
                             (style.text_decoration_overline ? "1" : "0");
  Fail(description, expected, actual);
}

void TestLineHeightValues() {
  {
    const Document document = TargetPage("line-height: normal");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectLineHeight(styles, FindElementById(document, "target"), LineHeight::Kind::kNormal, 0.0f,
                     "line-height: normal");
  }
  struct Case {
    const char* declarations;
    LineHeight::Kind kind;
    float value;
    const char* description;
  };
  const Case cases[] = {
      {"line-height: 1.5", LineHeight::Kind::kNumber, 1.5f, "a unitless line-height is a number"},
      {"line-height: 2", LineHeight::Kind::kNumber, 2.0f, "an integer line-height is a number"},
      {"line-height: 0", LineHeight::Kind::kNumber, 0.0f, "a zero line-height is a number"},
      {"line-height: 24px", LineHeight::Kind::kPx, 24.0f, "a px line-height is absolute"},
      {"line-height: 2.4px", LineHeight::Kind::kPx, 2.4f, "a decimal px line-height"},
      {"font-size: 10px; line-height: 2em", LineHeight::Kind::kPx, 20.0f,
       "an em line-height resolves against the element's own font size"},
      {"line-height: 150%", LineHeight::Kind::kNumber, 1.5f,
       "a percentage line-height becomes a number"},
      {"line-height: 50%", LineHeight::Kind::kNumber, 0.5f, "a percentage line-height below 1"},
  };
  for (const Case& test : cases) {
    const Document document = TargetPage(test.declarations);
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectLineHeight(styles, FindElementById(document, "target"), test.kind, test.value,
                     test.description);
  }
  // Invalid values are ignored.
  const Case invalid_cases[] = {
      {"line-height: -1", LineHeight::Kind::kNormal, 0.0f, "a negative number is ignored"},
      {"line-height: -10px", LineHeight::Kind::kNormal, 0.0f, "a negative length is ignored"},
      {"line-height: 1.5rem", LineHeight::Kind::kNormal, 0.0f,
       "rem is not part of the line-height contract"},
      {"line-height: 10vh", LineHeight::Kind::kNormal, 0.0f, "an unsupported unit is ignored"},
      {"line-height: abc", LineHeight::Kind::kNormal, 0.0f, "a malformed value is ignored"},
      {"line-height: 24px; line-height: nonsense", LineHeight::Kind::kPx, 24.0f,
       "an invalid later declaration keeps the earlier value"},
  };
  for (const Case& test : invalid_cases) {
    const Document document = TargetPage(test.declarations);
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectLineHeight(styles, FindElementById(document, "target"), test.kind, test.value,
                     test.description);
  }
}

void TestLineHeightInheritanceAndCascade() {
  {
    const Document document = toy::dom::ParseDocument(
        "<div id=outer style='line-height: 1.5'><span id=inner>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectLineHeight(styles, FindElementById(document, "inner"), LineHeight::Kind::kNumber, 1.5f,
                     "a number line-height inherits as a number");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<div id=outer style='line-height: 24px'><span id=inner>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectLineHeight(styles, FindElementById(document, "inner"), LineHeight::Kind::kPx, 24.0f,
                     "a px line-height inherits as computed pixels");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<div id=outer style='line-height: 24px'>"
        "<span id=inner style='line-height: 2'>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectLineHeight(styles, FindElementById(document, "inner"), LineHeight::Kind::kNumber, 2.0f,
                     "a child declaration overrides the inherited line-height");
  }
  {
    // A number inherited by a child with a different font size keeps its kind;
    // layout multiplies it by the child's own font size (contract).
    const Document document = toy::dom::ParseDocument(
        "<div style='line-height: 1.5'><span id=inner style='font-size: 20px'>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectLineHeight(styles, FindElementById(document, "inner"), LineHeight::Kind::kNumber, 1.5f,
                     "an inherited number stays unitless");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { line-height: 1.5 !important; } "
        "#target { line-height: 3; }</style>"
        "<div id=target>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectLineHeight(styles, FindElementById(document, "target"), LineHeight::Kind::kNumber, 1.5f,
                     "!important beats a later normal line-height");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { line-height: 1.5; }</style>"
        "<div id=target style='line-height: 2'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectLineHeight(styles, FindElementById(document, "target"), LineHeight::Kind::kNumber, 2.0f,
                     "an inline line-height beats a selector");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { line-height: 1.5 !important; }</style>"
        "<div id=target style='line-height: 2 !important'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectLineHeight(styles, FindElementById(document, "target"), LineHeight::Kind::kNumber, 2.0f,
                     "an inline !important line-height beats a selector !important");
  }
}

void TestTextAlign() {
  struct Case {
    const char* value;
    TextAlign expected;
    const char* description;
  };
  const Case cases[] = {
      {"left", TextAlign::kLeft, "text-align: left"},
      {"right", TextAlign::kRight, "text-align: right"},
      {"center", TextAlign::kCenter, "text-align: center"},
      {"justify", TextAlign::kJustify, "text-align: justify"},
      {"start", TextAlign::kLeft, "text-align: start maps to left"},
      {"end", TextAlign::kRight, "text-align: end maps to right"},
      {"bogus", TextAlign::kLeft, "an unknown text-align is ignored"},
  };
  for (const Case& test : cases) {
    const Document document = TargetPage(std::string("text-align: ") + test.value);
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectTextAlign(styles, FindElementById(document, "target"), test.expected, test.description);
  }
  // Inherits and can be overridden by the child.
  {
    const Document document = toy::dom::ParseDocument(
        "<div id=outer style='text-align: center'><span id=inner>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectTextAlign(styles, FindElementById(document, "inner"), TextAlign::kCenter,
                    "text-align inherits");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<div style='text-align: center'><span id=inner style='text-align: right'>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectTextAlign(styles, FindElementById(document, "inner"), TextAlign::kRight,
                    "a child text-align overrides the inherited value");
  }
}

void TestVisibility() {
  {
    const Document document = TargetPage("visibility: hidden");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectVisibility(styles, FindElementById(document, "target"), Visibility::kHidden,
                     "visibility: hidden");
  }
  {
    const Document document = TargetPage("visibility: visible");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectVisibility(styles, FindElementById(document, "target"), Visibility::kVisible,
                     "visibility: visible");
  }
  {
    const Document document = TargetPage("visibility: collapsed");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectVisibility(styles, FindElementById(document, "target"), Visibility::kVisible,
                     "an unknown visibility is ignored");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<div id=outer style='visibility: hidden'><span id=inner>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectVisibility(styles, FindElementById(document, "inner"), Visibility::kHidden,
                     "visibility inherits");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<div style='visibility: hidden'>"
        "<span id=inner style='visibility: visible'>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectVisibility(styles, FindElementById(document, "inner"), Visibility::kVisible,
                     "a child visibility overrides the inherited value");
  }
}

void TestTextDecoration() {
  struct Case {
    const char* value;
    bool underline;
    bool line_through;
    bool overline;
    const char* description;
  };
  const Case cases[] = {
      {"underline", true, false, false, "text-decoration: underline"},
      {"none", false, false, false, "text-decoration: none clears every line"},
      {"line-through", false, true, false, "text-decoration: line-through sets its flag"},
      {"overline", false, false, true, "text-decoration: overline sets its flag"},
      {"underline line-through", true, true, false, "a combined token list sets both flags"},
      {"underline overline", true, false, true, "a combined token list sets both flags"},
      {"underline line-through overline", true, true, true, "all three lines at once"},
      {"UNDERLINE LINE-THROUGH", true, true, false,
       "text-decoration values are case-insensitive"},
      {"underline dotted", true, false, false, "a style component keeps the underline"},
      {"underline red", true, false, false, "a color component keeps the underline"},
      {"none underline", true, false, false,
       "the declaration resets then sets the provided line token"},
      {"blink", false, false, false, "an unknown line token is tolerated and resets the flags"},
      {"wavy", false, false, false,
       "a style-only value resets the flags (the declaration resets first)"},
  };
  for (const Case& test : cases) {
    const Document document = TargetPage(std::string("text-decoration: ") + test.value);
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectDecorations(styles, FindElementById(document, "target"), test.underline,
                      test.line_through, test.overline, test.description);
  }
  {
    // Every declaration resets the three flags, so a later token list without a
    // line value clears an earlier declaration.
    const Document document = TargetPage("text-decoration: underline; text-decoration: wavy");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectDecorations(styles, FindElementById(document, "target"), false, false, false,
                      "a later token list resets the previously applied decoration");
  }
  {
    const Document document =
        TargetPage("text-decoration: underline line-through; text-decoration: none");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectDecorations(styles, FindElementById(document, "target"), false, false, false,
                      "a later none clears every decoration line");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<div id=outer style='text-decoration: underline line-through overline'>"
        "<span id=inner>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectDecorations(styles, FindElementById(document, "inner"), true, true, true,
                      "all three MVP decoration flags inherit to descendants");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<div style='text-decoration: underline line-through'>"
        "<span id=inner style='text-decoration: none'>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectDecorations(styles, FindElementById(document, "inner"), false, false, false,
                      "a child text-decoration: none clears the inherited lines");
  }
  {
    // A child declaration resets the inherited flags, so only the listed line is
    // set even if the parent had more.
    const Document document = toy::dom::ParseDocument(
        "<div style='text-decoration: underline line-through'>"
        "<span id=inner style='text-decoration: overline'>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectDecorations(styles, FindElementById(document, "inner"), false, false, true,
                      "a child decoration resets the inherited flags");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { text-decoration: underline !important; }</style>"
        "<div id=target style='text-decoration: none'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectDecorations(styles, FindElementById(document, "target"), true, false, false,
                      "a selector !important underline beats a normal inline none");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { text-decoration: underline !important; }</style>"
        "<div id=target style='text-decoration: none !important'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectDecorations(styles, FindElementById(document, "target"), false, false, false,
                      "an inline !important none beats a selector !important underline");
  }
}

}  // namespace

int main() {
  TestLineHeightValues();
  TestLineHeightInheritanceAndCascade();
  TestTextAlign();
  TestVisibility();
  TestTextDecoration();
  return toy_test::Finish("style typography test");
}
