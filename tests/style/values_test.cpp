// Tests for the M14a CSS value batch: units (%/rem/em/px/unitless zero),
// font-size resolution, colors (hex/rgb/hsl/named/alpha), the background
// shorthand color component, logical property mapping, auto margins and
// width/height/min/max constraints.

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
using toy::style::AutoLength;
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

NodeId FindFirstElementByTag(const Document& document, std::string_view tag) {
  for (const NodeId element : CollectElements(document)) {
    if (document.Get(element).name == tag) {
      return element;
    }
  }
  return toy::dom::kInvalidNode;
}

// A page whose `<div id=target>` carries `declarations` as its inline style. The
// style attribute uses single quotes so double-quoted values are allowed.
Document TargetPage(const std::string& declarations) {
  return toy::dom::ParseDocument("<div id=target style='" + declarations + "'>x</div>");
}

void Fail(const char* description, const std::string& expected, const std::string& actual) {
  std::fprintf(stderr, "style values test failed: %s\n  expected: %s\n  actual:   %s\n",
               description, expected.c_str(), actual.c_str());
  ++toy_test::failures;
}

std::string DescribeLength(const std::optional<Length>& length) {
  if (!length) {
    return "auto/none";
  }
  return std::to_string(length->value) +
         (length->unit == Length::Unit::kPercent ? "%" : "px");
}

void ExpectLengthPx(const StyleTree& styles, NodeId id, Length ComputedStyle::* member,
                    float expected, const char* description) {
  const Length& actual = styles.Get(id).*member;
  if (actual.unit != Length::Unit::kPx || std::fabs(actual.value - expected) > 0.01f) {
    Fail(description, std::to_string(expected) + "px",
         std::to_string(actual.value) + (actual.unit == Length::Unit::kPercent ? "%" : "px"));
  }
}

void ExpectLengthPercent(const StyleTree& styles, NodeId id, Length ComputedStyle::* member,
                         float expected, const char* description) {
  const Length& actual = styles.Get(id).*member;
  if (actual.unit != Length::Unit::kPercent || std::fabs(actual.value - expected) > 0.01f) {
    Fail(description, std::to_string(expected) + "%",
         std::to_string(actual.value) + (actual.unit == Length::Unit::kPercent ? "%" : "px"));
  }
}

void ExpectOptionalLengthPx(const StyleTree& styles, NodeId id,
                            std::optional<Length> ComputedStyle::* member, float expected,
                            const char* description) {
  const std::optional<Length>& actual = styles.Get(id).*member;
  if (!actual || actual->unit != Length::Unit::kPx ||
      std::fabs(actual->value - expected) > 0.01f) {
    Fail(description, std::to_string(expected) + "px", DescribeLength(actual));
  }
}

void ExpectOptionalLengthPercent(const StyleTree& styles, NodeId id,
                                 std::optional<Length> ComputedStyle::* member, float expected,
                                 const char* description) {
  const std::optional<Length>& actual = styles.Get(id).*member;
  if (!actual || actual->unit != Length::Unit::kPercent ||
      std::fabs(actual->value - expected) > 0.01f) {
    Fail(description, std::to_string(expected) + "%", DescribeLength(actual));
  }
}

void ExpectNoLength(const StyleTree& styles, NodeId id,
                    std::optional<Length> ComputedStyle::* member, const char* description) {
  if ((styles.Get(id).*member).has_value()) {
    Fail(description, "auto/none", DescribeLength(styles.Get(id).*member));
  }
}

void ExpectMarginPx(const StyleTree& styles, NodeId id, AutoLength ComputedStyle::* member,
                    float expected, const char* description) {
  const AutoLength& actual = styles.Get(id).*member;
  if (actual.is_auto || actual.length.unit != Length::Unit::kPx ||
      std::fabs(actual.length.value - expected) > 0.01f) {
    Fail(description, std::to_string(expected) + "px",
         actual.is_auto ? "auto" : DescribeLength(actual.length));
  }
}

void ExpectMarginAuto(const StyleTree& styles, NodeId id, AutoLength ComputedStyle::* member,
                      const char* description) {
  if (!(styles.Get(id).*member).is_auto) {
    Fail(description, "auto", "a length");
  }
}

void ExpectColor(const StyleTree& styles, NodeId id, std::uint32_t expected,
                 const char* description) {
  const std::uint32_t actual = styles.Get(id).color;
  if (actual != expected) {
    char expected_text[8];
    char actual_text[8];
    std::snprintf(expected_text, sizeof(expected_text), "#%06x", expected);
    std::snprintf(actual_text, sizeof(actual_text), "#%06x", actual);
    Fail(description, expected_text, actual_text);
  }
}

void ExpectBackgroundColor(const StyleTree& styles, NodeId id, std::uint32_t expected_argb,
                           const char* description) {
  const std::uint32_t actual = styles.Get(id).background_color;
  if (actual != expected_argb) {
    char expected_text[10];
    char actual_text[10];
    std::snprintf(expected_text, sizeof(expected_text), "#%08x", expected_argb);
    std::snprintf(actual_text, sizeof(actual_text), "#%08x", actual);
    Fail(description, expected_text, actual_text);
  }
}

void ExpectFontSize(const StyleTree& styles, NodeId id, float expected, const char* description) {
  const float actual = styles.Get(id).font_size_px;
  if (std::fabs(actual - expected) > 0.01f) {
    Fail(description, std::to_string(expected), std::to_string(actual));
  }
}

// Non-zero unitless lengths and unsupported units are ignored.
void TestLengthUnits() {
  {
    const Document document =
        TargetPage("margin: 10px 2.5px -4px 0; padding: 1em 2rem 0 50%; "
                   "width: 25.5%; height: 2em; font-size: 10px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_top, 10.0f, "px margin");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_right, 2.5f, "decimal px margin");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_bottom, -4.0f, "negative px margin");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_left, 0.0f, "unitless zero margin");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_top, 10.0f, "em padding uses own size");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_right, 32.0f,
                   "rem padding uses the root size");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_bottom, 0.0f,
                   "the repeated value of a 3-value padding keeps px zero");
    ExpectLengthPercent(styles, target, &ComputedStyle::padding_left, 50.0f,
                        "percentage padding stays unresolved");
    ExpectOptionalLengthPercent(styles, target, &ComputedStyle::width, 25.5f,
                                "decimal percentage width stays unresolved");
    ExpectOptionalLengthPx(styles, target, &ComputedStyle::height, 20.0f,
                           "em height uses own size");
  }
  // Unsupported values leave the earlier value in place.
  {
    const Document document = TargetPage(
        "margin-top: 5px; margin-top: 3vh; width: 10ex; width: 7px; "
        "padding-top: 4; padding-top: 2px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_top, 5.0f,
                   "an unsupported unit is ignored");
    ExpectOptionalLengthPx(styles, target, &ComputedStyle::width, 7.0f,
                           "an unsupported width unit is ignored");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_top, 2.0f,
                   "a non-zero unitless length is invalid");
  }
  // The root element's own rem values use the 16px initial size; descendants
  // use the root's computed size.
  {
    const Document document = toy::dom::ParseDocument(
        "<html style='font-size: 20px; margin-top: 1rem'>"
        "<body><div id=child style='margin-top: 1rem'>x</div></body></html>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId html = FindFirstElementByTag(document, "html");
    const NodeId child = FindElementById(document, "child");
    ExpectMarginPx(styles, html, &ComputedStyle::margin_top, 16.0f,
                   "the root element's own rem uses the initial size");
    ExpectMarginPx(styles, child, &ComputedStyle::margin_top, 20.0f,
                   "a descendant's rem uses the root's computed size");
  }
}

// font-size percentages/rem/em resolution.
void TestFontSizeUnits() {
  // The root's percentage font size is relative to the 16px initial size.
  {
    const Document document =
        toy::dom::ParseDocument("<html style='font-size: 150%'><body></body></html>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectFontSize(styles, FindFirstElementByTag(document, "html"), 24.0f,
                   "the root percentage font size uses the initial size");
  }
  // The root's rem font size is relative to the 16px initial size.
  {
    const Document document =
        toy::dom::ParseDocument("<html style='font-size: 2rem'><body></body></html>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectFontSize(styles, FindFirstElementByTag(document, "html"), 32.0f,
                   "the root rem font size uses the initial size");
  }
  // Descendants resolve rem against the root's computed size and em/% against
  // the parent's computed size.
  {
    const Document document = toy::dom::ParseDocument(
        "<html style='font-size: 20px'><body>"
        "<div id=rem style='font-size: 2rem'>x</div>"
        "<div id=em style='font-size: 10px'><p id=child style='font-size: 2em'>y</p>"
        "<p id=percent style='font-size: 50%'>z</p></div>"
        "</body></html>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectFontSize(styles, FindElementById(document, "rem"), 40.0f,
                   "a descendant rem font size uses the root's computed size");
    ExpectFontSize(styles, FindElementById(document, "child"), 20.0f,
                   "an em font size uses the parent's computed size");
    ExpectFontSize(styles, FindElementById(document, "percent"), 5.0f,
                   "a percentage font size uses the parent's computed size");
  }
  // Invalid font sizes are ignored; the last valid declaration wins.
  {
    const Document document =
        TargetPage("font-size: 10px; font-size: 200%; font-size: -5px; font-size: 3em");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectFontSize(styles, FindElementById(document, "target"), 48.0f,
                   "a later valid em font size wins and negative sizes are ignored");
  }
}

// Text color (alpha dropped) and background color (alpha kept).
void TestColors() {
  const Document document = toy::dom::ParseDocument(
      "<style>"
      "#hex3 { color: #abc; background-color: #abc; }"
      "#hex4 { color: #abcd; background-color: #abcd; }"
      "#hex6 { color: #aabbcc; background-color: #aabbcc; }"
      "#hex8 { color: #aabbccdd; background-color: #aabbccdd; }"
      "#rgb { color: rgb(1, 2, 3); background-color: rgb(1, 2, 3); }"
      "#rgbpercent { color: rgb(100%, 0%, 50%); background-color: rgb(100%, 0%, 50%); }"
      "#rgba { color: rgba(255, 0, 0, 0.5); background-color: rgba(255, 0, 0, 0.5); }"
      "#rgba255 { color: rgb(255, 0, 0, 128); background-color: rgb(255, 0, 0, 128); }"
      "#hsl { color: hsl(120, 100%, 50%); background-color: hsl(120, 100%, 50%); }"
      "#hsl2 { color: hsl(0, 100%, 25%); background-color: hsla(0, 100%, 25%, 0.25); }"
      "#named { color: rebeccapurple; background-color: aliceblue; }"
      "#orange { color: orange; background-color: transparent; }"
      "#clamp { color: rgb(300, -5, 127.5); background-color: rgb(300, -5, 127.5); }"
      "</style>"
      "<div id=hex3>x</div><div id=hex4>x</div><div id=hex6>x</div><div id=hex8>x</div>"
      "<div id=rgb>x</div><div id=rgbpercent>x</div><div id=rgba>x</div>"
      "<div id=rgba255>x</div><div id=hsl>x</div><div id=hsl2>x</div>"
      "<div id=named>x</div><div id=orange>x</div><div id=clamp>x</div>");
  const StyleTree styles = toy::style::ComputeStyles(document);
  const auto node = [&](std::string_view id) { return FindElementById(document, id); };
  const auto check_color = [&](std::string_view id, std::uint32_t expected) {
    ExpectColor(styles, node(id), expected, "text color value");
  };
  const auto check_background = [&](std::string_view id, std::uint32_t expected) {
    ExpectBackgroundColor(styles, node(id), expected, "background color value");
  };

  check_color("hex3", 0xaabbccu);
  check_background("hex3", 0xffaabbccu);
  check_color("hex4", 0xaabbccu);
  check_background("hex4", 0xddaabbccu);
  check_color("hex6", 0xaabbccu);
  check_background("hex6", 0xffaabbccu);
  check_color("hex8", 0xaabbccu);
  check_background("hex8", 0xddaabbccu);

  check_color("rgb", 0x010203u);
  check_background("rgb", 0xff010203u);
  check_color("rgbpercent", 0xff0080u);
  check_background("rgbpercent", 0xffff0080u);
  check_color("rgba", 0xff0000u);  // Alpha is dropped for text.
  check_background("rgba", 0x80ff0000u);  // ...but kept for backgrounds.
  check_color("rgba255", 0xff0000u);
  check_background("rgba255", 0x80ff0000u);

  check_color("hsl", 0x00ff00u);
  check_background("hsl", 0xff00ff00u);
  check_color("hsl2", 0x800000u);
  check_background("hsl2", 0x40800000u);

  check_color("named", 0x663399u);
  check_background("named", 0xfff0f8ffu);
  check_color("orange", 0xffa500u);
  check_background("orange", 0x00000000u);
  check_color("clamp", 0xff0080u);
  check_background("clamp", 0xffff0080u);

  // Invalid colors leave the previous value in place.
  {
    const Document invalid = TargetPage(
        "color: #123; color: notacolor; background-color: #112233; "
        "background-color: #12345");
    const StyleTree invalid_styles = toy::style::ComputeStyles(invalid);
    const NodeId target = FindElementById(invalid, "target");
    ExpectColor(invalid_styles, target, 0x112233u, "an unknown color is ignored");
    ExpectBackgroundColor(invalid_styles, target, 0xff112233u,
                          "a malformed hex color is ignored");
  }
}

// The `background` shorthand sets only its color component; `transparent`
// resets the background alpha, a component-less value leaves it unchanged.
void TestBackgroundShorthand() {
  {
    const Document document = TargetPage("background: transparent");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBackgroundColor(styles, FindElementById(document, "target"), 0x00000000u,
                          "background: transparent is the transparent color");
  }
  {
    const Document document = TargetPage("background: #102030");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBackgroundColor(styles, FindElementById(document, "target"), 0xff102030u,
                          "a color-only background shorthand applies the color");
  }
  {
    // A function value is one token even with internal whitespace, and the
    // color may appear after other components.
    const Document document = TargetPage(
        "background: url(x.png) no-repeat center rgb(1, 2, 3)");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBackgroundColor(styles, FindElementById(document, "target"), 0xff010203u,
                          "the first color component is applied wherever it appears");
  }
  {
    const Document document = TargetPage("background: #10203080 url(\"a b.png\")");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBackgroundColor(styles, FindElementById(document, "target"), 0x80102030u,
                          "a color-first background shorthand keeps its alpha");
  }
  {
    // Without a color component the shorthand is a no-op.
    const Document document =
        TargetPage("background-color: blue; background: url(x.png) no-repeat");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBackgroundColor(styles, FindElementById(document, "target"), 0xff0000ffu,
                          "a color-less background shorthand leaves the background");
  }
  {
    // A later `background-color` still wins over an earlier shorthand.
    const Document document = TargetPage("background: #112233; background-color: #445566");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectBackgroundColor(styles, FindElementById(document, "target"), 0xff445566u,
                          "a later background-color beats the shorthand color");
  }
}

// Logical properties map to their physical horizontal-tb equivalents.
void TestLogicalProperties() {
  {
    const Document document = TargetPage(
        "margin-block-start: 1px; margin-block-end: 2px; "
        "margin-inline-start: 3px; margin-inline-end: 4px; "
        "padding-block-start: 5px; padding-block-end: 6px; "
        "padding-inline-start: 7px; padding-inline-end: 8px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_top, 1.0f, "margin-block-start -> top");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_bottom, 2.0f,
                   "margin-block-end -> bottom");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_left, 3.0f,
                   "margin-inline-start -> left");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_right, 4.0f,
                   "margin-inline-end -> right");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_top, 5.0f,
                   "padding-block-start -> top");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_bottom, 6.0f,
                   "padding-block-end -> bottom");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_left, 7.0f,
                   "padding-inline-start -> left");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_right, 8.0f,
                   "padding-inline-end -> right");
  }
  {
    // One-value logical shorthands set both ends.
    const Document document =
        TargetPage("margin-block: 1px; margin-inline: auto; padding-block: 2px; "
                   "padding-inline: 3px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_top, 1.0f, "margin-block one value");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_bottom, 1.0f,
                   "margin-block one value repeats");
    ExpectMarginAuto(styles, target, &ComputedStyle::margin_left, "margin-inline auto -> left");
    ExpectMarginAuto(styles, target, &ComputedStyle::margin_right, "margin-inline auto -> right");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_top, 2.0f, "padding-block one value");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_bottom, 2.0f,
                   "padding-block one value repeats");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_left, 3.0f, "padding-inline one value");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_right, 3.0f,
                   "padding-inline one value repeats");
  }
  {
    // Two-value logical shorthands are start then end.
    const Document document =
        TargetPage("margin-block: 1px 2px; margin-inline: 3px 4px; "
                   "padding-block: 5px 6px; padding-inline: 7px 8px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_top, 1.0f, "margin-block start");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_bottom, 2.0f, "margin-block end");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_left, 3.0f, "margin-inline start");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_right, 4.0f, "margin-inline end");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_top, 5.0f, "padding-block start");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_bottom, 6.0f, "padding-block end");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_left, 7.0f, "padding-inline start");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_right, 8.0f, "padding-inline end");
  }
  {
    // Size logical properties map to physical width/height and constraints.
    const Document document =
        TargetPage("font-size: 10px; inline-size: 50%; block-size: 10rem; "
                   "min-inline-size: 2em; max-inline-size: 30%; "
                   "min-block-size: 5px; max-block-size: none");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectOptionalLengthPercent(styles, target, &ComputedStyle::width, 50.0f,
                                "inline-size -> width");
    ExpectOptionalLengthPx(styles, target, &ComputedStyle::height, 160.0f,
                           "block-size -> height");
    ExpectOptionalLengthPx(styles, target, &ComputedStyle::min_width, 20.0f,
                           "min-inline-size -> min-width");
    ExpectOptionalLengthPercent(styles, target, &ComputedStyle::max_width, 30.0f,
                                "max-inline-size -> max-width");
    ExpectOptionalLengthPx(styles, target, &ComputedStyle::min_height, 5.0f,
                           "min-block-size -> min-height");
    ExpectNoLength(styles, target, &ComputedStyle::max_height, "max-block-size: none");
  }
  {
    // Physical and logical longhands share one cascade: the later declaration
    // wins regardless of which spelling it uses.
    const Document document = TargetPage(
        "margin-block-start: 1px; margin-top: 2px; "
        "padding-top: 3px; padding-block-start: 4px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_top, 2.0f,
                   "a later physical longhand overrides an earlier logical one");
    ExpectLengthPx(styles, target, &ComputedStyle::padding_top, 4.0f,
                   "a later logical longhand overrides an earlier physical one");
  }
}

// auto margins, width/height auto/%/length and the min/max constraints.
void TestMarginsAndSizes() {
  {
    const Document document = TargetPage(
        "margin: auto; width: auto; height: 2em; font-size: 10px; "
        "min-width: 12px; max-width: 30%; min-height: none; max-height: 40px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectMarginAuto(styles, target, &ComputedStyle::margin_top, "margin: auto -> top");
    ExpectMarginAuto(styles, target, &ComputedStyle::margin_right, "margin: auto -> right");
    ExpectMarginAuto(styles, target, &ComputedStyle::margin_bottom, "margin: auto -> bottom");
    ExpectMarginAuto(styles, target, &ComputedStyle::margin_left, "margin: auto -> left");
    ExpectNoLength(styles, target, &ComputedStyle::width, "width: auto");
    ExpectOptionalLengthPx(styles, target, &ComputedStyle::height, 20.0f, "height: 2em");
    ExpectOptionalLengthPx(styles, target, &ComputedStyle::min_width, 12.0f, "min-width px");
    ExpectOptionalLengthPercent(styles, target, &ComputedStyle::max_width, 30.0f,
                                "max-width percentage");
    ExpectNoLength(styles, target, &ComputedStyle::min_height, "min-height: none");
    ExpectOptionalLengthPx(styles, target, &ComputedStyle::max_height, 40.0f, "max-height px");
  }
  {
    // Mixed auto margins and a 1-4 value shorthand.
    const Document document = TargetPage("margin: auto 10px 20px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectMarginAuto(styles, target, &ComputedStyle::margin_top, "three-value margin top auto");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_right, 10.0f,
                   "three-value margin right");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_bottom, 20.0f,
                   "three-value margin bottom");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_left, 10.0f,
                   "three-value margin left repeats right");
  }
  {
    // Invalid shorthand values leave every side unchanged.
    const Document document =
        TargetPage("margin: 3px; margin: 1px broken; margin-left: auto; width: 9px");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectMarginPx(styles, target, &ComputedStyle::margin_top, 3.0f,
                   "an invalid margin shorthand is a no-op");
    ExpectMarginAuto(styles, target, &ComputedStyle::margin_left,
                     "a later auto longhand still applies");
    ExpectOptionalLengthPx(styles, target, &ComputedStyle::width, 9.0f,
                           "later declarations still apply after a no-op");
  }
  {
    // Invalid width/height values keep the earlier value.
    const Document document = TargetPage(
        "width: 10px; width: auto; width: nonsense; height: 4px; height: 5ex");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectNoLength(styles, target, &ComputedStyle::width,
                   "width: auto resets the value and nonsense is ignored");
    ExpectOptionalLengthPx(styles, target, &ComputedStyle::height, 4.0f,
                           "an unsupported height unit is ignored");
  }
}

// New properties follow the existing cascade rules (specificity, !important,
// inline).
void TestCascadeOrder() {
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { margin-left: auto !important; width: 50% !important; }</style>"
        "<div id=target style='margin-left: 5px; width: 10px'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId target = FindElementById(document, "target");
    ExpectMarginAuto(styles, target, &ComputedStyle::margin_left,
                     "a selector !important margin beats a normal inline one");
    ExpectOptionalLengthPercent(styles, target, &ComputedStyle::width, 50.0f,
                                "a selector !important width beats a normal inline one");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#target { width: 50% !important; }</style>"
        "<div id=target style='width: 10px !important'>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOptionalLengthPx(styles, FindElementById(document, "target"), &ComputedStyle::width,
                           10.0f, "an inline !important wins over a selector !important");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>.c { max-width: 10px; } div { max-width: 20px; }</style>"
        "<div id=target class=c>x</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectOptionalLengthPx(styles, FindElementById(document, "target"), &ComputedStyle::max_width,
                           10.0f, "a class rule beats a type rule for new properties");
  }
}

}  // namespace

int main() {
  TestLengthUnits();
  TestFontSizeUnits();
  TestColors();
  TestBackgroundShorthand();
  TestLogicalProperties();
  TestMarginsAndSizes();
  TestCascadeOrder();
  return toy_test::Finish("style values test");
}
