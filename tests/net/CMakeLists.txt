# One executable per test subject; CTest names follow <module>.<topic>.

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Committed test-only TLS certificates (see tests/fixtures/tls/README.md).
set(TLS_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/tls")

add_executable(net_url_test url_test.cpp)
toybrowser_set_warnings(net_url_test)
target_link_libraries(net_url_test PRIVATE toy_net)
add_test(NAME net.url COMMAND net_url_test)

add_executable(net_url_resolution_test url_resolution_test.cpp)
toybrowser_set_warnings(net_url_resolution_test)
target_link_libraries(net_url_resolution_test PRIVATE toy_net toy_test_support)
add_test(NAME net.url_resolution COMMAND net_url_resolution_test)

add_executable(net_http_test http_test.cpp)
toybrowser_set_warnings(net_http_test)
target_link_libraries(net_http_test PRIVATE toy_net toy_test_support Threads::Threads OpenSSL::SSL
                                             OpenSSL::Crypto)
target_compile_definitions(net_http_test PRIVATE
                           "TOYBROWSER_TLS_FIXTURES=\"${TLS_FIXTURES_DIR}\"")
add_test(NAME net.http COMMAND net_http_test)
# The https redirect case trusts only the committed test CA.
set_tests_properties(net.http PROPERTIES ENVIRONMENT
                     "SSL_CERT_FILE=${TLS_FIXTURES_DIR}/test_ca.pem")

add_executable(net_tls_test tls_test.cpp)
toybrowser_set_warnings(net_tls_test)
target_link_libraries(net_tls_test PRIVATE toy_net toy_test_support Threads::Threads OpenSSL::SSL
                                          OpenSSL::Crypto)
target_compile_definitions(net_tls_test PRIVATE
                           "TOYBROWSER_TLS_FIXTURES=\"${TLS_FIXTURES_DIR}\"")
add_test(NAME net.tls COMMAND net_tls_test)
# The transport creates its SSL_CTX lazily from OpenSSL's default verify paths, so
# pointing SSL_CERT_FILE at the test CA is enough to trust the loopback server.
set_tests_properties(net.tls PROPERTIES ENVIRONMENT
                     "SSL_CERT_FILE=${TLS_FIXTURES_DIR}/test_ca.pem")

add_executable(net_file_test file_test.cpp)
toybrowser_set_warnings(net_file_test)
target_link_libraries(net_file_test PRIVATE toy_net)
add_test(NAME net.file COMMAND net_file_test)
