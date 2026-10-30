# The platform module owns OS windows and input. In M1 that is an X11 backend;
# libX11 is approved in docs/dependencies.md. The headless path needs none of
# this, but the module itself is always built.

find_package(X11 REQUIRED)

add_library(toy_platform STATIC
  window.cpp
)

target_include_directories(toy_platform PUBLIC include)
target_link_libraries(toy_platform PUBLIC X11::X11)

toybrowser_set_warnings(toy_platform)
