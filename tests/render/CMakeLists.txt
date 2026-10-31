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

add_executable(render_paint_test paint_test.cpp)
toybrowser_set_warnings(render_paint_test)
target_link_libraries(render_paint_test PRIVATE toy_render toy_test_support)
add_test(NAME render.paint COMMAND render_paint_test "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/fonts")

add_executable(render_image_test image_test.cpp)
toybrowser_set_warnings(render_image_test)
target_link_libraries(render_image_test PRIVATE toy_render toy_test_support)
add_test(NAME render.image COMMAND render_image_test "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/images")

add_executable(render_font_ttf_test font_ttf_test.cpp)
toybrowser_set_warnings(render_font_ttf_test)
target_link_libraries(render_font_ttf_test PRIVATE toy_render toy_test_support)
add_test(NAME render.font_ttf COMMAND render_font_ttf_test "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/fonts")
