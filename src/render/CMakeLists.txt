# Software rasterization primitives and image output. No HTML/CSS knowledge.

add_library(toy_render STATIC
  surface.cpp
  ppm.cpp
  font.cpp
)
target_include_directories(toy_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(toy_render PUBLIC cxx_std_20)
toybrowser_set_warnings(toy_render)
