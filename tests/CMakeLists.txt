# One executable per test subject. Each is registered with CTest.
# Tests must be deterministic and must not need a network, a display or
# installed fonts.

add_executable(toolchain_smoke toolchain_smoke.cpp)
toybrowser_set_warnings(toolchain_smoke)
add_test(NAME toolchain.smoke COMMAND toolchain_smoke)

# Header-only assertion helpers for new test executables. Existing tests keep
# their local Check copies; migration is a separate refactor.
add_library(toy_test_support INTERFACE)
target_include_directories(toy_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

# Test directories follow the same explicit list as the source tree.
foreach(module render platform net dom css style layout shell)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${module}/CMakeLists.txt")
    add_subdirectory(${module})
  endif()
endforeach()
