# The platform test must not need a running X server: it forces an unusable
# DISPLAY and checks that window creation fails cleanly.
add_executable(platform_window_test window_test.cpp)
target_link_libraries(platform_window_test PRIVATE toy_platform)
toybrowser_set_warnings(platform_window_test)

add_test(NAME platform.window_requires_display COMMAND platform_window_test)
