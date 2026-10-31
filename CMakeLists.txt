cmake_minimum_required(VERSION 3.20)

project(toybrowser
  VERSION 0.1.0
  DESCRIPTION "A toy web browser built from scratch"
  LANGUAGES CXX)

# Nothing in the build may depend on one machine's paths, toolchain version or
# installed package set: only a conforming C++20 compiler and CMake are assumed.

# Default to an optimised build. The browser is CPU-bound (text shaping, layout,
# rasterization); an unoptimised default made normal pages several times slower.
# Single-config generators only: multi-config generators choose per-config types.
# Override with -DCMAKE_BUILD_TYPE=Debug/Release/... as usual.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
      "Build type (defaults to RelWithDebInfo; pass Debug/Release/... to override)" FORCE)
  message(STATUS "toybrowser: defaulting CMAKE_BUILD_TYPE to RelWithDebInfo")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# compile_commands.json for clangd and IDEs; generated inside the build directory,
# which stays untracked.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Tests are opt-in so a plain `cmake --build` only compiles the browser itself.
# CTest executables are numerous; enable them explicitly for verification with
# -DTOYBROWSER_BUILD_TESTS=ON (see docs/conventions.md).
option(TOYBROWSER_BUILD_TESTS "Build the unit tests" OFF)
option(TOYBROWSER_WERROR "Treat compiler warnings as errors" OFF)

if(NOT TOYBROWSER_BUILD_TESTS)
  message(STATUS
    "toybrowser: tests are not built (pass -DTOYBROWSER_BUILD_TESTS=ON to build and run them)")
endif()

# Applied to every first-party target. Module targets added in later milestones
# call this from their own CMakeLists.txt.
function(toybrowser_set_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    # Deterministic floating point: optimisation must not fuse multiply-adds,
    # so layout geometry stays byte-identical across build types and compilers.
    target_compile_options(${target} PRIVATE -ffp-contract=off)
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

# Explicit module list: deterministic and easy to audit. The Lead adds a module
# name here (and in tests/CMakeLists.txt) once when the module is introduced;
# directories without a CMakeLists.txt are skipped, so names may be added early.
foreach(module render platform net dom css style layout paint shell)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${module}/CMakeLists.txt")
    add_subdirectory(src/${module})
  endif()
endforeach()

if(TARGET toy_shell)
  target_link_libraries(toybrowser PRIVATE toy_shell)
endif()

include(GNUInstallDirs)
install(TARGETS toybrowser RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md README.zh.md LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

if(TOYBROWSER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
