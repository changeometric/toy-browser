# Networking module: URL parsing and blocking HTTP/1.1 and file:// fetching.

add_library(toy_net STATIC
  url.cpp
  http.cpp
)

target_include_directories(toy_net PUBLIC include)
target_compile_features(toy_net PUBLIC cxx_std_20)

toybrowser_set_warnings(toy_net)
