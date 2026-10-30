# One executable per test subject; CTest names follow <module>.<topic>.

find_package(Threads REQUIRED)

add_executable(net_url_test url_test.cpp)
toybrowser_set_warnings(net_url_test)
target_link_libraries(net_url_test PRIVATE toy_net)
add_test(NAME net.url COMMAND net_url_test)

add_executable(net_http_test http_test.cpp)
toybrowser_set_warnings(net_http_test)
target_link_libraries(net_http_test PRIVATE toy_net Threads::Threads)
add_test(NAME net.http COMMAND net_http_test)

add_executable(net_file_test file_test.cpp)
toybrowser_set_warnings(net_file_test)
target_link_libraries(net_file_test PRIVATE toy_net)
add_test(NAME net.file COMMAND net_file_test)
