# Box tree, block/inline layout and line breaking. Text metrics come from a
# caller-supplied callback, so layout never depends on render.

add_library(toy_layout STATIC
  box.cpp
  dump.cpp
  geometry.cpp
  layout.cpp
  text_break.cpp
)

target_include_directories(toy_layout PUBLIC include)
target_compile_features(toy_layout PUBLIC cxx_std_20)
target_link_libraries(toy_layout PUBLIC toy_dom toy_style)

toybrowser_set_warnings(toy_layout)
