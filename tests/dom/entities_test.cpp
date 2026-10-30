// Tests for entity decoding in text, RCDATA content and attribute values, and
// for constructs where entities must NOT be decoded (raw text, comments).

#include <cstdio>
#include <string>

#include "toy/dom/node.h"

namespace {

int g_failures = 0;

void ExpectDump(const std::string& html, const std::string& expected, const char* description) {
  const std::string actual = toy::dom::DumpDocument(toy::dom::ParseDocument(html));
  if (actual == expected) {
    return;
  }
  std::fprintf(stderr, "dom entities test failed: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n",
               description, html.c_str(), expected.c_str(), actual.c_str());
  ++g_failures;
}

struct EntityCase {
  std::string html;
  std::string expected;
  const char* description;
};

void TestEntityTable() {
  const std::string nbsp("\xC2\xA0");
  const std::string euro("\xE2\x82\xAC");
  const std::string cjk("\xE4\xBD\xA0");  // U+4F60, a CJK codepoint.

  std::string named_expected = R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "&<>\"')dom";
  named_expected += nbsp;
  named_expected += "\"\n";

  std::string utf8_expected = R"dom(#document
  <html>
    <head>
    <body>
      <p>
        ")dom";
  utf8_expected += euro;
  utf8_expected += cjk;
  utf8_expected += nbsp;
  utf8_expected += "\"\n";

  const EntityCase kCases[] = {
      // The supported named entities.
      {"<p>&amp;&lt;&gt;&quot;&apos;&nbsp;</p>", named_expected,
       "named entities decode in text, including U+00A0 as UTF-8 bytes"},
      // Numeric references.
      {"<p>&#65;&#66;&#67;&#x41;&#x62;&#x63;</p>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "ABCAbc"
)dom",
       "decimal and hexadecimal numeric references decode"},
      {"<p>&#x20AC;&#20320;&nbsp;</p>", utf8_expected,
       "numeric references above ASCII encode as UTF-8"},
      // Invalid or unknown references stay literal.
      {"<p>&#0;&#xD800;&#x110000;&#;&#x;</p>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "&#0;&#xD800;&#x110000;&#;&#x;"
)dom",
       "invalid numeric references stay literal text"},
      {"<p>&bogus; &amp &AMP; &foo</p>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "&bogus; &amp &AMP; &foo"
)dom",
       "unknown names and references without a semicolon stay literal"},
      {"<p>a & b &</p>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "a & b &"
)dom",
       "lone ampersands stay literal"},
      {"<p>&amp;amp;</p>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "&amp;"
)dom",
       "decoding is a single pass, the result is not decoded again"},
      // Attribute values are decoded.
      {"<p title=\"a&#33;b&#x21;c\">", R"dom(#document
  <html>
    <head>
    <body>
      <p title="a!b!c">
)dom",
       "numeric references decode in double-quoted attribute values"},
      {"<p title='&lt;x&gt;'>", R"dom(#document
  <html>
    <head>
    <body>
      <p title="<x>">
)dom",
       "named entities decode in single-quoted attribute values"},
      {"<p data-x=&amp;>", R"dom(#document
  <html>
    <head>
    <body>
      <p data-x="&">
)dom",
       "entities decode in unquoted attribute values"},
      // RCDATA decodes; raw text and comments do not.
      {"<textarea>&lt;&amp;&gt;</textarea>", R"dom(#document
  <html>
    <head>
    <body>
      <textarea>
        "<&>"
)dom",
       "RCDATA content is entity-decoded"},
      {"<script>&amp;&lt;</script>", R"dom(#document
  <html>
    <head>
    <body>
      <script>
        "&amp;&lt;"
)dom",
       "raw text content is not entity-decoded"},
      {"<!--&amp;-->", R"dom(#document
  <!-- &amp; -->
  <html>
    <head>
    <body>
)dom",
       "comments are not entity-decoded"},
      {"<!doctype html&amp;>", R"dom(#document
  <!doctype html&amp;>
  <html>
    <head>
    <body>
)dom",
       "doctype content is not entity-decoded"},
      // Decoded text can never become markup.
      {"<p>&lt;b&gt;bold&lt;/b&gt;</p>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "<b>bold</b>"
)dom",
       "decoded angle brackets are text, not tags"},
      // Text before html still decodes.
      {"&amp;text", R"dom(#document
  <html>
    <head>
    <body>
      "&text"
)dom",
       "entity decoding applies to text before html as well"},
  };
  for (const EntityCase& entity_case : kCases) {
    ExpectDump(entity_case.html, entity_case.expected, entity_case.description);
  }
}

}  // namespace

int main() {
  TestEntityTable();
  if (g_failures != 0) {
    std::fprintf(stderr, "dom entities test: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("dom entities test passed\n");
  return 0;
}
