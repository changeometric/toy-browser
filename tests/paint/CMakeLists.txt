# One executable per test subject; CTest names follow <module>.<topic>.

add_executable(paint_display_list_test display_list_test.cpp)
toybrowser_set_warnings(paint_display_list_test)
target_link_libraries(paint_display_list_test PRIVATE toy_paint toy_test_support)
add_test(NAME paint.display_list COMMAND paint_display_list_test)
