# HTML tokenizer, tree builder and arena DOM. Standard library only.

add_library(toy_dom STATIC
  document.cpp
  dump.cpp
  parser.cpp
  tokenizer.cpp
)

target_include_directories(toy_dom PUBLIC include)
target_compile_features(toy_dom PUBLIC cxx_std_20)

toybrowser_set_warnings(toy_dom)
