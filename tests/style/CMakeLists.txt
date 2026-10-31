# One executable per test subject; CTest names follow <module>.<topic>.

add_executable(style_cascade_test cascade_test.cpp)
toybrowser_set_warnings(style_cascade_test)
target_link_libraries(style_cascade_test PRIVATE toy_style)
add_test(NAME style.cascade COMMAND style_cascade_test)

add_executable(style_dump_test dump_test.cpp)
toybrowser_set_warnings(style_dump_test)
target_link_libraries(style_dump_test PRIVATE toy_style)
add_test(NAME style.dump COMMAND style_dump_test)

add_executable(style_parser_test parser_test.cpp)
toybrowser_set_warnings(style_parser_test)
target_link_libraries(style_parser_test PRIVATE toy_style toy_test_support)
add_test(NAME style.parser COMMAND style_parser_test)

add_executable(style_external_sheet_test external_sheet_test.cpp)
toybrowser_set_warnings(style_external_sheet_test)
target_link_libraries(style_external_sheet_test PRIVATE toy_style toy_test_support)
add_test(NAME style.external_sheets COMMAND style_external_sheet_test)

add_executable(style_values_test values_test.cpp)
toybrowser_set_warnings(style_values_test)
target_link_libraries(style_values_test PRIVATE toy_style toy_test_support)
add_test(NAME style.values COMMAND style_values_test)

add_executable(style_typography_test typography_test.cpp)
toybrowser_set_warnings(style_typography_test)
target_link_libraries(style_typography_test PRIVATE toy_style toy_test_support)
add_test(NAME style.typography COMMAND style_typography_test)
