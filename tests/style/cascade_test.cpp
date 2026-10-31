// Tests for the M4 cascade: UA defaults, specificity, inline styles,
// !important, inheritance, shorthands, em lengths and ignored values.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "toy/dom/node.h"
#include "toy/style/computed_style.h"

namespace {

using toy::dom::Document;
using toy::dom::NodeId;
using toy::dom::NodeType;
using toy::style::AutoLength;
using toy::style::ComputedStyle;
using toy::style::Display;
using toy::style::FontStyle;
using toy::style::Length;
using toy::style::StyleTree;

int g_failures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "style cascade test failed: %s\n", description);
    ++g_failures;
  }
}

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

NodeId FindFirstElementByTag(const Document& document, std::string_view tag) {
  for (const NodeId element : CollectElements(document)) {
    if (document.Get(element).name == tag) {
      return element;
    }
  }
  return toy::dom::kInvalidNode;
}

void ExpectPx(const StyleTree& styles, NodeId id, float ComputedStyle::* member, float expected,
              const char* description) {
  const float actual = styles.Get(id).*member;
  if (std::fabs(actual - expected) > 0.01f) {
    std::fprintf(stderr, "style cascade test failed: %s (expected %g, got %g)\n", description,
                 expected, actual);
    ++g_failures;
  }
}

void ExpectLength(const StyleTree& styles, NodeId id, Length ComputedStyle::* member,
                  Length::Unit unit, float expected, const char* description) {
  const Length& actual = styles.Get(id).*member;
  if (actual.unit != unit || std::fabs(actual.value - expected) > 0.01f) {
    std::fprintf(stderr, "style cascade test failed: %s (expected %g, got %g, unit %d vs %d)\n",
                 description, expected, actual.value, static_cast<int>(unit),
                 static_cast<int>(actual.unit));
    ++g_failures;
  }
}

void ExpectPxLength(const StyleTree& styles, NodeId id, Length ComputedStyle::* member,
                    float expected, const char* description) {
  ExpectLength(styles, id, member, Length::Unit::kPx, expected, description);
}

void ExpectPercentLength(const StyleTree& styles, NodeId id, Length ComputedStyle::* member,
                         float expected, const char* description) {
  ExpectLength(styles, id, member, Length::Unit::kPercent, expected, description);
}

void ExpectMargin(const StyleTree& styles, NodeId id, AutoLength ComputedStyle::* member,
                  float expected, const char* description) {
  const AutoLength& actual = styles.Get(id).*member;
  if (actual.is_auto || actual.length.unit != Length::Unit::kPx ||
      std::fabs(actual.length.value - expected) > 0.01f) {
    std::fprintf(stderr, "style cascade test failed: %s (expected %g px)\n", description, expected);
    ++g_failures;
  }
}

void ExpectOptionalLength(const StyleTree& styles, NodeId id,
                          std::optional<Length> ComputedStyle::* member, float expected,
                          const char* description) {
  const std::optional<Length>& actual = styles.Get(id).*member;
  if (!actual || actual->unit != Length::Unit::kPx ||
      std::fabs(actual->value - expected) > 0.01f) {
    std::fprintf(stderr, "style cascade test failed: %s (expected %g px)\n", description, expected);
    ++g_failures;
  }
}

void ExpectOptionalPercentLength(const StyleTree& styles, NodeId id,
                                 std::optional<Length> ComputedStyle::* member, float expected,
                                 const char* description) {
  const std::optional<Length>& actual = styles.Get(id).*member;
  if (!actual || actual->unit != Length::Unit::kPercent ||
      std::fabs(actual->value - expected) > 0.01f) {
    std::fprintf(stderr, "style cascade test failed: %s (expected %g%%)\n", description, expected);
    ++g_failures;
  }
}

void ExpectDisplay(const StyleTree& styles, NodeId id, Display expected, const char* description) {
  if (styles.Get(id).display != expected) {
    std::fprintf(stderr, "style cascade test failed: %s\n", description);
    ++g_failures;
  }
}

void ExpectColor(const StyleTree& styles, NodeId id, std::uint32_t expected,
                 const char* description) {
  if (styles.Get(id).color != expected) {
    std::fprintf(stderr, "style cascade test failed: %s (expected #%06x, got #%06x)\n", description,
                 expected, styles.Get(id).color);
    ++g_failures;
  }
}

void TestUserAgentDefaults() {
  const Document document = toy::dom::ParseDocument(
      "<!doctype html><html><head><style>a { color: #0000ee; }</style></head>"
      "<body><div><p>paragraph</p><span>span</span><a href=\"/x\">link</a></div>"
      "<h1>h1</h1><h2>h2</h2><h3>h3</h3><title>t</title></body></html>");
  const StyleTree styles = toy::style::ComputeStyles(document);

  const NodeId html = FindFirstElementByTag(document, "html");
  const NodeId head = FindFirstElementByTag(document, "head");
  const NodeId body = FindFirstElementByTag(document, "body");
  const NodeId div = FindFirstElementByTag(document, "div");
  const NodeId p = FindFirstElementByTag(document, "p");
  const NodeId span = FindFirstElementByTag(document, "span");
  const NodeId anchor = FindFirstElementByTag(document, "a");
  const NodeId h1 = FindFirstElementByTag(document, "h1");
  const NodeId h2 = FindFirstElementByTag(document, "h2");
  const NodeId h3 = FindFirstElementByTag(document, "h3");
  const NodeId title = FindFirstElementByTag(document, "title");
  const NodeId style_element = FindFirstElementByTag(document, "style");

  Check(html != toy::dom::kInvalidNode && body != toy::dom::kInvalidNode, "test document parses");

  ExpectDisplay(styles, html, Display::kBlock, "html is block");
  ExpectDisplay(styles, body, Display::kBlock, "body is block");
  ExpectDisplay(styles, div, Display::kBlock, "div is block");
  ExpectDisplay(styles, p, Display::kBlock, "p is block");
  ExpectDisplay(styles, span, Display::kInline, "span is inline");
  ExpectDisplay(styles, head, Display::kNone, "head is display:none");
  ExpectDisplay(styles, title, Display::kNone, "title is display:none");
  ExpectDisplay(styles, style_element, Display::kNone, "style is display:none");

  ExpectColor(styles, html, 0x000000u, "the default text color is black");
  ExpectColor(styles, anchor, 0x0000eeu, "the UA sheet makes links #0000ee");
  ExpectPx(styles, html, &ComputedStyle::font_size_px, 16.0f, "the initial font size is 16px");
  if (styles.Get(html).font_weight != 400) {
    Check(false, "the initial font weight is 400");
  }
  if (styles.Get(html).font_style != FontStyle::kNormal) {
    Check(false, "the initial font style is normal");
  }
  if (styles.Get(html).width.has_value()) {
    Check(false, "the initial width is auto");
  }

  ExpectMargin(styles, body, &ComputedStyle::margin_top, 8.0f,
               "the UA sheet gives body an 8px margin");
  ExpectMargin(styles, body, &ComputedStyle::margin_right, 8.0f, "body margin-right is 8px");
  ExpectMargin(styles, body, &ComputedStyle::margin_bottom, 8.0f, "body margin-bottom is 8px");
  ExpectMargin(styles, body, &ComputedStyle::margin_left, 8.0f, "body margin-left is 8px");

  ExpectMargin(styles, p, &ComputedStyle::margin_top, 16.0f,
               "the UA sheet gives p a 16px top margin");
  ExpectMargin(styles, p, &ComputedStyle::margin_right, 0.0f, "p margin-right is 0");
  ExpectMargin(styles, p, &ComputedStyle::margin_bottom, 16.0f, "p has a 16px bottom margin");
  ExpectMargin(styles, p, &ComputedStyle::margin_left, 0.0f, "p margin-left is 0");

  ExpectPx(styles, h1, &ComputedStyle::font_size_px, 32.0f, "h1 is 32px");
  if (styles.Get(h1).font_weight != 700) {
    Check(false, "h1 is bold");
  }
  ExpectMargin(styles, h1, &ComputedStyle::margin_top, 16.0f, "h1 top margin is 16px");
  ExpectPx(styles, h2, &ComputedStyle::font_size_px, 24.0f, "h2 is 24px");
  ExpectMargin(styles, h2, &ComputedStyle::margin_top, 12.0f, "h2 top margin is 12px");
  ExpectPx(styles, h3, &ComputedStyle::font_size_px, 20.0f, "h3 is 20px");
  ExpectMargin(styles, h3, &ComputedStyle::margin_top, 12.0f, "h3 top margin is 12px");
}

void TestInheritance() {
  const Document document = toy::dom::ParseDocument(
      "<div id=outer style=\"color: red; font-size: 20px; font-weight: 700; "
      "font-style: italic; background-color: #ffffff\"><span id=inner>text</span></div>");
  const StyleTree styles = toy::style::ComputeStyles(document);
  const NodeId outer = FindElementById(document, "outer");
  const NodeId inner = FindElementById(document, "inner");

  ExpectColor(styles, outer, 0xff0000u, "the inline style colors the parent");
  ExpectColor(styles, inner, 0xff0000u, "color inherits");
  ExpectPx(styles, inner, &ComputedStyle::font_size_px, 20.0f, "font-size inherits");
  if (styles.Get(inner).font_weight != 700) {
    Check(false, "font-weight inherits");
  }
  if (styles.Get(inner).font_style != FontStyle::kItalic) {
    Check(false, "font-style inherits");
  }
  if (styles.Get(inner).background_color != 0x00000000u) {
    Check(false, "background-color does not inherit");
  }
  ExpectDisplay(styles, inner, Display::kInline, "span display does not inherit");

  // The root inherits from the initial values, not from the body.
  const Document nested = toy::dom::ParseDocument(
      "<body style=\"font-size: 30px; color: blue\"><p id=child>x</p></body>");
  const StyleTree nested_styles = toy::style::ComputeStyles(nested);
  const NodeId html = FindFirstElementByTag(nested, "html");
  const NodeId child = FindElementById(nested, "child");
  ExpectPx(nested_styles, html, &ComputedStyle::font_size_px, 16.0f,
               "the root inherits the initial font size");
  ExpectColor(nested_styles, html, 0x000000u, "the root inherits the initial color");
  ExpectPx(nested_styles, child, &ComputedStyle::font_size_px, 30.0f,
               "descendants inherit from body");
  ExpectColor(nested_styles, child, 0x0000ffu, "descendants inherit body color");
}

// The non-element Get contract: text nodes report their parent element style.
void TestNonElementGet() {
  const Document document = toy::dom::ParseDocument("<p id=p style=\"color: red\">text</p>");
  const StyleTree styles = toy::style::ComputeStyles(document);
  const NodeId p = FindElementById(document, "p");
  NodeId text = document.Get(p).first_child;
  while (text != toy::dom::kInvalidNode && document.Get(text).type != NodeType::kText) {
    text = document.Get(text).next_sibling;
  }
  Check(text != toy::dom::kInvalidNode, "test document has a text child");
  if (text != toy::dom::kInvalidNode) {
    ExpectColor(styles, text, 0xff0000u, "a text node reports its parent element style");
  }
  Check(styles.Get(toy::dom::kInvalidNode).font_size_px == 16.0f,
        "an invalid node id reports the default style");
}

void TestSpecificityAndOrder() {
  // #id beats .class beats type.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>p { color: red; } .c { color: blue; } #i { color: green; }</style>"
        "<p id=i class=c>text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId p = FindElementById(document, "i");
    ExpectColor(styles, p, 0x008000u, "#id beats .class and type");
  }
  // Source order breaks specificity ties.
  {
    const Document document =
        toy::dom::ParseDocument("<style>p { color: red; } p { color: blue; }</style><p>text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindFirstElementByTag(document, "p"), 0x0000ffu,
                "the later rule wins on a specificity tie");
  }
  // A comma group uses the most specific matching selector.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>p, .c { color: red; } p { color: blue; }</style><p class=c>text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindFirstElementByTag(document, "p"), 0xff0000u,
                "a matching class alternative raises the specificity");
  }
  // Universal selectors have zero specificity.
  {
    const Document document =
        toy::dom::ParseDocument("<style>* { color: gray; } p { color: red; }</style><p>text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindFirstElementByTag(document, "p"), 0xff0000u, "type beats universal");
  }
  // Type selectors are case-insensitive.
  {
    const Document document =
        toy::dom::ParseDocument("<style>DIV { color: red; }</style><div>text</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindFirstElementByTag(document, "div"), 0xff0000u,
                "type selectors match case-insensitively");
  }
  // Any class token matches.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>.b { color: blue; }</style><div class=\"a b c\">text</div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindFirstElementByTag(document, "div"), 0x0000ffu,
                "any space-separated class token matches");
  }
  // Two style elements are processed in document order.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>p { color: red; }</style><style>p { color: blue; }</style><p>text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindFirstElementByTag(document, "p"), 0x0000ffu,
                "later style elements win on ties");
  }
}

void TestCombinators() {
  // Child combinator only matches a direct parent.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>div > p { color: red; }</style><div><section><p id=p>text</p></section></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindElementById(document, "p"), 0x000000u,
                "div > p does not match a grandchild");
  }
  {
    const Document document = toy::dom::ParseDocument(
        "<style>div > p { color: red; } div p { color: blue; }</style>"
        "<div><p id=p>text</p></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindElementById(document, "p"), 0x0000ffu,
                "equal specificity descendant rule wins by source order");
  }
  // A class in the ancestor compound raises its specificity.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>.a > p { color: red; } div p { color: blue; }</style>"
        "<div class=a><p id=p>text</p></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindElementById(document, "p"), 0xff0000u,
                "class ancestor beats a longer type selector chain");
  }
  // Descendant combinator skips intermediate elements of any tag.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>body span { color: green; }</style><body><div><span id=s>text</span></div></body>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindElementById(document, "s"), 0x008000u,
                "descendant selectors match across intermediate elements");
  }
}

void TestInlineStyles() {
  // Inline beats an id selector.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#i { color: red; }</style><p id=i style=\"color: blue\">text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindElementById(document, "i"), 0x0000ffu, "inline beats a selector");
  }
  // The later declaration wins inside one style attribute.
  {
    const Document document =
        toy::dom::ParseDocument("<p id=p style=\"color: red; color: blue\">text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindElementById(document, "p"), 0x0000ffu,
                "the later inline declaration wins");
  }
  // Author !important beats normal inline.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>p { color: red !important; }</style><p style=\"color: blue\">text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindFirstElementByTag(document, "p"), 0xff0000u,
                "author !important beats normal inline");
  }
  // Inline !important keeps author-important origin with inline specificity.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>#i { color: red !important; }</style>"
        "<p id=i style=\"color: green !important\">text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindElementById(document, "i"), 0x008000u,
                "inline !important beats selector !important");
  }
  // !important beats a later normal declaration.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>p { color: red !important; } p { color: blue; }</style><p>text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, FindFirstElementByTag(document, "p"), 0xff0000u,
                "!important beats a later normal declaration");
  }
}

void TestShorthands() {
  const Document document = toy::dom::ParseDocument(
      "<div id=one style=\"margin: 5px\">1</div>"
      "<div id=two style=\"margin: 1px 2px\">2</div>"
      "<div id=three style=\"margin: 1px 2px 3px\">3</div>"
      "<div id=four style=\"margin: 1px 2px 3px 4px\">4</div>"
      "<div id=padding style=\"padding: 1px 2px 3px 4px\">5</div>"
      "<div id=override style=\"margin: 5px; margin-left: 9px\">6</div>"
      "<div id=later style=\"margin-left: 9px; margin: 5px\">7</div>"
      "<div id=invalid style=\"margin: 3px; margin: 1px broken\">8</div>");
  const StyleTree styles = toy::style::ComputeStyles(document);

  const NodeId one = FindElementById(document, "one");
  ExpectMargin(styles, one, &ComputedStyle::margin_top, 5.0f, "one-value shorthand sets top");
  ExpectMargin(styles, one, &ComputedStyle::margin_right, 5.0f, "one-value shorthand sets right");
  ExpectMargin(styles, one, &ComputedStyle::margin_bottom, 5.0f, "one-value shorthand sets bottom");
  ExpectMargin(styles, one, &ComputedStyle::margin_left, 5.0f, "one-value shorthand sets left");

  const NodeId two = FindElementById(document, "two");
  ExpectMargin(styles, two, &ComputedStyle::margin_top, 1.0f, "two-value shorthand top");
  ExpectMargin(styles, two, &ComputedStyle::margin_right, 2.0f, "two-value shorthand right");
  ExpectMargin(styles, two, &ComputedStyle::margin_bottom, 1.0f, "two-value shorthand bottom");
  ExpectMargin(styles, two, &ComputedStyle::margin_left, 2.0f, "two-value shorthand left");

  const NodeId three = FindElementById(document, "three");
  ExpectMargin(styles, three, &ComputedStyle::margin_top, 1.0f, "three-value shorthand top");
  ExpectMargin(styles, three, &ComputedStyle::margin_right, 2.0f, "three-value shorthand right");
  ExpectMargin(styles, three, &ComputedStyle::margin_bottom, 3.0f, "three-value shorthand bottom");
  ExpectMargin(styles, three, &ComputedStyle::margin_left, 2.0f, "three-value shorthand left");

  const NodeId four = FindElementById(document, "four");
  ExpectMargin(styles, four, &ComputedStyle::margin_top, 1.0f, "four-value shorthand top");
  ExpectMargin(styles, four, &ComputedStyle::margin_right, 2.0f, "four-value shorthand right");
  ExpectMargin(styles, four, &ComputedStyle::margin_bottom, 3.0f, "four-value shorthand bottom");
  ExpectMargin(styles, four, &ComputedStyle::margin_left, 4.0f, "four-value shorthand left");

  const NodeId padding = FindElementById(document, "padding");
  ExpectPxLength(styles, padding, &ComputedStyle::padding_top, 1.0f, "padding shorthand top");
  ExpectPxLength(styles, padding, &ComputedStyle::padding_right, 2.0f, "padding shorthand right");
  ExpectPxLength(styles, padding, &ComputedStyle::padding_bottom, 3.0f, "padding shorthand bottom");
  ExpectPxLength(styles, padding, &ComputedStyle::padding_left, 4.0f, "padding shorthand left");

  const NodeId override = FindElementById(document, "override");
  ExpectMargin(styles, override, &ComputedStyle::margin_top, 5.0f,
               "later longhand beats shorthand");
  ExpectMargin(styles, override, &ComputedStyle::margin_left, 9.0f, "longhand overrides shorthand");

  const NodeId later = FindElementById(document, "later");
  ExpectMargin(styles, later, &ComputedStyle::margin_left, 5.0f, "later shorthand beats longhand");

  const NodeId invalid = FindElementById(document, "invalid");
  ExpectMargin(styles, invalid, &ComputedStyle::margin_left, 3.0f,
               "an invalid shorthand leaves the property unchanged");
}

void TestEmUnits() {
  // font-size em is relative to the parent font size.
  {
    const Document document = toy::dom::ParseDocument(
        "<div style=\"font-size: 20px\"><p id=p style=\"font-size: 1.5em\">text</p></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectPx(styles, FindElementById(document, "p"), &ComputedStyle::font_size_px, 30.0f,
                 "em font-size is relative to the parent");
  }
  // Other em lengths are relative to the element's own computed font size.
  {
    const Document document = toy::dom::ParseDocument(
        "<div style=\"font-size: 10px\"><p id=p style=\"font-size: 2em; margin: 1em; "
        "width: 2em\">text</p></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId p = FindElementById(document, "p");
    ExpectPx(styles, p, &ComputedStyle::font_size_px, 20.0f, "2em font-size is 20px");
    ExpectMargin(styles, p, &ComputedStyle::margin_top, 20.0f,
                 "em margins use the element's own font size");
    ExpectMargin(styles, p, &ComputedStyle::margin_left, 20.0f,
                 "em margins use the element's own font size");
    ExpectOptionalLength(styles, p, &ComputedStyle::width, 40.0f,
                         "em width uses the element's own font size");
  }
  // em in a style sheet resolves the same way.
  {
    const Document document = toy::dom::ParseDocument(
        "<style>div { font-size: 20px; } div p { margin-left: 2em; }</style>"
        "<div><p id=p>text</p></div>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectMargin(styles, FindElementById(document, "p"), &ComputedStyle::margin_left, 40.0f,
                 "em in a rule uses the inherited font size");
  }
  // Length units are case-insensitive.
  {
    const Document document =
        toy::dom::ParseDocument("<p id=p style=\"font-size: 2EM; margin-top: 3PX\">text</p>");
    const StyleTree styles = toy::style::ComputeStyles(document);
    const NodeId p = FindElementById(document, "p");
    ExpectPx(styles, p, &ComputedStyle::font_size_px, 32.0f, "EM is case-insensitive");
    ExpectMargin(styles, p, &ComputedStyle::margin_top, 3.0f, "PX is case-insensitive");
  }
}

void TestUnknownValuesIgnored() {
  const Document document = toy::dom::ParseDocument(
      "<style>div { color: notacolor; display: flex; width: 50%; "
      "padding: 1px 2%; font-weight: 950; font-style: oblique; "
      "border-width: 2px; }</style>"
      "<div id=d style=\"background-color: #12345\">text</div>");
  const StyleTree styles = toy::style::ComputeStyles(document);
  const NodeId div = FindElementById(document, "d");
  ExpectColor(styles, div, 0x000000u, "an unknown color is ignored");
  ExpectDisplay(styles, div, Display::kBlock, "an unsupported display value is ignored");
  ExpectOptionalPercentLength(styles, div, &ComputedStyle::width, 50.0f,
                              "a percentage width is stored unresolved in style");
  ExpectPxLength(styles, div, &ComputedStyle::padding_top, 1.0f,
                 "a padding shorthand with a percentage keeps its px component");
  ExpectPercentLength(styles, div, &ComputedStyle::padding_right, 2.0f,
                      "a padding shorthand percentage is stored unresolved");
  ExpectPxLength(styles, div, &ComputedStyle::padding_bottom, 1.0f,
                 "the two-value padding shorthand repeats the first value");
  ExpectPercentLength(styles, div, &ComputedStyle::padding_left, 2.0f,
                      "the two-value padding shorthand repeats the second value");
  if (styles.Get(div).font_weight != 400) {
    Check(false, "a font-weight outside 100..900 is ignored");
  }
  if (styles.Get(div).font_style != FontStyle::kNormal) {
    Check(false, "an unsupported font-style is ignored");
  }
  if (styles.Get(div).background_color != 0x00000000u) {
    Check(false, "a malformed hex color is ignored");
  }

  // Invalid longhands leave an earlier valid value in place.
  const Document nested = toy::dom::ParseDocument(
      "<div id=n style=\"margin-top: 4px; margin-top: nonsense; width: 12rem\">text</div>");
  const StyleTree nested_styles = toy::style::ComputeStyles(nested);
  const NodeId n = FindElementById(nested, "n");
  ExpectMargin(nested_styles, n, &ComputedStyle::margin_top, 4.0f,
               "an invalid value does not reset the property");
  ExpectOptionalLength(nested_styles, n, &ComputedStyle::width, 192.0f,
                       "12rem resolves against the 16px initial root font size");

  // Colors: #rgb expands, named colors and background-color work.
  const Document colored = toy::dom::ParseDocument(
      "<div id=c style=\"color: #abc; background-color: #112233\">text</div>"
      "<div id=t style=\"background-color: transparent; color: gray\">text</div>");
  const StyleTree colored_styles = toy::style::ComputeStyles(colored);
  ExpectColor(colored_styles, FindElementById(colored, "c"), 0xaabbccu, "#rgb expands to #rrggbb");
  if (colored_styles.Get(FindElementById(colored, "c")).background_color != 0xff112233u) {
    Check(false, "an opaque background color keeps its alpha");
  }
  if (colored_styles.Get(FindElementById(colored, "t")).background_color != 0x00000000u) {
    Check(false, "transparent background-color is alpha 0");
  }
  ExpectColor(colored_styles, FindElementById(colored, "t"), 0x808080u, "the named color gray");
}

}  // namespace

int main() {
  TestUserAgentDefaults();
  TestInheritance();
  TestNonElementGet();
  TestSpecificityAndOrder();
  TestCombinators();
  TestInlineStyles();
  TestShorthands();
  TestEmUnits();
  TestUnknownValuesIgnored();
  if (g_failures != 0) {
    std::fprintf(stderr, "style cascade test: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("style cascade test passed\n");
  return 0;
}
