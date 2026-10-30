# CSS scanner, parser and selector model. Standard library only; no DOM dependency.

add_library(toy_css STATIC
  parser.cpp
)

target_include_directories(toy_css PUBLIC include)
target_compile_features(toy_css PUBLIC cxx_std_20)

toybrowser_set_warnings(toy_css)
