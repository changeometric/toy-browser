# M14a CLI check, run by CTest as one test:
#   cmake -DTOYBROWSER=<toybrowser> -DFIXTURE_DIR=<css_compat_site> -DOUTPUT=<ppm> \
#         -P css_compat_screenshot_check.cmake
#
# One headless --screenshot run over the CSS-compatibility fixture site must exit
# 0 and write a binary PPM of the requested size. No display and no network are
# needed; the fixture site is file:// only. The pixel content is asserted by the
# shell.css_compat C++ test, this check only covers the CLI/presentation path.

foreach(required TOYBROWSER FIXTURE_DIR OUTPUT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "css_compat_screenshot_check.cmake: missing -D${required}")
  endif()
endforeach()

file(REMOVE "${OUTPUT}")
execute_process(
  COMMAND "${TOYBROWSER}" --size 320x400 --screenshot "${OUTPUT}"
          "file://${FIXTURE_DIR}/values.html"
  RESULT_VARIABLE screenshot_result
  OUTPUT_VARIABLE screenshot_stdout
  ERROR_VARIABLE screenshot_stderr)
if(NOT screenshot_result EQUAL 0)
  message(FATAL_ERROR "screenshot run failed (${screenshot_result}): ${screenshot_stderr}")
endif()
if(NOT EXISTS "${OUTPUT}")
  message(FATAL_ERROR "screenshot run did not write '${OUTPUT}'")
endif()

# CMake's file(READ LIMIT) can return more than the requested bytes, so take an
# explicit prefix and compare the exact P6 header
# ("P6\n" + "320 400\n" + "255\n" = 15 bytes).
file(READ "${OUTPUT}" ppm_head LIMIT 15)
string(SUBSTRING "${ppm_head}" 0 15 ppm_header)
if(NOT ppm_header STREQUAL "P6\n320 400\n255\n")
  message(FATAL_ERROR "screenshot does not start with a 320x400 P6 header: '${ppm_header}'")
endif()

file(SIZE "${OUTPUT}" screenshot_size)
set(expected_size 384015)
if(NOT screenshot_size EQUAL expected_size)
  message(FATAL_ERROR "unexpected PPM size ${screenshot_size}, expected ${expected_size}")
endif()
message(STATUS "wrote ${OUTPUT} (${screenshot_size} bytes): ${screenshot_stdout}")
