# One executable per test subject; CTest names follow <module>.<topic>.

add_executable(dom_parser_test parser_test.cpp)
toybrowser_set_warnings(dom_parser_test)
target_link_libraries(dom_parser_test PRIVATE toy_dom)
add_test(NAME dom.parser COMMAND dom_parser_test)

add_executable(dom_entities_test entities_test.cpp)
toybrowser_set_warnings(dom_entities_test)
target_link_libraries(dom_entities_test PRIVATE toy_dom)
add_test(NAME dom.entities COMMAND dom_entities_test)
