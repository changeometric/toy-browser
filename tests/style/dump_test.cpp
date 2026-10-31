// Exact dump tests for the computed style tree.

#include <cstddef>
#include <cstdio>
#include <string>

#include "toy/dom/node.h"
#include "toy/style/computed_style.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "style dump test failed: %s\n", description);
    ++g_failures;
  }
}

void ExpectDump(const char* html, const char* expected, const char* description) {
  const toy::dom::Document document = toy::dom::ParseDocument(html);
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  const std::string actual = toy::style::DumpStyleTree(document, styles);
  if (actual == expected) {
    return;
  }
  std::fprintf(stderr, "style dump test failed: %s\ninput: %s\nexpected:\n%s\nactual:\n%s\n",
               description, html, expected, actual.c_str());
  ++g_failures;
}

void TestDumpCases() {
  // A minimal page: only element nodes are dumped, with DOM-depth indentation.
  ExpectDump(
      "<p>hi</p>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
      <p> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=16px margin-right=0px margin-bottom=16px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
)dom",
      "a minimal page dumps only element nodes");

  // Author rules, inline overrides, shorthands, em widths and opaque colors.
  ExpectDump(
      "<!doctype html><html><head><style>p { color: red; font-size: 20px; margin: 2px 4px; } "
      ".wide { width: 10em; padding: 1px; background-color: #abc; }</style></head>"
      "<body><div class=\"wide\"><p id=\"t\" style=\"margin-left: 3px\">x</p></div></body>"
      "</html>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
      <style> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
      <div> display=block color=#000000 background-color=#aabbcc font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=1px padding-right=1px padding-bottom=1px padding-left=1px width=160px height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
        <p> display=block color=#ff0000 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=2px margin-right=4px margin-bottom=2px margin-left=3px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#ff0000 #ff0000 #ff0000 #ff0000 box-sizing=content-box overflow=visible
)dom",
      "author rules, inline style and em lengths are dumped");

  // Lengths are rounded to the nearest integer; #rgb colors are expanded.
  ExpectDump(
      "<div style=\"width: 10.4px; margin-top: 2.5px; margin-bottom: 2.4px; "
      "font-size: 15.5px; color: #123; background-color: #abc\">x</div>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
      <div> display=block color=#112233 background-color=#aabbcc font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=3px margin-right=0px margin-bottom=2px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=10px height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#112233 #112233 #112233 #112233 box-sizing=content-box overflow=visible
)dom",
      "lengths are rounded and colors normalized");

  // Nested elements at normal depth keep their exact, uncapped indentation.
  ExpectDump(
      "<div><div><p>x</p></div></div>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
      <div> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
        <div> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
          <p> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=16px margin-right=0px margin-bottom=16px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
)dom",
      "nested elements at normal depth keep exact indentation");

  // Inline elements and the UA link color.
  ExpectDump(
      "<a href=\"/x\">link</a>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
      <a> display=inline color=#0000ee background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#0000ee #0000ee #0000ee #0000ee box-sizing=content-box overflow=visible
)dom",
      "inline elements and the UA link color are dumped");

  // M14: the appended length/typography fields: percentages, auto margins and
  // sizes, min/max constraints, line-height formats, alignment, visibility and
  // decoration, plus their inheritance.
  ExpectDump(
      "<!doctype html><html style=\"font-size: 20px\"><head></head><body>"
      "<div id=m style=\"width: 50%; height: 10rem; min-width: 2em; max-width: 400px; "
      "min-height: 10%; margin: 0 auto; padding: 1px 2% 3em; line-height: 1.5; "
      "text-align: justify; visibility: hidden; text-decoration: underline\">"
      "<p id=t style=\"width: auto\">x</p></div></body></html>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <head> display=none color=#000000 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <body> display=block color=#000000 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
      <div> display=block color=#000000 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=auto margin-bottom=0px margin-left=auto padding-top=1px padding-right=2% padding-bottom=60px padding-left=2% width=50% height=200px min-width=40px min-height=10% max-width=400px max-height=none line-height=1.5 text-align=justify visibility=hidden text-decoration=underline border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
        <p> display=block color=#000000 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=16px margin-right=0px margin-bottom=16px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=1.5 text-align=justify visibility=hidden text-decoration=underline border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
)dom",
      "the M14 appended fields are dumped with their frozen formatting");

  // A pre-M14 page: every old field is unchanged and only the appended fields
  // are new.
  ExpectDump(
      "<div class=\"legacy\"><p style=\"margin: 4px; color: #123\">x</p></div>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
      <div> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
        <p> display=block color=#112233 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=4px margin-right=4px margin-bottom=4px margin-left=4px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#112233 #112233 #112233 #112233 box-sizing=content-box overflow=visible
)dom",
      "a legacy page keeps its old fields and only appends the new defaults");

  // M14b: borders on all four sides (used width 0 for none/hidden), currentColor
  // and alpha colors resolved in the dump, decoration token list, box-sizing and
  // overflow, plus the non-inherited initial values on descendants.
  ExpectDump(
      "<!doctype html><html style=\"font-size: 20px\"><head></head><body>"
      "<div id=b style=\"color: #336699; border-top: 1px solid #ff0000; "
      "border-right: thick dashed #00ff0080; border-bottom: 2px groove currentColor; "
      "border-left: 3px dotted transparent; box-sizing: border-box; overflow-x: hidden; "
      "text-decoration: underline line-through overline\">"
      "<p id=p style=\"border-style: none hidden solid; overflow: visible hidden\">x</p>"
      "</div></body></html>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <head> display=none color=#000000 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <body> display=block color=#000000 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
      <div> display=block color=#336699 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=underline line-through overline border-top-width=1px border-right-width=5px border-bottom-width=2px border-left-width=3px border-top-style=solid border-right-style=dashed border-bottom-style=groove border-left-style=dotted border-color=#ff0000 #00ff00 #336699 #000000 box-sizing=border-box overflow=hidden
        <p> display=block color=#336699 background-color=transparent font-size=20px font-weight=400 font-style=normal font-family=serif margin-top=16px margin-right=0px margin-bottom=16px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=underline line-through overline border-top-width=0px border-right-width=0px border-bottom-width=3px border-left-width=0px border-top-style=none border-right-style=hidden border-bottom-style=solid border-left-style=hidden border-color=#336699 #336699 #336699 #336699 box-sizing=content-box overflow=hidden
)dom",
      "the M14b appended border/box-sizing/overflow fields are dumped");

  // An M14a page: the M14a fields are unchanged and only the appended M14b
  // fields are new.
  ExpectDump(
      "<div class=\"legacy\" style=\"font-size: 10px; width: 50%; min-width: 2em; "
      "line-height: 1.5; text-align: center; text-decoration: underline\">x</div>",
      R"dom(  <html> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <head> display=none color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
    <body> display=block color=#000000 background-color=transparent font-size=16px font-weight=400 font-style=normal font-family=serif margin-top=8px margin-right=8px margin-bottom=8px margin-left=8px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=auto height=auto min-width=none min-height=none max-width=none max-height=none line-height=normal text-align=left visibility=visible text-decoration=none border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
      <div> display=block color=#000000 background-color=transparent font-size=10px font-weight=400 font-style=normal font-family=serif margin-top=0px margin-right=0px margin-bottom=0px margin-left=0px padding-top=0px padding-right=0px padding-bottom=0px padding-left=0px width=50% height=auto min-width=20px min-height=none max-width=none max-height=none line-height=1.5 text-align=center visibility=visible text-decoration=underline border-top-width=0px border-right-width=0px border-bottom-width=0px border-left-width=0px border-top-style=none border-right-style=none border-bottom-style=none border-left-style=none border-color=#000000 #000000 #000000 #000000 box-sizing=content-box overflow=visible
)dom",
      "an M14a page only gains the appended M14b defaults");
}

// The font-family field: raw for plain [A-Za-z0-9_-] names, double-quoted with
// backslash escapes for everything else, items joined with ", ".
std::string DivFontFamilyField(const char* html) {
  const toy::dom::Document document = toy::dom::ParseDocument(html);
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  const std::string dump = toy::style::DumpStyleTree(document, styles);
  const std::size_t line = dump.find("<div> display=");
  const std::string marker = " font-family=";
  if (line == std::string::npos) {
    return "<missing div line>";
  }
  const std::size_t begin = dump.find(marker, line);
  if (begin == std::string::npos) {
    return "<missing font-family field>";
  }
  const std::size_t value_begin = begin + marker.size();
  const std::size_t value_end = dump.find(" margin-top=", value_begin);
  if (value_end == std::string::npos) {
    return "<missing field end>";
  }
  return dump.substr(value_begin, value_end - value_begin);
}

void TestFontFamilyDump() {
  Check(DivFontFamilyField("<div>x</div>") == "serif",
        "the initial font-family dumps as the raw generic name");
  Check(DivFontFamilyField("<div style='font-family: ToyFont'>x</div>") == "ToyFont",
        "a plain family name dumps raw");
  Check(DivFontFamilyField("<div style='font-family: \"Toy Test Font\", sans-serif'>x</div>") ==
            "\"Toy Test Font\", sans-serif",
        "a name with spaces is quoted, the list is joined with a comma and a space");
  Check(DivFontFamilyField("<div style='font-family: \"A\\\"B\"'>x</div>") == "\"A\\\"B\"",
        "a double quote inside a family name is escaped in the dump");
  Check(DivFontFamilyField("<div style='font-family: \"A\\\\B\"'>x</div>") == "\"A\\\\B\"",
        "a backslash inside a family name is escaped in the dump");
}

// Regression: the capped indent keeps the dump linear for deep nesting.
void TestDeepNestingDump() {
  std::string html;
  html.reserve(10000 * 5);
  for (int level = 0; level < 10000; ++level) {
    html.append("<div>");
  }
  const toy::dom::Document document = toy::dom::ParseDocument(html);
  Check(document.size() > 10000, "a deeply nested document is parsed");
  const toy::style::StyleTree styles = toy::style::ComputeStyles(document);
  const std::string dump = toy::style::DumpStyleTree(document, styles);
  Check(!dump.empty(), "a deeply nested style tree still dumps");
  Check(dump.back() == '\n', "a deeply nested dump ends with one newline");
  // About 10,000 style lines of roughly 800 bytes each: ~7.9 MB with the cap.
  // Without the cap the quadratic indentation would add ~100 MB, so this bound
  // fails loudly if the cap is removed while staying close to the real size.
  Check(dump.size() < 10U * 1024U * 1024U, "deep nesting stays far below the uncapped size");

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
  Check(max_indent <= 128, "style dump indentation is capped at 64 levels");
  Check(max_indent == 128, "a 10,000 level tree reaches the indentation cap");
}

}  // namespace

int main() {
  TestDumpCases();
  TestFontFamilyDump();
  TestDeepNestingDump();
  if (g_failures != 0) {
    std::fprintf(stderr, "style dump test: %d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("style dump test passed\n");
  return 0;
}
