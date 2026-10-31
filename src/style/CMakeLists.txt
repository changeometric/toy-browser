# CSS cascade and computed styles. The UA style sheet is embedded; external CSS
# text is fetched by the caller and supplied to the cascade at its <link>
# element.

add_library(toy_style STATIC
  compute_styles.cpp
  dump.cpp
  property_parser.cpp
  selector_matcher.cpp
  style_tree.cpp
  ua_stylesheet.cpp
)

target_include_directories(toy_style PUBLIC include)
target_compile_features(toy_style PUBLIC cxx_std_20)
target_link_libraries(toy_style PUBLIC toy_dom toy_css)

toybrowser_set_warnings(toy_style)
