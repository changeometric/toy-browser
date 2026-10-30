# Display-list construction from the box tree. Pure: no rasterization and no
# knowledge of HTML/CSS beyond the computed values layout already consumed.

add_library(toy_paint STATIC
  paint.cpp
)

target_include_directories(toy_paint PUBLIC include)
target_compile_features(toy_paint PUBLIC cxx_std_20)
target_link_libraries(toy_paint PUBLIC toy_render toy_layout toy_style toy_dom)

toybrowser_set_warnings(toy_paint)
