# One executable per test subject; CTest names follow <module>.<topic>.

add_executable(css_parser_test parser_test.cpp)
toybrowser_set_warnings(css_parser_test)
target_link_libraries(css_parser_test PRIVATE toy_css)
add_test(NAME css.parser COMMAND css_parser_test)
