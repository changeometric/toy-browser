# Installed-font discovery for --font-family (M8b). Approved dependency, see
# docs/dependencies.md; discovered through CMake only, no hardcoded version or
# path.
find_package(Fontconfig REQUIRED)

add_library(toy_shell STATIC
  application.cpp
  browser.cpp
  fonts.cpp
  page.cpp
)

target_include_directories(toy_shell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(toy_shell PUBLIC
  toy_render
  toy_platform
  toy_net
  toy_dom
  toy_style
  toy_layout
  toy_paint
)
# Fontconfig is a shell-only implementation detail: only the shell resolves a
# family name, and no other module or public header mentions it.
target_link_libraries(toy_shell PRIVATE Fontconfig::Fontconfig)

toybrowser_set_warnings(toy_shell)
