# Networking module: URL parsing and blocking HTTP/1.1 and file:// fetching, with TLS
# for https URLs through the approved OpenSSL library.

find_package(OpenSSL REQUIRED)

add_library(toy_net STATIC
  url.cpp
  http.cpp
  socket.cpp
  stream.cpp
  tls.cpp
)

target_include_directories(toy_net PUBLIC include)
target_compile_features(toy_net PUBLIC cxx_std_20)
target_link_libraries(toy_net PRIVATE OpenSSL::SSL OpenSSL::Crypto)

toybrowser_set_warnings(toy_net)
