// Tests for M12 external author sheets: sheets supplied by the caller for a
// document's <link> elements join the cascade at that element's document
// position, interleaved with the document's own <style> elements.

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
using toy::style::LinkedStyleSheet;
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

void ExpectColor(const StyleTree& styles, NodeId id, std::uint32_t expected,
                 const char* description) {
  if (styles.Get(id).color != expected) {
    std::fprintf(stderr, "style external sheet test failed: %s (expected #%06x, got #%06x)\n",
                 description, expected, styles.Get(id).color);
    ++toy_test::failures;
  }
}

// A linked sheet before the <style> element is overridden by the later <style>
// on a specificity tie.
void TestLinkedSheetBeforeStyle() {
  const Document document = toy::dom::ParseDocument(
      "<!doctype html><html><head>"
      "<link id=external rel=stylesheet href=\"external.css\">"
      "<style>p { color: red; }</style>"
      "</head><body><p id=target>text</p></body></html>");
  const NodeId link = FindElementById(document, "external");
  const NodeId target = FindElementById(document, "target");
  toy_test::Check(link != toy::dom::kInvalidNode, "the document has the <link> element");
  toy_test::Check(target != toy::dom::kInvalidNode, "the document has the <p> element");

  const std::vector<LinkedStyleSheet> linked = {
      LinkedStyleSheet{link, "p { color: blue; }"}};
  const StyleTree styles = toy::style::ComputeStyles(document, linked);
  ExpectColor(styles, target, 0xff0000u,
              "a later <style> sheet wins a specificity tie against an earlier linked sheet");
}

// A linked sheet after the <style> element wins the same tie.
void TestLinkedSheetAfterStyle() {
  const Document document = toy::dom::ParseDocument(
      "<!doctype html><html><head>"
      "<style>p { color: red; }</style>"
      "<link id=external rel=stylesheet href=\"external.css\">"
      "</head><body><p id=target>text</p></body></html>");
  const NodeId link = FindElementById(document, "external");
  const NodeId target = FindElementById(document, "target");
  toy_test::Check(link != toy::dom::kInvalidNode, "the document has the <link> element");

  const std::vector<LinkedStyleSheet> linked = {
      LinkedStyleSheet{link, "p { color: blue; }"}};
  const StyleTree styles = toy::style::ComputeStyles(document, linked);
  ExpectColor(styles, target, 0x0000ffu,
              "a linked sheet after the <style> element wins the same specificity tie");
}

// Two links with the same rule: the later <link> element wins.
void TestLinkElementOrder() {
  const Document document = toy::dom::ParseDocument(
      "<!doctype html><html><head>"
      "<link id=first rel=stylesheet href=\"first.css\">"
      "<link id=second rel=stylesheet href=\"second.css\">"
      "</head><body><p id=target>text</p></body></html>");
  const NodeId first = FindElementById(document, "first");
  const NodeId second = FindElementById(document, "second");
  const NodeId target = FindElementById(document, "target");

  const std::vector<LinkedStyleSheet> linked = {
      LinkedStyleSheet{first, "p { color: blue; }"},
      LinkedStyleSheet{second, "p { color: green; }"}};
  const StyleTree styles = toy::style::ComputeStyles(document, linked);
  ExpectColor(styles, target, 0x008000u, "the later <link> element's sheet wins a tie");
}

// Inline styles keep the inline rank and beat a linked sheet.
void TestInlineStyleBeatsLinkedSheet() {
  const Document document = toy::dom::ParseDocument(
      "<!doctype html><html><head>"
      "<link id=external rel=stylesheet href=\"external.css\">"
      "</head><body><p id=target style=\"color: green\">text</p></body></html>");
  const NodeId link = FindElementById(document, "external");
  const NodeId target = FindElementById(document, "target");

  const std::vector<LinkedStyleSheet> linked = {
      LinkedStyleSheet{link, "p { color: blue; } #target { color: red; }"}};
  const StyleTree styles = toy::style::ComputeStyles(document, linked);
  ExpectColor(styles, target, 0x008000u, "an inline style beats a linked sheet");
}

// !important in a linked sheet beats a normal later rule; specificity still
// dominates document order.
void TestImportantAndSpecificity() {
  // Important earlier linked sheet beats a normal later <style> rule.
  {
    const Document document = toy::dom::ParseDocument(
        "<!doctype html><html><head>"
        "<link id=external rel=stylesheet href=\"external.css\">"
        "<style>p { color: blue; }</style>"
        "</head><body><p id=target>text</p></body></html>");
    const NodeId link = FindElementById(document, "external");
    const NodeId target = FindElementById(document, "target");
    const std::vector<LinkedStyleSheet> linked = {
        LinkedStyleSheet{link, "p { color: red !important; }"}};
    const StyleTree styles = toy::style::ComputeStyles(document, linked);
    ExpectColor(styles, target, 0xff0000u,
                "!important in a linked sheet beats a normal later <style> rule");
  }
  // Higher specificity wins among equal-importance rules even when the lower
  // specificity rule comes later in document order.
  {
    const Document document = toy::dom::ParseDocument(
        "<!doctype html><html><head>"
        "<link id=external rel=stylesheet href=\"external.css\">"
        "<style>p { color: blue !important; }</style>"
        "</head><body><p id=target>text</p></body></html>");
    const NodeId link = FindElementById(document, "external");
    const NodeId target = FindElementById(document, "target");
    const std::vector<LinkedStyleSheet> linked = {
        LinkedStyleSheet{link, "#target { color: red !important; }"}};
    const StyleTree styles = toy::style::ComputeStyles(document, linked);
    ExpectColor(styles, target, 0xff0000u,
                "specificity dominates document order among !important rules");
  }
}

// Entries that do not name a <link> element of the document, and empty texts,
// contribute nothing.
void TestIgnoredEntries() {
  const Document document = toy::dom::ParseDocument(
      "<!doctype html><html><head>"
      "<link id=external rel=stylesheet href=\"external.css\">"
      "</head><body><div id=not_a_link>block</div><p id=target>text</p></body></html>");
  const NodeId link = FindElementById(document, "external");
  const NodeId not_a_link = FindElementById(document, "not_a_link");
  const NodeId target = FindElementById(document, "target");
  toy_test::Check(link != toy::dom::kInvalidNode, "the document has the <link> element");
  toy_test::Check(not_a_link != toy::dom::kInvalidNode, "the document has the <div> element");

  // Invalid, absent and non-link node ids plus empty text are all no-ops here;
  // only the valid linked sheet contributes a rule.
  const std::vector<LinkedStyleSheet> linked = {
      LinkedStyleSheet{toy::dom::kInvalidNode, "p { color: blue; }"},
      LinkedStyleSheet{not_a_link, "p { color: green; }"},
      LinkedStyleSheet{static_cast<NodeId>(document.size() + 10), "p { color: yellow; }"},
      LinkedStyleSheet{link, ""},
      LinkedStyleSheet{link, "p { color: red; }"}};
  const StyleTree styles = toy::style::ComputeStyles(document, linked);
  ExpectColor(styles, target, 0xff0000u, "only the valid linked entry applies");

  // With only ignored entries the element keeps the default color.
  const std::vector<LinkedStyleSheet> ignored = {
      LinkedStyleSheet{toy::dom::kInvalidNode, "p { color: blue; }"},
      LinkedStyleSheet{not_a_link, "p { color: green; }"},
      LinkedStyleSheet{static_cast<NodeId>(document.size() + 10), "p { color: yellow; }"},
      LinkedStyleSheet{link, ""}};
  const StyleTree ignored_styles = toy::style::ComputeStyles(document, ignored);
  ExpectColor(ignored_styles, target, 0x000000u, "ignored entries contribute no rules");
}

// Several entries naming the same <link> element apply in input order.
void TestMultipleEntriesForOneLink() {
  const Document document = toy::dom::ParseDocument(
      "<!doctype html><html><head>"
      "<link id=external rel=stylesheet href=\"external.css\">"
      "</head><body><p id=target>text</p></body></html>");
  const NodeId link = FindElementById(document, "external");
  const NodeId target = FindElementById(document, "target");

  const std::vector<LinkedStyleSheet> linked = {
      LinkedStyleSheet{link, "p { color: red; }"},
      LinkedStyleSheet{link, "p { color: blue; }"}};
  const StyleTree styles = toy::style::ComputeStyles(document, linked);
  ExpectColor(styles, target, 0x0000ffu, "same-link entries apply in input order");
}

// The one-argument overload ignores <link> elements entirely.
void TestOneArgumentOverload() {
  // A document with a <link> but no external entries has no external rules.
  {
    const Document document = toy::dom::ParseDocument(
        "<!doctype html><html><head>"
        "<link id=external rel=stylesheet href=\"external.css\">"
        "</head><body><p id=target>text</p></body></html>");
    const NodeId target = FindElementById(document, "target");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, target, 0x000000u, "the one-argument overload applies no external rules");
  }
  // <style> rules still apply normally next to a <link>.
  {
    const Document document = toy::dom::ParseDocument(
        "<!doctype html><html><head>"
        "<link id=external rel=stylesheet href=\"external.css\">"
        "<style>p { color: red; }</style>"
        "</head><body><p id=target>text</p></body></html>");
    const NodeId target = FindElementById(document, "target");
    const StyleTree styles = toy::style::ComputeStyles(document);
    ExpectColor(styles, target, 0xff0000u, "the one-argument overload still applies <style>");
  }
  // An explicit empty linked-sheet list behaves exactly like the one-argument
  // overload, including the ignored <link>.
  {
    const Document document = toy::dom::ParseDocument(
        "<!doctype html><html><head>"
        "<link id=external rel=stylesheet href=\"external.css\">"
        "<style>p { color: red; }</style>"
        "</head><body><p id=target>text</p></body></html>");
    const NodeId target = FindElementById(document, "target");
    const StyleTree one_arg = toy::style::ComputeStyles(document);
    const StyleTree empty_list = toy::style::ComputeStyles(document, {});
    ExpectColor(empty_list, target, one_arg.Get(target).color,
                "an empty linked-sheet list matches the one-argument overload");
  }
}

}  // namespace

int main() {
  TestLinkedSheetBeforeStyle();
  TestLinkedSheetAfterStyle();
  TestLinkElementOrder();
  TestInlineStyleBeatsLinkedSheet();
  TestImportantAndSpecificity();
  TestIgnoredEntries();
  TestMultipleEntriesForOneLink();
  TestOneArgumentOverload();
  return toy_test::Finish("style external sheet test");
}
