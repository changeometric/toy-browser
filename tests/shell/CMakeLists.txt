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
