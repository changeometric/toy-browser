add_executable(shell_headless_test headless_test.cpp)
target_link_libraries(shell_headless_test PRIVATE toy_shell)
toybrowser_set_warnings(shell_headless_test)

add_test(NAME shell.headless_screenshot COMMAND shell_headless_test)

# M7 headless navigation: drives toy::shell::Browser against the committed
# multi-page fixture site (file:// only; no display, no network).
add_executable(shell_navigation_test navigation_test.cpp)
target_link_libraries(shell_navigation_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(shell_navigation_test)
add_test(NAME shell.navigation
  COMMAND shell_navigation_test "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/nav_site")

# End-to-end --dump-body checks against local files: no network, no display.
set(shell_dump_body_fixture "${CMAKE_CURRENT_BINARY_DIR}/dump_body_fixture.txt")
file(WRITE "${shell_dump_body_fixture}" "M2 dump body fixture: shell end-to-end fetch\n")

add_test(NAME shell.dump_body_file
  COMMAND $<TARGET_FILE:toybrowser> --dump-body "file://${shell_dump_body_fixture}")
set_tests_properties(shell.dump_body_file PROPERTIES
  PASS_REGULAR_EXPRESSION "M2 dump body fixture: shell end-to-end fetch")

add_test(NAME shell.dump_body_missing_file
  COMMAND $<TARGET_FILE:toybrowser> --dump-body
          "file://${CMAKE_CURRENT_BINARY_DIR}/missing_dump_body_fixture.txt")
set_tests_properties(shell.dump_body_missing_file PROPERTIES WILL_FAIL TRUE)

# End-to-end --dump-dom check: parses a local HTML fixture; no network, no display.
set(shell_dump_dom_fixture "${CMAKE_CURRENT_BINARY_DIR}/dump_dom_fixture.html")
file(WRITE "${shell_dump_dom_fixture}"
     "<!doctype html><html><head><title>t</title></head><body><p class=\"x\">hi</p></body></html>\n")

add_test(NAME shell.dump_dom_file
  COMMAND $<TARGET_FILE:toybrowser> --dump-dom "file://${shell_dump_dom_fixture}")
set_tests_properties(shell.dump_dom_file PROPERTIES
  PASS_REGULAR_EXPRESSION "<p class=\"x\">\n *\"hi\"")

add_test(NAME shell.dump_dom_missing_file
  COMMAND $<TARGET_FILE:toybrowser> --dump-dom
          "file://${CMAKE_CURRENT_BINARY_DIR}/missing_dump_dom_fixture.html")
set_tests_properties(shell.dump_dom_missing_file PROPERTIES WILL_FAIL TRUE)

# Regression test: out-of-range window sizes must be rejected, never abort.
add_executable(shell_size_limit_test size_limit_test.cpp)
target_link_libraries(shell_size_limit_test PRIVATE toy_shell)
toybrowser_set_warnings(shell_size_limit_test)
add_test(NAME shell.size_limit COMMAND shell_size_limit_test)

# End-to-end --dump-style check: parses an inline <style> fixture; no network, no display.
set(shell_dump_style_fixture "${CMAKE_CURRENT_BINARY_DIR}/dump_style_fixture.html")
file(WRITE "${shell_dump_style_fixture}"
     "<!doctype html><html><head><style>p { color: #ff0000; margin: 4px 8px } h1 { display: block; font-size: 32px }</style></head><body><h1>Title</h1><p>hi</p></body></html>\n")

add_test(NAME shell.dump_style_file
  COMMAND $<TARGET_FILE:toybrowser> --dump-style "file://${shell_dump_style_fixture}")
set_tests_properties(shell.dump_style_file PROPERTIES
  PASS_REGULAR_EXPRESSION "<p> display=block color=#ff0000.*margin-top=4px")

add_test(NAME shell.dump_style_missing_file
  COMMAND $<TARGET_FILE:toybrowser> --dump-style
          "file://${CMAKE_CURRENT_BINARY_DIR}/missing_dump_style_fixture.html")
set_tests_properties(shell.dump_style_missing_file PROPERTIES WILL_FAIL TRUE)

# End-to-end --dump-boxes checks: the default 800px viewport and the UA body
# margin (8px) fix the geometry; the fixture's author rule overrides the UA p
# margin. No network, no display.
set(shell_dump_boxes_fixture "${CMAKE_CURRENT_BINARY_DIR}/dump_boxes_fixture.html")
file(WRITE "${shell_dump_boxes_fixture}"
     "<!doctype html><html><head><style>p { margin: 4px 8px }</style></head><body><p>Hello</p></body></html>\n")

# <body> x=8 y=8: viewport 800 wide, UA body margin 8 -> content width 784.
# <p> x=16 y=12: body content starts at x=8; the author margin 8px/4px shifts it.
# text "Hello" at 16px: five cells * 8px = 40px wide, one 16px line tall.
add_test(NAME shell.dump_boxes_file
  COMMAND $<TARGET_FILE:toybrowser> --dump-boxes "file://${shell_dump_boxes_fixture}")
set_tests_properties(shell.dump_boxes_file PROPERTIES
  PASS_REGULAR_EXPRESSION "<html> block x=0 y=0 width=800 height=40\n *<body> block x=8 y=8 width=784 height=24\n *<p> block x=16 y=12 width=768 height=16\n *text \"Hello\" x=16 y=12 width=40 height=16")

add_test(NAME shell.dump_boxes_missing_file
  COMMAND $<TARGET_FILE:toybrowser> --dump-boxes
          "file://${CMAKE_CURRENT_BINARY_DIR}/missing_dump_boxes_fixture.html")
set_tests_properties(shell.dump_boxes_missing_file PROPERTIES WILL_FAIL TRUE)

# The box layout viewport width follows --size, not the default 800.
add_test(NAME shell.dump_boxes_size
  COMMAND $<TARGET_FILE:toybrowser> --size 400x300 --dump-boxes
          "file://${shell_dump_boxes_fixture}")
set_tests_properties(shell.dump_boxes_size PROPERTIES
  PASS_REGULAR_EXPRESSION "<html> block x=0 y=0 width=400 height=40\n *<body> block x=8 y=8 width=384 height=24")

# Usage errors exit 2: a missing URL argument and conflicting headless modes.
add_test(NAME shell.dump_boxes_missing_url
  COMMAND $<TARGET_FILE:toybrowser> --dump-boxes)
set_tests_properties(shell.dump_boxes_missing_url PROPERTIES WILL_FAIL TRUE)

add_test(NAME shell.dump_boxes_conflict
  COMMAND $<TARGET_FILE:toybrowser> --dump-boxes "file://${shell_dump_boxes_fixture}"
          --dump-dom "file://${shell_dump_boxes_fixture}")
set_tests_properties(shell.dump_boxes_conflict PROPERTIES WILL_FAIL TRUE)

# M6 end-to-end pixel path: fetch -> parse -> style -> layout -> paint ->
# rasterize -> PPM, all headless and deterministic. The fixture's frozen M5
# geometry (200x100 viewport): h1 text cell x=8..40 y=24..56 in red, p cell
# x=8..48 y=88..100 in black, white page elsewhere.
set(shell_screenshot_page_fixture
    "${CMAKE_CURRENT_BINARY_DIR}/screenshot_page_fixture.html")
file(WRITE "${shell_screenshot_page_fixture}"
     "<!doctype html><html><head><style>h1 { color: #ff0000 }</style></head><body><h1>AB</h1><p>Hello</p></body></html>\n")

add_executable(shell_screenshot_page_test screenshot_page_test.cpp)
target_link_libraries(shell_screenshot_page_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(shell_screenshot_page_test)
add_test(NAME shell.screenshot_page
  COMMAND shell_screenshot_page_test "${shell_screenshot_page_fixture}"
          "${CMAKE_CURRENT_BINARY_DIR}/screenshot_page.ppm")

# CLI contract for the optional positional page URL: at most one URL, never
# together with a --dump-* mode, and a screenshot of a missing page is a runtime
# error. CTest only checks the non-zero status; the contract's exact exit codes
# are 2 (usage error) and 1 (runtime error).
add_test(NAME shell.cli_positional_url_twice
  COMMAND $<TARGET_FILE:toybrowser> "file://${shell_screenshot_page_fixture}"
          "file://${shell_screenshot_page_fixture}")
set_tests_properties(shell.cli_positional_url_twice PROPERTIES WILL_FAIL TRUE)

add_test(NAME shell.cli_positional_url_with_dump_dom
  COMMAND $<TARGET_FILE:toybrowser> --dump-dom "file://${shell_dump_dom_fixture}"
          "file://${shell_screenshot_page_fixture}")
set_tests_properties(shell.cli_positional_url_with_dump_dom PROPERTIES WILL_FAIL TRUE)

add_test(NAME shell.cli_screenshot_missing_page
  COMMAND $<TARGET_FILE:toybrowser>
          --screenshot "${CMAKE_CURRENT_BINARY_DIR}/missing_page.ppm"
          "file://${CMAKE_CURRENT_BINARY_DIR}/missing_screenshot_page.html")
set_tests_properties(shell.cli_screenshot_missing_page PROPERTIES WILL_FAIL TRUE)

# M8a image pipeline: the committed image fixture site loads through the same
# shell page loader as the window/screenshot path, so the decoded images reserve
# real boxes in the box dump and paint end to end. file:// only, no display, no
# network. Geometry is frozen from `toybrowser --size 320x200 --dump-boxes`.
set(shell_img_site "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/img_site")

add_executable(images_test images_test.cpp)
target_link_libraries(images_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(images_test)

add_test(NAME shell.images COMMAND images_test "${shell_img_site}")

# The 1:1 image keeps the decoded intrinsic size (16x8) and the CSS-scaled image
# reserves its specified 64x32 box, while the page keeps its frozen height: this
# proves --dump-boxes goes through the same image loader as painting.
add_test(NAME shell.images_dump_boxes
  COMMAND $<TARGET_FILE:toybrowser> --size 320x200 --dump-boxes
          "file://${shell_img_site}/index.html")
set_tests_properties(shell.images_dump_boxes PROPERTIES
  PASS_REGULAR_EXPRESSION
    "  <body> block x=8 y=8 width=304 height=552\n    <p> block x=8 y=24 width=304 height=16\n      text \"Before\" x=8 y=24 width=48 height=16\n      <img> inline x=64 y=24 width=16 height=8\n      text \"after\" x=88 y=24 width=40 height=16\n    <p> block x=8 y=72 width=304 height=32\n      <img> inline x=8 y=72 width=64 height=32\n")

# The deliberately missing image is skipped: the two text boxes are adjacent in
# the dump (no broken-image box between them) and the page still loads and dumps.
add_test(NAME shell.images_dump_boxes_missing_image
  COMMAND $<TARGET_FILE:toybrowser> --size 320x200 --dump-boxes
          "file://${shell_img_site}/index.html")
set_tests_properties(shell.images_dump_boxes_missing_image PROPERTIES
  PASS_REGULAR_EXPRESSION
    "      text \"Missing\" x=8 y=272 width=56 height=16\n      text \"image\" x=72 y=272 width=40 height=16\n")

# --screenshot over the image fixture must succeed and write one binary PPM of
# the requested size (the pixel content is asserted by shell.images).
add_test(NAME shell.images_screenshot
  COMMAND ${CMAKE_COMMAND}
          -DTOYBROWSER=$<TARGET_FILE:toybrowser>
          -DFIXTURE_DIR=${shell_img_site}
          -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/images_screenshot.ppm
          -P "${CMAKE_CURRENT_SOURCE_DIR}/images_screenshot_check.cmake")

# M8b real-font selection: --font loads a font file, --font-family resolves one
# through fontconfig, and both drive layout metrics and painting. The committed
# fixture font (tests/fixtures/fonts/toy_test.ttf, family "Toy Test Font") keeps
# the tests independent of installed fonts; the fontconfig test points
# FONTCONFIG_FILE at a generated test-local config whose only <dir> is the fixture
# fonts directory, so the family resolves deterministically on any machine.
set(shell_font_file "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/fonts/toy_test.ttf")
set(shell_font_site "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/font_site")

# M10 deterministic fontconfig configurations. Automatic system-font selection is
# the default now, so tests must never read the machine's font configuration or
# installed fonts. Two configs are generated in the build tree at configure time:
#
# - the fixture config: `tests/fixtures/fonts` is the only <dir>, and pattern
#   rules map the generic CSS families to the Latin fixture family. With it,
#   `serif`, `sans`, `sans-serif` and `monospace` resolve to "Toy Test Font"
#   (Bold for font-weight: 700, Italic for font-style: italic) while "Toy CJK"
#   stays the character-coverage fallback for Han text. fontconfig normalizes
#   `sans-serif` to the family `sans`, so both names need a rule.
# - the empty config: its only <dir> does not exist, so fontconfig finds no font
#   and the shell degrades to the frozen built-in bitmap font. Every pre-M10 test
#   that goes through the automatic selection runs with this config, keeping all
#   pre-M10 goldens byte-identical.
set(shell_fontconfig_dir "${CMAKE_CURRENT_BINARY_DIR}/fontconfig")
set(shell_fontconfig_fixture_file "${shell_fontconfig_dir}/fonts.conf")
set(shell_fontconfig_empty_file "${shell_fontconfig_dir}/empty-fonts.conf")
file(MAKE_DIRECTORY "${shell_fontconfig_dir}/cache")
file(MAKE_DIRECTORY "${shell_fontconfig_dir}/empty-cache")
set(shell_fontconfig_rules "")
foreach(shell_generic_family serif sans sans-serif monospace)
  string(APPEND shell_fontconfig_rules
    "  <match target=\"pattern\">\n"
    "    <test name=\"family\" qual=\"any\" compare=\"eq\"><string>${shell_generic_family}</string></test>\n"
    "    <edit name=\"family\" mode=\"assign\" binding=\"same\"><string>Toy Test Font</string></edit>\n"
    "  </match>\n")
endforeach()
file(WRITE "${shell_fontconfig_fixture_file}"
  "<?xml version=\"1.0\"?>\n"
  "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
  "<fontconfig>\n"
  "  <dir>${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/fonts</dir>\n"
  "  <cachedir>${shell_fontconfig_dir}/cache</cachedir>\n"
  "${shell_fontconfig_rules}"
  "</fontconfig>\n")
file(WRITE "${shell_fontconfig_empty_file}"
  "<?xml version=\"1.0\"?>\n"
  "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
  "<fontconfig>\n"
  "  <dir>${shell_fontconfig_dir}/nonexistent-fonts</dir>\n"
  "  <cachedir>${shell_fontconfig_dir}/empty-cache</cachedir>\n"
  "</fontconfig>\n")

add_executable(fonts_test fonts_test.cpp)
target_link_libraries(fonts_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(fonts_test)
add_test(NAME shell.fonts COMMAND fonts_test "${shell_font_site}")

# Real-font metrics are visible in the box dump: with the monospace fixture font
# one 32 px glyph advances 19.2 px, so "ABC" is 57.6 -> 58 px wide instead of the
# bitmap font's 48 px. Geometry is frozen from the `--font` dump below.
add_test(NAME shell.fonts_dump_boxes
  COMMAND $<TARGET_FILE:toybrowser> --font "${shell_font_file}" --size 320x200 --dump-boxes
          "file://${shell_font_site}/index.html")
set_tests_properties(shell.fonts_dump_boxes PROPERTIES
  PASS_REGULAR_EXPRESSION
    "    <h1> block x=8 y=24 width=304 height=32\n      text \"ABC\" x=8 y=24 width=58 height=32\n    <p> block x=8 y=88 width=304 height=16\n      text \"AB\" x=8 y=88 width=19 height=16\n      text \"Cab\" x=37 y=88 width=29 height=16\n")

# A fontconfig family lookup sees the committed fixture fonts only, so "Toy Test
# Font" is unambiguous. Unlike --font (one face for all text), the M10
# --font-family path follows the computed CSS weight: the UA rule
# `h1 { font-weight: 700 }` selects the bold fixture face, so "ABC" at 32 px is
# 62 px wide (three 0.65 em advances) instead of the regular 58 px.
add_test(NAME shell.fonts_dump_boxes_family
  COMMAND $<TARGET_FILE:toybrowser> --font-family "Toy Test Font" --size 320x200 --dump-boxes
          "file://${shell_font_site}/index.html")
set_tests_properties(shell.fonts_dump_boxes_family PROPERTIES
  ENVIRONMENT "FONTCONFIG_FILE=${shell_fontconfig_fixture_file}"
  PASS_REGULAR_EXPRESSION
    "    <h1> block x=8 y=24 width=304 height=32\n      text \"ABC\" x=8 y=24 width=62 height=32\n    <p> block x=8 y=88 width=304 height=16\n      text \"AB\" x=8 y=88 width=19 height=16\n      text \"Cab\" x=37 y=88 width=29 height=16\n")

# A font that cannot be loaded is a runtime error (exit 1), not a usage error and
# never a silent bitmap fallback.
add_test(NAME shell.fonts_missing_file
  COMMAND $<TARGET_FILE:toybrowser> --font "${CMAKE_CURRENT_BINARY_DIR}/missing_font.ttf"
          --size 320x200 --dump-boxes "file://${shell_font_site}/index.html")
set_tests_properties(shell.fonts_missing_file PROPERTIES WILL_FAIL TRUE)

# The font flags never apply to the image-free dump modes (usage error).
add_test(NAME shell.fonts_with_dump_dom
  COMMAND $<TARGET_FILE:toybrowser> --font "${shell_font_file}" --dump-dom
          "file://${shell_font_site}/index.html")
set_tests_properties(shell.fonts_with_dump_dom PROPERTIES WILL_FAIL TRUE)

# At most one font selection may be given.
add_test(NAME shell.fonts_flags_conflict
  COMMAND $<TARGET_FILE:toybrowser> --font "${shell_font_file}" --font-family "Toy Test Font"
          --size 320x200 --dump-boxes "file://${shell_font_site}/index.html")
set_tests_properties(shell.fonts_flags_conflict PROPERTIES WILL_FAIL TRUE)

# --font and --font-family each require a non-empty argument.
add_test(NAME shell.fonts_missing_argument
  COMMAND $<TARGET_FILE:toybrowser> --font)
set_tests_properties(shell.fonts_missing_argument PROPERTIES WILL_FAIL TRUE)

# --font with --screenshot must render and write one binary PPM (the real-font
# pixels themselves are asserted by shell.fonts).
add_test(NAME shell.fonts_screenshot
  COMMAND ${CMAKE_COMMAND}
          -DTOYBROWSER=$<TARGET_FILE:toybrowser>
          -DFONT_FILE=${shell_font_file}
          -DFIXTURE_DIR=${shell_font_site}
          -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fonts_screenshot.ppm
          -P "${CMAKE_CURRENT_SOURCE_DIR}/fonts_screenshot_check.cmake")

# M9 end-to-end HTTPS: the committed https_site page is served from memory over an in-process
# TLS loopback server (tests/support/tls_server.h) and rendered through toy::shell::Browser,
# including its PNG over the same transport. The test proves the TLS transport works end to end
# through the shell; trust comes only from the committed test CA via SSL_CERT_FILE in the CTest
# ENVIRONMENT (never in code), so no public network, display or installed certificate is needed.
find_package(OpenSSL REQUIRED)

set(shell_https_site "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/https_site")
set(shell_tls_fixtures "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/tls")

add_executable(shell_https_test https_test.cpp)
target_link_libraries(shell_https_test PRIVATE
  toy_shell toy_test_support OpenSSL::SSL OpenSSL::Crypto)
toybrowser_set_warnings(shell_https_test)

add_test(NAME shell.https
  COMMAND shell_https_test "${shell_https_site}" "${shell_tls_fixtures}")
set_tests_properties(shell.https PROPERTIES
  ENVIRONMENT "SSL_CERT_FILE=${shell_tls_fixtures}/test_ca.pem")

# M12 external stylesheets: the shell private styled-document path discovers,
# filters and fetches <link rel="stylesheet"> sheets in document order, resolves
# their href against the page's final URL and cascades them with inline <style>
# and style attributes. The committed fixture site is file:// only and the C++
# test additionally uses the in-process loopback HTTP server for redirect/absolute
# URL/failure cases; no public network, display or installed font is involved.
set(shell_css_site "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/css_site")

add_executable(shell_external_css_test external_css_test.cpp)
# The test includes the shell-private page loader header on purpose (like
# shell.cjk_layout and shell.page_language).
target_include_directories(shell_external_css_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/shell")
target_link_libraries(shell_external_css_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(shell_external_css_test)
add_test(NAME shell.external_css
  COMMAND shell_external_css_test "${shell_css_site}")

# --dump-style uses the same styled-document path: the external sheet of
# index.html must be applied (the .external rule's color/background/font-size and
# margins come only from styles/site.css).
add_test(NAME shell.dump_style_external
  COMMAND $<TARGET_FILE:toybrowser> --dump-style
          "file://${shell_css_site}/index.html")
set_tests_properties(shell.dump_style_external PROPERTIES
  PASS_REGULAR_EXPRESSION
    "<p> display=block color=#123456 background-color=#abcdef font-size=24px.*margin-top=6px margin-right=10px margin-bottom=12px margin-left=14px")

# A page whose every <link> is filtered out or failing still dumps successfully
# and keeps the default style: stylesheet problems never fail the page.
add_test(NAME shell.dump_style_filtered_only
  COMMAND $<TARGET_FILE:toybrowser> --dump-style
          "file://${shell_css_site}/filtered_only.html")
set_tests_properties(shell.dump_style_filtered_only PROPERTIES
  PASS_REGULAR_EXPRESSION
    "<p> display=block color=#000000 background-color=transparent font-size=16px.*margin-top=16px")

# M13 resource loading: one net::Client per page load (keep-alive for the page, its
# stylesheets and its images) plus a per-load image URL cache. The loopback HTTP
# server keeps connections alive and counts connections/requests, so the test
# asserts reuse and deduplication deterministically; the committed image fixture
# supplies the decoded PNG bytes. No public network, display or installed font.
add_executable(shell_resource_loading_test resource_loading_test.cpp)
# The test includes the shell-private page loader header on purpose.
target_include_directories(shell_resource_loading_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/shell")
target_link_libraries(shell_resource_loading_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(shell_resource_loading_test)
add_test(NAME shell.resource_loading
  COMMAND shell_resource_loading_test "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/images")

# M14a CSS compatibility: the committed css_compat_site fixture exercises the
# batch-1 properties end to end (percent/rem lengths, color functions, the
# background shorthand, logical properties, min/max-width, auto margins,
# line-height, text-align, text-decoration and visibility). The C++ test loads
# the pages through the shell-private page loader with the deterministic bitmap
# font path and checks computed values, frozen box geometry and painted pixels.
# legacy.html uses only pre-M14 CSS and is the byte-compatibility fixture.
set(shell_css_compat_site "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/css_compat_site")

add_executable(shell_css_compat_test css_compat_test.cpp)
# The test includes the shell-private page loader header on purpose.
target_include_directories(shell_css_compat_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/shell")
target_link_libraries(shell_css_compat_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(shell_css_compat_test)
add_test(NAME shell.css_compat
  COMMAND shell_css_compat_test "${shell_css_compat_site}")

# --dump-style appends the M14a fields at the end of each line; the min/max box
# of values.html proves min/max-width, and the percentage/rem boxes prove the new
# length forms. Pre-M14 fields stay in their old positions.
add_test(NAME shell.dump_style_css_compat
  COMMAND $<TARGET_FILE:toybrowser> --dump-style
          "file://${shell_css_compat_site}/values.html")
set_tests_properties(shell.dump_style_css_compat PROPERTIES
  PASS_REGULAR_EXPRESSION
    "<div> display=block color=#000000 background-color=#ddccdd[^\n]*width=50px[^\n]*min-width=150px min-height=none max-width=200px")

# M14b: --dump-style appends the border/box-sizing/overflow fields after the M14a
# fields; the four-sided #four box of borders.html proves the used border widths,
# styles, resolved colors and box-sizing.
add_test(NAME shell.dump_style_css_compat_borders
  COMMAND $<TARGET_FILE:toybrowser> --dump-style
          "file://${shell_css_compat_site}/borders.html")
set_tests_properties(shell.dump_style_css_compat_borders PROPERTIES
  PASS_REGULAR_EXPRESSION
    "border-top-width=2px border-right-width=3px border-bottom-width=4px border-left-width=5px border-top-style=solid.*border-color=#ff0000 #00ff00 #0000ff #ffff00 box-sizing=content-box overflow=visible")

# The CLI presentation path over the same fixture: --screenshot must succeed and
# write one binary PPM of the requested size (pixels are asserted by
# shell.css_compat). The values page covers M14a and the borders page M14b.
add_test(NAME shell.css_compat_screenshot
  COMMAND ${CMAKE_COMMAND}
          -DTOYBROWSER=$<TARGET_FILE:toybrowser>
          -DFIXTURE_DIR=${shell_css_compat_site}
          -DPAGE=values.html
          -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/css_compat_screenshot.ppm
          -P "${CMAKE_CURRENT_SOURCE_DIR}/css_compat_screenshot_check.cmake")

add_test(NAME shell.css_compat_screenshot_borders
  COMMAND ${CMAKE_COMMAND}
          -DTOYBROWSER=$<TARGET_FILE:toybrowser>
          -DFIXTURE_DIR=${shell_css_compat_site}
          -DPAGE=borders.html
          -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/css_compat_screenshot_borders.ppm
          -P "${CMAKE_CURRENT_SOURCE_DIR}/css_compat_screenshot_check.cmake")

# M17 JPEG images: the shell page loader decodes images by CONTENT through
# render::DecodeImage, so a page that mixes PNG and JPEG works with no shell
# knowledge of the codec. The committed jpeg_site fixture uses the JPEG/PNG files
# from tests/fixtures/images/ (jpeg_rgb_16x8.jpg, jpeg_gray_16x8.jpg,
# jpeg_progressive_16x8.jpg, jpeg_truncated_16x8.jpg and flag_16x8.png) plus a
# missing.jpg: valid JPEGs paint, broken JPEGs are skipped silently. JPEG is
# lossy, so the C++ test compares pixels with the documented per-channel
# tolerance; file:// only, no network, display or installed font.
set(shell_jpeg_site "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/jpeg_site")

add_executable(shell_jpeg_test jpeg_test.cpp)
# The test includes the shell-private page loader header on purpose.
target_include_directories(shell_jpeg_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/shell")
target_link_libraries(shell_jpeg_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(shell_jpeg_test)
add_test(NAME shell.jpeg COMMAND shell_jpeg_test "${shell_jpeg_site}")

# --dump-boxes over the same fixture: the JPEG rows reserve their intrinsic 16x8
# boxes and the CSS-scaled progressive JPEG a 32x16 box, exactly like PNG, while
# the missing/truncated rows keep text only.
add_test(NAME shell.jpeg_dump_boxes
  COMMAND $<TARGET_FILE:toybrowser> --size 320x400 --dump-boxes
          "file://${shell_jpeg_site}/index.html")
set_tests_properties(shell.jpeg_dump_boxes PROPERTIES
  PASS_REGULAR_EXPRESSION
    "<img> inline x=8 y=24 width=16 height=8([^\n]*\n)* *<img> inline x=8 y=72 width=16 height=8([^\n]*\n)* *<img> inline x=8 y=120 width=32 height=16([^\n]*\n)* *<img> inline x=8 y=168 width=16 height=8([^\n]*\n)* *<img> inline x=24 y=168 width=16 height=8")

# M10: every pre-M10 test that runs toybrowser or a Browser through the automatic
# system-font selection uses the empty config, which forces the built-in bitmap
# font. Their outputs are the pre-M10 goldens, so they must stay byte-identical.
# Tests that need real fonts set the fixture config explicitly (shell.fonts_*,
# shell.font_selection, shell.cjk_*); the --font tests are unaffected because
# FromFile never consults fontconfig.
foreach(shell_bitmap_font_test IN ITEMS
    shell.headless_screenshot
    shell.navigation
    shell.dump_body_file
    shell.dump_body_missing_file
    shell.dump_dom_file
    shell.dump_dom_missing_file
    shell.size_limit
    shell.dump_style_file
    shell.dump_style_missing_file
    shell.dump_boxes_file
    shell.dump_boxes_missing_file
    shell.dump_boxes_size
    shell.dump_boxes_missing_url
    shell.dump_boxes_conflict
    shell.screenshot_page
    shell.cli_positional_url_twice
    shell.cli_positional_url_with_dump_dom
    shell.cli_screenshot_missing_page
    shell.images
    shell.images_dump_boxes
    shell.images_dump_boxes_missing_image
    shell.images_screenshot
    shell.external_css
    shell.dump_style_external
    shell.dump_style_filtered_only
    shell.resource_loading
    shell.css_compat
    shell.dump_style_css_compat
    shell.dump_style_css_compat_borders
    shell.css_compat_screenshot
    shell.css_compat_screenshot_borders
    shell.jpeg
    shell.jpeg_dump_boxes
    shell.fonts
    shell.fonts_dump_boxes
    shell.fonts_missing_file
    shell.fonts_with_dump_dom
    shell.fonts_flags_conflict
    shell.fonts_missing_argument
    shell.fonts_screenshot)
  set_tests_properties("${shell_bitmap_font_test}" PROPERTIES
    ENVIRONMENT "FONTCONFIG_FILE=${shell_fontconfig_empty_file}")
endforeach()

# M10 automatic font selection: Fonts::SystemDefault resolves the computed font
# family/weight/style and the page language through the generated fixture config,
# selects the committed fixture faces and splits mixed Latin/Han text into
# coverage runs. Deterministic on any machine (only tests/fixtures/fonts is
# visible to fontconfig).
add_executable(font_selection_test font_selection_test.cpp)
target_link_libraries(font_selection_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(font_selection_test)
add_test(NAME shell.font_selection
  COMMAND font_selection_test "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/fonts")
set_tests_properties(shell.font_selection PROPERTIES
  ENVIRONMENT "FONTCONFIG_FILE=${shell_fontconfig_fixture_file}")

# The same executable with the generated empty config proves the documented
# bitmap degradation: fontconfig has no font, so automatic selection returns the
# built-in bitmap metrics and no runs, while FromFile keeps working.
add_test(NAME shell.font_selection_bitmap
  COMMAND font_selection_test "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/fonts" bitmap)
set_tests_properties(shell.font_selection_bitmap PROPERTIES
  ENVIRONMENT "FONTCONFIG_FILE=${shell_fontconfig_empty_file}")

# M10 end-to-end CJK page: the committed cjk_site fixture has <html lang="zh-CN">
# and mixed Latin/Han text styled through CSS. The box dump proves automatic
# font selection/fallback geometry AND the post-CJK-line-breaking segmentation:
# every Han code point is its own word-level text box, so the heading lists three
# 22 px boxes (32 px, 0.7 em advance), "Hello 中文" lists the Latin word plus two
# 11 px Han boxes, and the 35-character long paragraph wraps into two lines
# (27 units ending at x=299, then 8 units on the next line at y=248, block height
# 32 px). The Latin anchors pin the total advances independently. Geometry and
# expected pixels are documented in tests/fixtures/cjk_site/README.md; the exact
# per-unit text contents are asserted by shell.cjk_layout.
set(shell_cjk_site "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/cjk_site")

add_test(NAME shell.cjk_dump_boxes
  COMMAND $<TARGET_FILE:toybrowser> --size 320x288 --dump-boxes
          "file://${shell_cjk_site}/index.html")
set_tests_properties(shell.cjk_dump_boxes PROPERTIES
  ENVIRONMENT "FONTCONFIG_FILE=${shell_fontconfig_fixture_file}"
  PASS_REGULAR_EXPRESSION
    "    <h1> block x=8 y=24 width=304 height=32\n      text \"[^\"]*\" x=8 y=24 width=22 height=32\n      text \"[^\"]*\" x=30 y=24 width=22 height=32\n      text \"[^\"]*\" x=53 y=24 width=22 height=32\n    <p> block x=8 y=88 width=304 height=16\n      text \"Hello\" x=8 y=88 width=48 height=16\n      text \"[^\"]*\" x=66 y=88 width=11 height=16\n      text \"[^\"]*\" x=77 y=88 width=11 height=16\n    <p> block x=8 y=136 width=304 height=16\n      text \"ABC\" x=8 y=136 width=31 height=16\n      text \"[^\"]*\" x=50 y=136 width=11 height=16\n    <p> block x=8 y=184 width=304 height=16\n      text \"abc\" x=8 y=184 width=30 height=16\n      text \"[^\"]*\" x=48 y=184 width=11 height=16\n      text \"Hello\" x=69 y=184 width=50 height=16\n    <p> block x=8 y=232 width=304 height=32\n      text \"[^\"]*\" x=8 y=232 width=11 height=16\n([^\n]*\n)*      text \"[^\"]*\" x=299 y=232 width=11 height=16\n      text \"[^\"]*\" x=8 y=248 width=11 height=16\n([^\n]*\n)*      text \"[^\"]*\" x=86 y=248 width=11 height=16")

# Per-unit text-content assertion for the same page: the CLI regex above freezes
# positions/widths but cannot spell out the Han characters, so this C++ test loads
# the fixture through the shell-private loader and checks the exact text-box
# sequence (one Han code point per box, Latin words grouped), the wrapped paragraph
# line break and the per-line baseline offsets.
add_executable(shell_cjk_layout_test cjk_layout_test.cpp)
# The test includes the shell-private page loader header on purpose.
target_include_directories(shell_cjk_layout_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/shell")
target_link_libraries(shell_cjk_layout_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(shell_cjk_layout_test)
add_test(NAME shell.cjk_layout
  COMMAND shell_cjk_layout_test "${shell_cjk_site}")
set_tests_properties(shell.cjk_layout PROPERTIES
  ENVIRONMENT "FONTCONFIG_FILE=${shell_fontconfig_fixture_file}")

# The screenshot check renders the same page at 320x288 (the full page height) and
# verifies the PPM header/size plus ink presence and real-font anti-aliasing in
# the frozen text regions, including both wrapped lines of the long Han paragraph
# (the synthetic fixture glyphs are block art, so exact AA bytes are not asserted).
add_test(NAME shell.cjk_screenshot
  COMMAND ${CMAKE_COMMAND}
          -DTOYBROWSER=$<TARGET_FILE:toybrowser>
          -DFIXTURE_DIR=${shell_cjk_site}
          -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cjk_screenshot.ppm
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cjk_screenshot_check.cmake")
set_tests_properties(shell.cjk_screenshot PROPERTIES
  ENVIRONMENT "FONTCONFIG_FILE=${shell_fontconfig_fixture_file}")

# M10 page language hint: the shell-private loader extracts the root <html lang>
# value (trimmed, first attribute) for fontconfig. The fixtures are generated in
# the build tree; the test drives the private loader directly because the hint is
# not observable through the public Browser API with the fixture fonts. No fonts,
# network or display are involved.
set(shell_language_fixtures_dir "${CMAKE_CURRENT_BINARY_DIR}/page_language_fixtures")
file(MAKE_DIRECTORY "${shell_language_fixtures_dir}")
file(WRITE "${shell_language_fixtures_dir}/zh.html"
  "<!doctype html><html lang=\"zh-CN\"><body><p>x</p></body></html>\n")
file(WRITE "${shell_language_fixtures_dir}/trimmed.html"
  "<!doctype html><html lang=\"  zh-CN  \"><body><p>x</p></body></html>\n")
file(WRITE "${shell_language_fixtures_dir}/first_attribute.html"
  "<!doctype html><html lang=\"en\" lang=\"de\"><body><p>x</p></body></html>\n")
file(WRITE "${shell_language_fixtures_dir}/absent.html"
  "<!doctype html><html><body><p>x</p></body></html>\n")
file(WRITE "${shell_language_fixtures_dir}/body_language.html"
  "<!doctype html><html><body lang=\"fr\"><p lang=\"de\">x</p></body></html>\n")
file(WRITE "${shell_language_fixtures_dir}/empty_language.html"
  "<!doctype html><html lang=\"\"><body><p>x</p></body></html>\n")

add_executable(shell_page_language_test page_language_test.cpp)
# The test includes the shell-private page loader header on purpose.
target_include_directories(shell_page_language_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/shell")
target_link_libraries(shell_page_language_test PRIVATE toy_shell toy_test_support)
toybrowser_set_warnings(shell_page_language_test)
add_test(NAME shell.page_language
  COMMAND shell_page_language_test "${shell_language_fixtures_dir}")
