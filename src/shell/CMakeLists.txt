add_library(toy_shell STATIC
  application.cpp
)

target_include_directories(toy_shell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(toy_shell PUBLIC toy_render toy_platform toy_net)

toybrowser_set_warnings(toy_shell)
