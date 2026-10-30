add_executable(shell_headless_test headless_test.cpp)
target_link_libraries(shell_headless_test PRIVATE toy_shell)
toybrowser_set_warnings(shell_headless_test)

add_test(NAME shell.headless_screenshot COMMAND shell_headless_test)

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
