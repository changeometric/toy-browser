# One executable per test subject; CTest names follow <module>.<topic>.

add_executable(layout_block_test block_test.cpp)
toybrowser_set_warnings(layout_block_test)
target_link_libraries(layout_block_test PRIVATE toy_layout toy_test_support)
add_test(NAME layout.block COMMAND layout_block_test)

add_executable(layout_inline_test inline_test.cpp)
toybrowser_set_warnings(layout_inline_test)
target_link_libraries(layout_inline_test PRIVATE toy_layout toy_test_support)
add_test(NAME layout.inline COMMAND layout_inline_test)

add_executable(layout_line_break_test line_break_test.cpp)
toybrowser_set_warnings(layout_line_break_test)
target_link_libraries(layout_line_break_test PRIVATE toy_layout toy_test_support)
add_test(NAME layout.line_break COMMAND layout_line_break_test)

add_executable(layout_dump_test dump_test.cpp)
toybrowser_set_warnings(layout_dump_test)
target_link_libraries(layout_dump_test PRIVATE toy_layout toy_test_support)
add_test(NAME layout.dump COMMAND layout_dump_test)

add_executable(layout_deep_nesting_test deep_nesting_test.cpp)
toybrowser_set_warnings(layout_deep_nesting_test)
target_link_libraries(layout_deep_nesting_test PRIVATE toy_layout toy_test_support)
add_test(NAME layout.deep_nesting COMMAND layout_deep_nesting_test)
