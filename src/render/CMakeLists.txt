# Software rasterization primitives and image output. No HTML/CSS knowledge.
# PNG decoding uses the approved libpng dependency (see docs/dependencies.md).

find_package(PNG REQUIRED)

add_library(toy_render STATIC
  surface.cpp
  ppm.cpp
  font.cpp
  paint.cpp
  image.cpp
)
target_include_directories(toy_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(toy_render PUBLIC cxx_std_20)
target_link_libraries(toy_render PRIVATE PNG::PNG)
toybrowser_set_warnings(toy_render)
