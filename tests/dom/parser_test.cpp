// Tests for the HTML tokenizer + tree builder: inline (html, expected dump)
// pairs compared as exact strings.

#include <cstddef>
#include <cstdio>
#include <string>

#include "toy/dom/node.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "dom parser test failed: %s\n", description);
    ++g_failures;
  }
}

// Verifies the arena links reachable from the root: every child points back to
// its parent and the sibling chain matches first/last child.
void ValidateLinksFor(const toy::dom::Document& doc, toy::dom::NodeId id, std::size_t depth) {
  using toy::dom::kInvalidNode;
  using toy::dom::NodeId;
  const NodeId first_child = doc.Get(id).first_child;
  NodeId previous = kInvalidNode;
  NodeId child = first_child;
  std::size_t child_count = 0;
  while (child != kInvalidNode) {
    const auto& node = doc.Get(child);
    Check(node.parent == id, "a child points at its parent");
    Check(node.prev_sibling == previous, "previous sibling links are consistent");
    previous = child;
    child = node.next_sibling;
    ++child_count;
    if (child_count > doc.size()) {
      Check(false, "sibling links do not loop");
      break;
    }
  }
  Check(doc.Get(id).last_child == previous, "last child matches the sibling chain");
  child = first_child;
  while (child != kInvalidNode) {
    ValidateLinksFor(doc, child, depth + 1);
    child = doc.Get(child).next_sibling;
  }
}

void ValidateDocument(const toy::dom::Document& doc) {
  using toy::dom::NodeType;
  const toy::dom::NodeId root = doc.root();
  Check(root == 0, "the root is node 0");
  Check(doc.size() > 0, "a parsed document has nodes");
  if (doc.size() == 0) {
    return;
  }
  Check(doc.Get(root).type == NodeType::kDocument, "the root is the #document node");
  Check(doc.Get(root).parent == toy::dom::kInvalidNode, "the root has no parent");
  Check(doc.Get(root).prev_sibling == toy::dom::kInvalidNode, "the root has no previous sibling");
  Check(doc.Get(root).next_sibling == toy::dom::kInvalidNode, "the root has no next sibling");
  ValidateLinksFor(doc, root, 0);
}

void ExpectDump(const char* html, const char* expected, const char* description) {
  const toy::dom::Document document = toy::dom::ParseDocument(html);
  ValidateDocument(document);
  const std::string actual = toy::dom::DumpDocument(document);
  if (actual == expected) {
    return;
  }
  std::fprintf(stderr, "dom parser test failed: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n",
               description, html, expected, actual.c_str());
  ++g_failures;
}

struct ParserCase {
  const char* html;
  const char* expected;
  const char* description;
};

void TestCaseTable() {
  static const ParserCase kCases[] = {
      // Missing markup is repaired into a full skeleton.
      {"", R"dom(#document
  <html>
    <head>
    <body>
)dom",
       "empty input creates the html/head/body skeleton"},
      {"<p>x", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "x"
)dom",
       "missing html/head/body are created around content"},
      {"<!doctype html><html><head><title>Hi</title></head><body><p>Hello</p></body></html>",
       R"dom(#document
  <!doctype html>
  <html>
    <head>
      <title>
        "Hi"
    <body>
      <p>
        "Hello"
)dom",
       "complete document keeps its structure"},
      // Attributes.
      {"<div ID=\"a\" class='b c' data-x=1 hidden>", R"dom(#document
  <html>
    <head>
    <body>
      <div id="a" class="b c" data-x="1" hidden="">
)dom",
       "attribute names are lowercased, values decoded and quoted/boolean forms kept"},
      {"<p id=\"a\" id=\"b\" class=\"c\">", R"dom(#document
  <html>
    <head>
    <body>
      <p id="a" class="c">
)dom",
       "the first duplicate attribute wins"},
      {"<a href=\"a&amp;b\" data-x='q\"r'>", R"dom(#document
  <html>
    <head>
    <body>
      <a href="a&b" data-x="q\"r">
)dom",
       "attribute values are entity-decoded and escaped in the dump"},
      {"<p title=\"a\nb\">", R"dom(#document
  <html>
    <head>
    <body>
      <p title="a\nb">
)dom",
       "attribute values keep whitespace, escaped in the dump"},
      {"<html lang=\"en\"><body class=\"page\">x", R"dom(#document
  <html lang="en">
    <head>
    <body class="page">
      "x"
)dom",
       "explicit html/body attributes are kept"},
      // Elements and void elements.
      {"<p>before<br><img src=\"x.png\"><hr>after", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "before"
        <br>
        <img src="x.png">
        <hr>
        "after"
)dom",
       "void elements never take children"},
      {"<section><article><span>text", R"dom(#document
  <html>
    <head>
    <body>
      <section>
        <article>
          <span>
            "text"
)dom",
       "unclosed elements are closed at EOF"},
      {"<b><i>bold</b>tail</i>", R"dom(#document
  <html>
    <head>
    <body>
      <b>
        <i>
          "bold"
      "tail"
)dom",
       "an end tag pops up to its matching open element and stray text follows"},
      {"<p>x</br></p>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "x"
)dom",
       "stray end tags of void elements are ignored"},
      {"<div/>inside", R"dom(#document
  <html>
    <head>
    <body>
      <div>
        "inside"
)dom",
       "a trailing '/' before '>' is ignored, so non-void elements stay open"},
      {"<div><div></div>inner</div>", R"dom(#document
  <html>
    <head>
    <body>
      <div>
        <div>
        "inner"
)dom",
       "end tags match the innermost open element of that name"},
      {"<html><html><body>x", R"dom(#document
  <html>
    <head>
    <body>
      "x"
)dom",
       "duplicate html start tags reuse the single html element"},
      {"<html><head><meta charset=\"utf-8\"></head><body>x", R"dom(#document
  <html>
    <head>
      <meta charset="utf-8">
    <body>
      "x"
)dom",
       "explicit head content stays in head"},
      {"<!doctype html>\n<html>\n  <head>\n    <title>T</title>\n  </head>\n  <body>\n    "
       "<p>P</p>\n  </body>\n</html>",
       R"dom(#document
  <!doctype html>
  <html>
    <head>
      "\n  "
      "\n    "
      <title>
        "T"
      "\n  "
    <body>
      "\n"
      "\n  "
      "\n    "
      <p>
        "P"
      "\n  "
      "\n"
)dom",
       "a formatted document keeps title in head and preserves whitespace"},
      {"<html>  <p>x", R"dom(#document
  <html>
    <head>
      "  "
    <body>
      <p>
        "x"
)dom",
       "formatting whitespace between html and head belongs to head"},
      {"<html><head></head>  <body>x", R"dom(#document
  <html>
    <head>
    <body>
      "  "
      "x"
)dom",
       "whitespace after head belongs to the body"},
      {"<html><!--c--><head><title>t</title></head><body>x", R"dom(#document
  <html>
    <!-- c -->
    <head>
      <title>
        "t"
    <body>
      "x"
)dom",
       "a comment between html and head keeps its source order"},

      // Comments and doctype.
      {"<!-- top --><!doctype html><p>x", R"dom(#document
  <!--  top  -->
  <!doctype html>
  <html>
    <head>
    <body>
      <p>
        "x"
)dom",
       "comments and doctype before html are document children in source order"},
      {"\n<!doctype html><p>x", R"dom(#document
  <!doctype html>
  <html>
    <head>
    <body>
      "\n"
      <p>
        "x"
)dom",
       "leading whitespace before html does not drop the doctype"},
      {"\n<!--c--><html><body>y", R"dom(#document
  <!-- c -->
  <html>
    <head>
    <body>
      "\n"
      "y"
)dom",
       "leading whitespace before html does not move a comment after html"},

      {"<div><!-- c --></div>", R"dom(#document
  <html>
    <head>
    <body>
      <div>
        <!--  c  -->
)dom",
       "comments inside an element are its children"},
      {"<!-- unfinished", R"dom(#document
  <!--  unfinished -->
  <html>
    <head>
    <body>
)dom",
       "an unterminated comment runs to EOF"},
      {"<!---->", R"dom(#document
  <!--  -->
  <html>
    <head>
    <body>
)dom",
       "an empty comment is kept"},
      {"<!doctype html PUBLIC \"x\">", R"dom(#document
  <!doctype html PUBLIC "x">
  <html>
    <head>
    <body>
)dom",
       "doctype data is the raw trimmed content"},
      {"<!DOCTYPE HTML>", R"dom(#document
  <!doctype HTML>
  <html>
    <head>
    <body>
)dom",
       "doctype data keeps its case while the node name is lowercased"},
      {"<p>x<!doctype html>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "x"
)dom",
       "a doctype after content is dropped"},
      {"<html><body></body></html><!-- after -->", R"dom(#document
  <html>
    <head>
    <body>
  <!--  after  -->
)dom",
       "a comment after </html> stays a document child"},
      // Text placement.
      {"hello<html><body>world</body></html>", R"dom(#document
  <html>
    <head>
    <body>
      "hello"
      "world"
)dom",
       "text before html goes into body"},
      {"<html><body>x</body></html>tail", R"dom(#document
  <html>
    <head>
    <body>
      "x"
      "tail"
)dom",
       "text after </html> is appended to body"},
      {"<body><p>x</p></body><head></head>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "x"
)dom",
       "a head created late is placed before body"},
      {"1 < 2 & 3 > 2", R"dom(#document
  <html>
    <head>
    <body>
      "1 < 2 & 3 > 2"
)dom",
       "a lone '<' that starts no markup is text"},
      {"<?xml?><p>x", R"dom(#document
  <html>
    <head>
    <body>
      "<?xml?>"
      <p>
        "x"
)dom",
       "unsupported markup falls back to literal text"},
      // Raw text and RCDATA.
      {"<script>if (a < b) { s = \"&amp;\"; }</script>", R"dom(#document
  <html>
    <head>
    <body>
      <script>
        "if (a < b) { s = \"&amp;\"; }"
)dom",
       "script content is verbatim text with no entity decoding"},
      {"<style>p > a { color: red; }</style>", R"dom(#document
  <html>
    <head>
    <body>
      <style>
        "p > a { color: red; }"
)dom",
       "style content is a verbatim text child"},
      {"<title>A &amp; B</title>", R"dom(#document
  <html>
    <head>
    <body>
      <title>
        "A & B"
)dom",
       "RCDATA content is entity-decoded"},
      {"<textarea><b>not bold</b></textarea>", R"dom(#document
  <html>
    <head>
    <body>
      <textarea>
        "<b>not bold</b>"
)dom",
       "tags inside RCDATA are text"},
      {"<script>var x = 1;", R"dom(#document
  <html>
    <head>
    <body>
      <script>
        "var x = 1;"
)dom",
       "unterminated raw text runs to EOF"},
      // Dump escaping and whitespace preservation.
      {"<p>line one\nline two</p>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "line one\nline two"
)dom",
       "newlines in text are escaped in the dump"},
      {"<p>a\r\nb</p>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "a\r\nb"
)dom",
       "CR and LF are escaped independently"},
      {"<p>a\"b\\c\td\001e</p>", R"dom(#document
  <html>
    <head>
    <body>
      <p>
        "a\"b\\c\td\u0001e"
)dom",
       "quotes, backslashes, tabs and control bytes are escaped"},
      {"\xEF\xBB\xBF<!doctype html><p>x", R"dom(#document
  <!doctype html>
  <html>
    <head>
    <body>
      <p>
        "x"
)dom",
       "a leading UTF-8 BOM is stripped"},
  };
  for (const ParserCase& parser_case : kCases) {
    ExpectDump(parser_case.html, parser_case.expected, parser_case.description);
  }
}

// Regression for the review round: deeply nested input must not make the dump
// grow quadratically. Indentation is capped at 64 levels, so 10,000 nested
// elements produce roughly 1.3 MB instead of hundreds of megabytes.
void TestDeepNestingDump() {
  std::string html;
  html.reserve(10000 * 5);
  for (int level = 0; level < 10000; ++level) {
    html.append("<div>");
  }
  const auto document = toy::dom::ParseDocument(html);
  Check(document.size() > 10000, "a deeply nested document is parsed");
  const std::string dump = toy::dom::DumpDocument(document);
  Check(!dump.empty(), "a deeply nested document still dumps");
  Check(dump.back() == '\n', "a deeply nested dump ends with one newline");
  Check(dump.size() < 2U * 1024U * 1024U, "deep nesting stays far below a few megabytes");

  // The cap holds for the deepest line as well: at most 64 * 2 leading spaces.
  std::size_t max_indent = 0;
  std::size_t line_start = 0;
  while (line_start < dump.size()) {
    std::size_t indent = 0;
    while (line_start + indent < dump.size() && dump[line_start + indent] == ' ') {
      ++indent;
    }
    max_indent = indent > max_indent ? indent : max_indent;
    const std::size_t line_end = dump.find('\n', line_start);
    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
  }
  Check(max_indent <= 128, "dump indentation is capped at 64 levels");
  Check(max_indent == 128, "a 10,000 level tree reaches the indentation cap");

  // Shallow trees are far below the cap and keep their exact dump.
  ExpectDump("<div><p>shallow</p></div>", R"dom(#document
  <html>
    <head>
    <body>
      <div>
        <p>
          "shallow"
)dom",
             "shallow dumps are unchanged by the indentation cap");
}

void TestDocumentArena() {
  using toy::dom::kInvalidNode;
  using toy::dom::NodeType;

  const auto document = toy::dom::ParseDocument("<p>x");
  Check(document.root() == 0, "root() is the #document node id");
  Check(document.Get(document.root()).type == NodeType::kDocument, "root() has the document type");
  Check(document.size() >= 5, "a parsed document has a document, html, head, body and content");

  const toy::dom::Document empty;
  Check(empty.root() == kInvalidNode, "a default-constructed document has no root");
  Check(empty.size() == 0, "a default-constructed document is empty");

  const auto first = toy::dom::ParseDocument("<p>same input</p>");
  const auto second = toy::dom::ParseDocument("<p>same input</p>");
  Check(toy::dom::DumpDocument(first) == toy::dom::DumpDocument(second),
        "parsing the same input twice produces the same dump");
}

}  // namespace

int main() {
  TestCaseTable();
  TestDocumentArena();
  TestDeepNestingDump();
  if (g_failures != 0) {
    std::fprintf(stderr, "dom parser test: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("dom parser test passed\n");
  return 0;
}
