cmake_minimum_required(VERSION 3.20)

project(toybrowser
  VERSION 0.1.0
  DESCRIPTION "A toy web browser built from scratch"
  LANGUAGES CXX)

# Nothing in the build may depend on one machine's paths, toolchain version or
# installed package set: only a conforming C++20 compiler and CMake are assumed.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# compile_commands.json for clangd and IDEs; generated inside the build directory,
# which stays untracked.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TOYBROWSER_BUILD_TESTS "Build the unit tests" ON)
option(TOYBROWSER_WERROR "Treat compiler warnings as errors" OFF)

# Applied to every first-party target. Module targets added in later milestones
# call this from their own CMakeLists.txt.
function(toybrowser_set_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(TOYBROWSER_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(TOYBROWSER_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  endif()
endfunction()

# The executable is the frontend; browser functionality lives in module
# libraries that appear milestone by milestone (see docs/architecture.md).
add_executable(toybrowser src/main.cpp)
target_compile_definitions(toybrowser PRIVATE TOYBROWSER_VERSION="${PROJECT_VERSION}")
toybrowser_set_warnings(toybrowser)

# Each module owns its CMakeLists.txt and is picked up as soon as the file
# exists, so parallel work on different modules never has to edit this file.
foreach(module render platform net dom css style shell)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${module}/CMakeLists.txt")
    add_subdirectory(src/${module})
  endif()
endforeach()

if(TARGET toy_shell)
  target_link_libraries(toybrowser PRIVATE toy_shell)
endif()

if(TOYBROWSER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
