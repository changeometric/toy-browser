# Software rasterization primitives and image output. No HTML/CSS knowledge.
# Approved third-party codecs/font machinery (see docs/dependencies.md): libpng
# for PNG decoding, freetype for real-font rasterization and metrics, harfbuzz
# for text shaping. They stay PRIVATE to this module.

find_package(PNG REQUIRED)
find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)

add_library(toy_render STATIC
  surface.cpp
  ppm.cpp
  font.cpp
  font_ttf.cpp
  paint.cpp
  image.cpp
)
target_include_directories(toy_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(toy_render PUBLIC cxx_std_20)
target_link_libraries(toy_render PRIVATE PNG::PNG Freetype::Freetype harfbuzz::harfbuzz)
toybrowser_set_warnings(toy_render)
