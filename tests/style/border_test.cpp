// Tests for the M14b border properties: width/style/color longhands, the
// 1-4 value width/style/color shorthands, the side and full border shorthands
// (with CSS reset-to-initial semantics), used widths and non-inheritance.

#include <cmath>
#include <cstddef>
#include <cstdint>
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
using toy::style::BorderSide;
using toy::style::BorderStyle;
using toy::style::ComputedStyle;
using toy::style::Length;
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
  std::fprintf(stderr, "style border test failed: %s\n  expected: %s\n  actual:   %s\n",
               description, expected.c_str(), actual.c_str());
  ++toy_test::failures;
}

const char* StyleName(BorderStyle style) {
  switch (style) {
    case BorderStyle::kNone:
      return "none";
    case BorderStyle::kHidden:
      return "hidden";
    case BorderStyle::kSolid:
      return "solid";
    case BorderStyle::kDashed:
      return "dashed";
    case BorderStyle::kDotted:
      return "dotted";
    case BorderStyle::kDouble:
      return "double";
    case BorderStyle::kGroove:
      return "groove";
    case BorderStyle::kRidge:
      return "ridge";
    case BorderStyle::kInset:
      return "inset";
    case BorderStyle::kOutset:
      return "outset";
  }
  return "?";
}

// The stored (unused) border width, in px.
void ExpectStoredWidth(const StyleTree& styles, NodeId id, BorderSide ComputedStyle::* member,
                       float expected, const char* description) {
  const BorderSide& side = styles.Get(id).*member;
  if (side.width.unit != Length::Unit::kPx || std::fabs(side.width.value - expected) > 0.01f) {
    Fail(description, std::to_string(expected) + "px",
         std::to_string(side.width.value) + " (unit " +
             std::to_string(static_cast<int>(side.width.unit)) + ")");
  }
}

// The width that participates in layout/painting (0 for none/hidden).
void ExpectUsedWidth(const StyleTree& styles, NodeId id, BorderSide ComputedStyle::* member,
                     float expected, const char* description) {
  const float actual = toy::style::UsedBorderWidth(styles.Get(id).*member);
  if (std::fabs(actual - expected) > 0.01f) {
    Fail(description, std::to_string(expected) + "px", std::to_string(actual) + "px");
  }
}

void ExpectStyle(const StyleTree& styles, NodeId id, BorderSide ComputedStyle::* member,
                 BorderStyle expected, const char* description) {
  const BorderStyle actual = (styles.Get(id).*member).style;
  if (actual != expected) {
    Fail(description, StyleName(expected), StyleName(actual));
  }
}

std::string DescribeColor(const std::optional<std::uint32_t>& color) {
  if (!color) {
    return "currentColor";
  }
  char text[12];
  std::snprintf(text, sizeof(text), "#%08x", *color);
  return text;
}

void ExpectBorderColor(const StyleTree& styles, NodeId id, BorderSide ComputedStyle::* member,
                       std::optional<std::uint32_t> expected, const char* description) {
  const std::optional<std::uint32_t>& actual = (styles.Get(id).*member).color;
  if (actual != expected) {
    Fail(description, DescribeColor(expected), DescribeColor(actual));
  }
}

void ExpectAllSides(const StyleTree& styles, NodeId id, float stored_width_px, BorderStyle style,
                    std::optional<std::uint32_t> color, const char* description) {
  BorderSide ComputedStyle::*const sides[4] = {&ComputedStyle::border_top,
                                               &ComputedStyle::border_right,
                                               &ComputedStyle::border_bottom,
                                               &ComputedStyle::border_left};
  for (BorderSide ComputedStyle::*side : sides) {
    ExpectStoredWidth(styles, id, side, stored_width_px, description);
    ExpectStyle(styles, id, side, style, description);
    ExpectBorderColor(styles, id, side, color, description);
  }
}

void TestWidthLonghands() {
  struct Case {
    const char* value;
    float expected;
    const char* description;
  };
  const Case cases[] = {
      {"1px", 1.0f, "a px width"},
      {"2.5px", 2.5f, "a decimal px width"},
      {"0", 0.0f, "unitless zero is a valid width"},
      {"thin", 1.0f, "thin is 1px"},
      {"medium", 3.0f, "medium is 3px"},
      {"thick", 5.0f, "thick is 5px"},
      {"THICK", 5.0f, "border width keywords are case-insensitive"},
  };
  for (const Case& test : cases) {
    const Document document = TargetPage(std::string("border-top-width: ") + test.value);
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectStoredWidth(styles, FindElementById(document, "target"), &ComputedStyle::border_top,
                      test.expected, test.description);
  }
  // Rejected widths leave the previous value in place.
  const Case rejected[] = {
      {"-2px", 1.0f, "a negative width is rejected"},
      {"50%", 1.0f, "a percentage width is rejected"},
      {"2em", 1.0f, "an em width is rejected"},
      {"2rem", 1.0f, "a rem width is rejected"},
      {"10vw", 1.0f, "an unsupported unit is ignored"},
      {"nonsense", 1.0f, "a malformed width is ignored"},
  };
  for (const Case& test : rejected) {
    const Document document =
        TargetPage(std::string("border-top-width: 1px; border-top-width: ") + test.value);
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectStoredWidth(styles, FindElementById(document, "target"), &ComputedStyle::border_top,
                      test.expected, test.description);
  }
}

void TestStyleLonghands() {
  const BorderStyle all[] = {BorderStyle::kNone,  BorderStyle::kHidden, BorderStyle::kSolid,
                             BorderStyle::kDashed, BorderStyle::kDotted, BorderStyle::kDouble,
                             BorderStyle::kGroove, BorderStyle::kRidge, BorderStyle::kInset,
                             BorderStyle::kOutset};
  for (const BorderStyle style : all) {
    const Document document =
        TargetPage(std::string("border-top-style: ") + StyleName(style));
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectStyle(styles, FindElementById(document, "target"), &ComputedStyle::border_top, style,
                "every border style keyword is accepted");
  }
  {
    const Document document = TargetPage("border-top-style: solid; border-top-style: wavy");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectStyle(styles, FindElementById(document, "target"), &ComputedStyle::border_top,
                BorderStyle::kSolid, "an unknown border style is ignored");
  }
}

void TestColorLonghands() {
  {
    const Document document = TargetPage(
        "border-top-color: #123; border-right-color: rgba(255, 0, 0, 0.5); "
        "border-bottom-color: hsl(120, 100%, 50%); border-left-color: transparent");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectBorderColor(styles, target, &ComputedStyle::border_top, 0xff112233u,
                      "#rgb border color");
    ExpectBorderColor(styles, target, &ComputedStyle::border_right, 0x80ff0000u,
                      "rgba border color keeps its alpha");
    ExpectBorderColor(styles, target, &ComputedStyle::border_bottom, 0xff00ff00u,
                      "hsl border color");
    ExpectBorderColor(styles, target, &ComputedStyle::border_left, 0x00000000u,
                      "transparent is an explicit transparent color");
  }
  {
    // The initial value and the currentColor keyword are both std::nullopt.
    const Document initial = TargetPage("");
    const StyleTree initial_styles = toy::style::ComputeStyles(initial);
    ExpectBorderColor(initial_styles, FindElementById(initial, "target"),
                      &ComputedStyle::border_top, std::nullopt,
                      "the initial border color is currentColor");
    const Document document =
        TargetPage("border-top-color: red; border-top-color: currentColor");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBorderColor(styles, FindElementById(document, "target"), &ComputedStyle::border_top,
                      std::nullopt, "currentColor stores std::nullopt");
    const Document case_document = TargetPage("border-top-color: CURRENTCOLOR");
    const StyleTree case_styles = toy::style::ComputeStyles(case_document);
    ExpectBorderColor(case_styles, FindElementById(case_document, "target"),
                      &ComputedStyle::border_top, std::nullopt,
                      "currentColor is case-insensitive");
  }
  {
    const Document document =
        TargetPage("border-top-color: blue; border-top-color: notacolor");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBorderColor(styles, FindElementById(document, "target"), &ComputedStyle::border_top,
                      0xff0000ffu, "an invalid border color is ignored");
  }
}

// The 1-4 value width/style/color shorthands expand like the margin shorthand.
void TestLonghandShorthands() {
  {
    const Document document = TargetPage("border-width: 1px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectAllSides(styles, target, 1.0f, BorderStyle::kNone, std::nullopt,
                   "a one-value border-width sets all sides");
  }
  {
    const Document document = TargetPage("border-width: 1px 2px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_top, 1.0f, "2-value border-width top");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_right, 2.0f,
                      "2-value border-width right");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_bottom, 1.0f,
                      "2-value border-width bottom");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_left, 2.0f,
                      "2-value border-width left");
  }
  {
    const Document document = TargetPage("border-width: 1px 2px 3px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_top, 1.0f, "3-value border-width top");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_right, 2.0f,
                      "3-value border-width right");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_bottom, 3.0f,
                      "3-value border-width bottom");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_left, 2.0f,
                      "3-value border-width left");
  }
  {
    const Document document = TargetPage("border-width: thin medium thick 4px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_top, 1.0f, "4-value border-width top");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_right, 3.0f,
                      "4-value border-width right");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_bottom, 5.0f,
                      "4-value border-width bottom");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_left, 4.0f,
                      "4-value border-width left");
  }
  {
    // An invalid value makes the whole shorthand a no-op.
    const Document document = TargetPage("border-width: 2px; border-width: 1px 2px bad");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectAllSides(styles, target, 2.0f, BorderStyle::kNone, std::nullopt,
                   "an invalid border-width shorthand is a no-op");
  }
  {
    const Document document = TargetPage("border-style: solid dashed");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectStyle(styles, target, &ComputedStyle::border_top, BorderStyle::kSolid,
                "border-style top");
    ExpectStyle(styles, target, &ComputedStyle::border_right, BorderStyle::kDashed,
                "border-style right");
    ExpectStyle(styles, target, &ComputedStyle::border_bottom, BorderStyle::kSolid,
                "border-style bottom");
    ExpectStyle(styles, target, &ComputedStyle::border_left, BorderStyle::kDashed,
                "border-style left");
  }
  {
    const Document document = TargetPage("border-color: #abc #11223344 currentColor");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectBorderColor(styles, target, &ComputedStyle::border_top, 0xffaabbccu,
                      "border-color top");
    ExpectBorderColor(styles, target, &ComputedStyle::border_right, 0x44112233u,
                      "border-color right");
    ExpectBorderColor(styles, target, &ComputedStyle::border_bottom, std::nullopt,
                      "border-color bottom currentColor");
    ExpectBorderColor(styles, target, &ComputedStyle::border_left, 0x44112233u,
                      "border-color left repeats right");
  }
  {
    // An invalid color makes the whole border-color shorthand a no-op.
    const Document document =
        TargetPage("border-color: red; border-color: blue notacolor");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectAllSides(styles, target, 3.0f, BorderStyle::kNone, 0xffff0000u,
                   "an invalid border-color shorthand is a no-op");
  }
}

// A side shorthand takes width/style/color in any order, resets the missing
// components to their initial values and applies to one side only.
void TestSideShorthands() {
  {
    const Document document =
        TargetPage("border-top: 2px solid red; border-right: thick dashed; "
                   "border-bottom: blue; border-left: 4px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_top, 2.0f, "border-top width");
    ExpectStyle(styles, target, &ComputedStyle::border_top, BorderStyle::kSolid,
                "border-top style");
    ExpectBorderColor(styles, target, &ComputedStyle::border_top, 0xffff0000u,
                      "border-top color");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_right, 5.0f, "border-right width");
    ExpectStyle(styles, target, &ComputedStyle::border_right, BorderStyle::kDashed,
                "border-right style");
    ExpectBorderColor(styles, target, &ComputedStyle::border_right, std::nullopt,
                      "border-right color resets to currentColor");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_bottom, 3.0f,
                      "border-bottom width resets to medium");
    ExpectStyle(styles, target, &ComputedStyle::border_bottom, BorderStyle::kNone,
                "border-bottom style resets to none");
    ExpectBorderColor(styles, target, &ComputedStyle::border_bottom, 0xff0000ffu,
                      "border-bottom color");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_left, 4.0f, "border-left width");
    ExpectStyle(styles, target, &ComputedStyle::border_left, BorderStyle::kNone,
                "border-left style resets to none");
    // The other sides are untouched.
    ExpectStyle(styles, target, &ComputedStyle::border_right, BorderStyle::kDashed,
                "a side shorthand only touches its own side");
  }
  {
    // Reset semantics: a later side shorthand resets the components it omits.
    const Document document = TargetPage("border-top: 4px solid red; border-top: blue");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_top, 3.0f,
                      "an omitted width resets to medium");
    ExpectStyle(styles, target, &ComputedStyle::border_top, BorderStyle::kNone,
                "an omitted style resets to none");
    ExpectBorderColor(styles, target, &ComputedStyle::border_top, 0xff0000ffu,
                      "the provided color is applied");
  }
  {
    // Duplicate or unknown components invalidate the whole declaration,
    // including its reset.
    const Document document =
        TargetPage("border-top: 4px solid red; border-top: 1px 2px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_top, 4.0f,
                      "a duplicate width invalidates the side shorthand");
    ExpectStyle(styles, target, &ComputedStyle::border_top, BorderStyle::kSolid,
                "an invalid side shorthand does not reset the style");
    ExpectBorderColor(styles, target, &ComputedStyle::border_top, 0xffff0000u,
                      "an invalid side shorthand does not reset the color");
    const Document unknown = TargetPage("border-top: 1px wavy red");
    const StyleTree unknown_styles = toy::style::ComputeStyles(unknown);
    ExpectStyle(unknown_styles, FindElementById(unknown, "target"), &ComputedStyle::border_top,
                BorderStyle::kNone, "an unknown component invalidates the side shorthand");
  }
  {
    // Order does not matter.
    const Document document = TargetPage("border-left: red 2px dashed");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_left, 2.0f, "unordered width");
    ExpectStyle(styles, target, &ComputedStyle::border_left, BorderStyle::kDashed,
                "unordered style");
    ExpectBorderColor(styles, target, &ComputedStyle::border_left, 0xffff0000u,
                      "unordered color");
  }
}

void TestFullBorderShorthand() {
  {
    const Document document = TargetPage("border: 2px dashed #123");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectAllSides(styles, FindElementById(document, "target"), 2.0f, BorderStyle::kDashed,
                   0xff112233u, "the border shorthand applies to all four sides");
  }
  {
    const Document document = TargetPage("border: solid");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectAllSides(styles, FindElementById(document, "target"), 3.0f, BorderStyle::kSolid,
                   std::nullopt, "the border shorthand resets omitted components");
  }
  {
    const Document document = TargetPage("border: 2px solid red; border: none");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectAllSides(styles, FindElementById(document, "target"), 3.0f, BorderStyle::kNone,
                   std::nullopt, "border: none resets every side");
    ExpectUsedWidth(styles, FindElementById(document, "target"), &ComputedStyle::border_top, 0.0f,
                    "border: none occupies no space");
  }
  {
    const Document document = TargetPage("border: 1px 2px solid");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectAllSides(styles, FindElementById(document, "target"), 3.0f, BorderStyle::kNone,
                   std::nullopt, "a duplicate component invalidates the border shorthand");
  }
}

void TestUsedWidths() {
  const Document document = TargetPage(
      "border-top: 4px solid; border-right: 4px none; border-bottom: 4px hidden; "
      "border-left: 4px groove");
  const StyleTree styles = toy::style::ComputeStyles(document);
  const NodeId target = FindElementById(document, "target");
  ExpectStoredWidth(styles, target, &ComputedStyle::border_top, 4.0f, "solid keeps its width");
  ExpectUsedWidth(styles, target, &ComputedStyle::border_top, 4.0f, "solid uses its width");
  ExpectUsedWidth(styles, target, &ComputedStyle::border_right, 0.0f, "none uses no width");
  ExpectUsedWidth(styles, target, &ComputedStyle::border_bottom, 0.0f, "hidden uses no width");
  ExpectUsedWidth(styles, target, &ComputedStyle::border_left, 4.0f, "groove uses its width");
}

void TestInitialAndInheritance() {
  {
    // The initial stored width is medium, the style is none and the color is
    // currentColor, so nothing is used until a style is set.
    const Document document = TargetPage("");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectAllSides(styles, target, 3.0f, BorderStyle::kNone, std::nullopt,
                   "borders start at their CSS initial values");
    ExpectUsedWidth(styles, target, &ComputedStyle::border_top, 0.0f,
                    "the initial border occupies no space");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<div id=outer style='border: 5px solid red'><span id=inner>x</span></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId inner = FindElementById(document, "inner");
    ExpectAllSides(styles, inner, 3.0f, BorderStyle::kNone, std::nullopt,
                   "borders are not inherited");
  }
}

void TestCascade() {
  {
    const Document document = toy::dom::ParseDocument(
        "<style>.c { border-top: 2px solid red; } div { border-top: 4px dashed blue; }</style>"
        "<div id=target class=c>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectStoredWidth(styles, FindElementById(document, "target"), &ComputedStyle::border_top,
                      2.0f, "a class rule beats a type rule for border shorthands");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { border-top: 2px solid red !important; }</style>"
        "<div id=target style='border-top: 4px dashed blue'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectStoredWidth(styles, target, &ComputedStyle::border_top, 2.0f,
                      "a selector !important border beats a normal inline one");
    ExpectStyle(styles, target, &ComputedStyle::border_top, BorderStyle::kSolid,
                "a selector !important border style wins");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { border-top: 2px solid red !important; }</style>"
        "<div id=target style='border-top: 4px dashed blue !important'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectStoredWidth(styles, FindElementById(document, "target"), &ComputedStyle::border_top,
                      4.0f, "an inline !important border beats a selector !important one");
  }
}

}  // namespace

int main() {
  TestWidthLonghands();
  TestStyleLonghands();
  TestColorLonghands();
  TestLonghandShorthands();
  TestSideShorthands();
  TestFullBorderShorthand();
  TestUsedWidths();
  TestInitialAndInheritance();
  TestCascade();
  return toy_test::Finish("style border test");
}
