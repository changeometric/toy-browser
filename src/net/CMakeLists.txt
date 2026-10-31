# Networking module: URL parsing and blocking HTTP/1.1 and file:// fetching, with TLS
# for https URLs through the approved OpenSSL library and transparent gzip decoding
# through the approved zlib (part of the libpng dependency).

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

add_library(toy_net STATIC
  url.cpp
  http.cpp
  socket.cpp
  stream.cpp
  tls.cpp
)

target_include_directories(toy_net PUBLIC include)
target_compile_features(toy_net PUBLIC cxx_std_20)
target_link_libraries(toy_net PRIVATE OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB)

toybrowser_set_warnings(toy_net)
