# One executable per test subject; CTest names follow <module>.<topic>.

add_executable(render_surface_test surface_test.cpp)
toybrowser_set_warnings(render_surface_test)
target_link_libraries(render_surface_test PRIVATE toy_render)
add_test(NAME render.surface COMMAND render_surface_test)

add_executable(render_ppm_test ppm_test.cpp)
toybrowser_set_warnings(render_ppm_test)
target_link_libraries(render_ppm_test PRIVATE toy_render)
add_test(NAME render.ppm COMMAND render_ppm_test)

add_executable(render_font_test font_test.cpp)
toybrowser_set_warnings(render_font_test)
target_link_libraries(render_font_test PRIVATE toy_render toy_test_support)
add_test(NAME render.font_metrics COMMAND render_font_test)
