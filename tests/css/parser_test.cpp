// Tests for toy::css: style sheet, selector and declaration parsing.

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "toy/css/stylesheet.h"

namespace {

namespace css = toy::css;

int g_failures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "css test failed: %s\n", description);
    ++g_failures;
  }
}

// Test-only textual dump of the parsed structures, so the cases below can be
// written as compact expected strings and compared field by field.
std::string DumpCompound(const css::CompoundSelector& compound) {
  std::string text;
  for (const css::SimpleSelector& part : compound.parts) {
    switch (part.kind) {
      case css::SimpleKind::kType:
        text += part.name;
        break;
      case css::SimpleKind::kClass:
        text += '.';
        text += part.name;
        break;
      case css::SimpleKind::kId:
        text += '#';
        text += part.name;
        break;
      case css::SimpleKind::kUniversal:
        text += '*';
        break;
    }
  }
  return text;
}

std::string DumpComplex(const css::ComplexSelector& complex) {
  std::string text;
  for (std::size_t index = 0; index < complex.compounds.size(); ++index) {
    if (index > 0) {
      const bool child = index - 1 < complex.combinators.size() &&
                         complex.combinators[index - 1] == css::Combinator::kChild;
      text += child ? " > " : " ";
    }
    text += DumpCompound(complex.compounds[index]);
  }
  return text;
}

std::string DumpSelector(const css::Selector& selector) {
  std::string text;
  for (std::size_t index = 0; index < selector.alternatives.size(); ++index) {
    if (index > 0) {
      text += ", ";
    }
    text += DumpComplex(selector.alternatives[index]);
  }
  return text;
}

std::string DumpDeclarations(const std::vector<css::Declaration>& declarations) {
  std::string text;
  for (std::size_t index = 0; index < declarations.size(); ++index) {
    if (index > 0) {
      text += "; ";
    }
    const css::Declaration& declaration = declarations[index];
    text += declaration.property;
    text += ':';
    text += declaration.value;
    if (declaration.important) {
      text += " !important";
    }
  }
  return text;
}

void CheckRuleCount(const std::string& sheet_text, std::size_t expected, const char* description) {
  const css::StyleSheet sheet = css::ParseStyleSheet(sheet_text);
  if (sheet.rules.size() != expected) {
    std::fprintf(stderr, "css test failed: %s (expected %zu rule(s), got %zu)\n", description,
                 expected, sheet.rules.size());
    ++g_failures;
  }
}

void CheckFirstSelector(const std::string& sheet_text, const std::string& expected,
                        const char* description) {
  const css::StyleSheet sheet = css::ParseStyleSheet(sheet_text);
  if (sheet.rules.empty()) {
    std::fprintf(stderr, "css test failed: %s (no rules parsed)\n", description);
    ++g_failures;
    return;
  }
  const std::string actual = DumpSelector(sheet.rules.front().selector);
  if (actual != expected) {
    std::fprintf(stderr, "css test failed: %s (expected selector '%s', got '%s')\n", description,
                 expected.c_str(), actual.c_str());
    ++g_failures;
  }
}

void CheckFirstDeclarations(const std::string& sheet_text, const std::string& expected,
                            const char* description) {
  const css::StyleSheet sheet = css::ParseStyleSheet(sheet_text);
  if (sheet.rules.empty()) {
    std::fprintf(stderr, "css test failed: %s (no rules parsed)\n", description);
    ++g_failures;
    return;
  }
  const std::string actual = DumpDeclarations(sheet.rules.front().declarations);
  if (actual != expected) {
    std::fprintf(stderr, "css test failed: %s (expected '%s', got '%s')\n", description,
                 expected.c_str(), actual.c_str());
    ++g_failures;
  }
}

void TestBasicRules() {
  CheckRuleCount("", 0, "an empty style sheet has no rules");
  CheckRuleCount("   \n\t  ", 0, "a whitespace-only style sheet has no rules");
  CheckRuleCount("body { color: red; }", 1, "a single rule is parsed");
  CheckFirstSelector("body { color: red; }", "body", "the single rule selector");
  CheckFirstDeclarations("body { color: red; }", "color:red", "the single rule declaration");
  CheckRuleCount("h1 { color: blue } p { color: green }", 2, "two rules are parsed in order");
  CheckRuleCount("a{}", 1, "a rule with an empty block is kept");
  CheckFirstDeclarations("a{}", "", "an empty block has no declarations");
  CheckFirstDeclarations("\n  body  {\n  color : red ;\n}\n", "color:red",
                         "newlines and spaces around tokens are ignored");
}

void TestSelectors() {
  struct SelectorCase {
    const char* css;
    const char* expected;
  };
  const SelectorCase cases[] = {
      {"div { }", "div"},         {".a { }", ".a"},
      {"#b { }", "#b"},           {"* { }", "*"},
      {"*.a { }", "*.a"},         {"div.a { }", "div.a"},
      {"div#b { }", "div#b"},     {"div.a#b { }", "div.a#b"},
      {"div p { }", "div p"},     {"div>p { }", "div > p"},
      {"div > p { }", "div > p"}, {"div  >  p { }", "div > p"},
      {"div\np { }", "div p"},    {"div > p em { }", "div > p em"},
      {"a b > c { }", "a b > c"}, {"ul > li > a { }", "ul > li > a"},
      {"* > * { }", "* > *"},     {"h1, p em, .x { }", "h1, p em, .x"},
      {"h1 ,\n p { }", "h1, p"},  {"P { }", "P"},
  };
  for (const SelectorCase& test : cases) {
    CheckFirstSelector(test.css, test.expected, test.css);
  }
}

void TestUnsupportedSelectors() {
  CheckRuleCount("a[href] { color: red }", 0, "an attribute selector rule is skipped");
  CheckRuleCount("a:hover { color: red }", 0, "a pseudo-class rule is skipped");
  CheckRuleCount("p::before { color: red }", 0, "a pseudo-element rule is skipped");
  CheckRuleCount("div*p { color: red }", 0, "a '*' in the middle of a compound is skipped");
  CheckRuleCount("a > { color: red }", 0, "a dangling child combinator is skipped");
  CheckRuleCount("> p { color: red }", 0, "a leading child combinator is skipped");
  CheckRuleCount("a >> p { color: red }", 0, "a doubled child combinator is skipped");
  CheckFirstSelector("a:hover, p { color: red }", "p", "a supported selector alternative survives");
  CheckFirstSelector("p, a[href] { color: red }", "p",
                     "unsupported alternatives are dropped from a group");
  CheckFirstSelector("p, , div { }", "p, div", "empty selector alternatives are skipped");
  CheckFirstSelector("p, { }", "p", "a trailing comma does not invalidate the group");
}

void TestDeclarations() {
  struct DeclarationCase {
    const char* css;
    const char* expected;
  };
  const DeclarationCase cases[] = {
      {"p { color: red; }", "color:red"},
      {"p { color: RED; Background-color: #ABC; }", "color:RED; background-color:#ABC"},
      {"p { margin:  0   auto  ; }", "margin:0 auto"},
      {"p { color: red !important; }", "color:red !important"},
      {"p { color: red ! IMPORTANT; }", "color:red !important"},
      {"p { color:red!important }", "color:red !important"},
      {"p { color: red; ; ; background: blue; }", "color:red; background:blue"},
      {"p { color red; }", ""},
      {"p { color: red }", "color:red"},
      {"p { color: red; background: blue }", "color:red; background:blue"},
      {"p { font-family: Arial,   sans-serif; }", "font-family:Arial, sans-serif"},
      {"p {  color  :  red  ;  }", "color:red"},
      {"p { content: \"a  b\"; }", "content:\"a b\""},
  };
  for (const DeclarationCase& test : cases) {
    CheckFirstDeclarations(test.css, test.expected, test.css);
  }
}

void TestDeclarationList() {
  const std::vector<css::Declaration> basic =
      css::ParseDeclarationList("color: red; background: blue");
  Check(basic.size() == 2, "a declaration list has two declarations");
  Check(DumpDeclarations(basic) == "color:red; background:blue",
        "declaration list properties are lowercased and values trimmed");

  const std::vector<css::Declaration> important =
      css::ParseDeclarationList(" color : red !Important ; ");
  Check(important.size() == 1, "a single declaration with !important");
  Check(DumpDeclarations(important) == "color:red !important",
        "!important is case-insensitive and stripped from the value");
  Check(important.size() == 1 && important.front().important,
        "the important flag is set on the declaration struct");

  Check(css::ParseDeclarationList("").empty(), "an empty declaration list is empty");
  Check(css::ParseDeclarationList(" ; ; ").empty(), "empty declarations are skipped");
  const std::vector<css::Declaration> mixed = css::ParseDeclarationList("garbage; width:10px");
  Check(DumpDeclarations(mixed) == "width:10px",
        "a declaration without ':' is skipped, later ones are kept");
  Check(css::ParseDeclarationList("font: 12px/1.5 Arial, sans-serif; color: blue").size() == 2,
        "values keep commas and slashes");
}

void TestAtRules() {
  CheckRuleCount("@media (max-width: 600px) { p { color: red } } p { color: blue }", 1,
                 "a media block is skipped and the following rule is kept");
  CheckFirstDeclarations("@media (max-width: 600px) { p { color: red } } p { color: blue }",
                         "color:blue", "the rule after a media block is parsed");
  CheckRuleCount("@import url(app.css); p { color: red }", 1, "an import statement is skipped");
  CheckRuleCount("@charset \"utf-8\";", 0, "a charset statement is skipped");
  CheckRuleCount("@font-face { font-family: x; src: url(x.woff); }", 0,
                 "a font-face block is skipped");
  CheckRuleCount("@media screen { @supports (display: grid) { a { color: red } } } b { }", 1,
                 "nested at-rules are skipped with their blocks");
  CheckFirstSelector("@media screen { @supports (display: grid) { a { color: red } } } b { }", "b",
                     "the rule after nested at-rules is kept");
}

void TestComments() {
  CheckRuleCount("/* leading */ p { color: red } /* trailing */", 1,
                 "comments around a rule are skipped");
  CheckRuleCount("p { color: /* inline */ red; }", 1, "an inline comment is skipped");
  CheckFirstDeclarations("p { color: /* inline */ red; }", "color:red",
                         "declarations around comments are parsed");
  CheckRuleCount("p { color: red } /* unterminated", 1,
                 "an unterminated trailing comment does not lose parsed rules");
  CheckRuleCount("/* unterminated p { color: red }", 0,
                 "an unterminated leading comment hides the rest");
  CheckFirstSelector("div /* between */ > /* parts */ p { }", "div > p",
                     "comments between selector tokens are treated as whitespace");
  CheckFirstDeclarations("p { content: \"a/*b*/c\"; }", "content:\"a/*b*/c\"",
                         "comment markers inside strings survive");
}

void TestMalformedInput() {
  CheckRuleCount("p { color: red", 1, "an unclosed block yields a rule");
  CheckFirstDeclarations("p { color: red", "color:red",
                         "declarations in an unclosed block are parsed");
  CheckRuleCount("p { color", 1, "a declaration without a colon is dropped, not fatal");
  CheckFirstDeclarations("p { color", "", "the unparseable declaration block is empty");
  CheckRuleCount("}}}", 0, "stray closing braces are skipped");
  CheckRuleCount("garbage text", 0, "trailing text without a block is skipped");
  CheckRuleCount("p { a { b: c } } q { color: red }", 2, "nested braces are tolerated");
  CheckRuleCount("p { color: red; } } q { color: blue }", 2,
                 "a stray closing brace between rules is skipped");
  CheckFirstSelector("p { color: red; } } q { color: blue }", "p",
                     "the rule before a stray closing brace is kept");
  CheckRuleCount("@media {", 0, "an unclosed at-rule is skipped");
  CheckRuleCount("p { color: red } @", 1, "an at-rule marker at EOF does not lose parsed rules");
  CheckRuleCount("p { color: red } /*", 1, "an unterminated comment at EOF is tolerated");
  CheckRuleCount("p { color: red } /", 1, "stray text after the last rule is tolerated");
  CheckRuleCount("p { background: url(a;b); }", 1, "semicolons inside parentheses do not split");
  CheckFirstDeclarations("p { background: url(a;b); }", "background:url(a;b)",
                         "a url() value with a semicolon is one declaration");
}

}  // namespace

int main() {
  TestBasicRules();
  TestSelectors();
  TestUnsupportedSelectors();
  TestDeclarations();
  TestDeclarationList();
  TestAtRules();
  TestComments();
  TestMalformedInput();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d css check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("css parser tests passed\n");
  return 0;
}
