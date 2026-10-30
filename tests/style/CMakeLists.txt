# One executable per test subject; CTest names follow <module>.<topic>.

add_executable(style_cascade_test cascade_test.cpp)
toybrowser_set_warnings(style_cascade_test)
target_link_libraries(style_cascade_test PRIVATE toy_style)
add_test(NAME style.cascade COMMAND style_cascade_test)

add_executable(style_dump_test dump_test.cpp)
toybrowser_set_warnings(style_dump_test)
target_link_libraries(style_dump_test PRIVATE toy_style)
add_test(NAME style.dump COMMAND style_dump_test)
