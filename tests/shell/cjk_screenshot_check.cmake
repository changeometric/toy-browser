# M10 CLI check, run by CTest as one test:
#   cmake -DTOYBROWSER=<toybrowser> -DFIXTURE_DIR=<cjk_site> -DOUTPUT=<ppm> \
#         -P cjk_screenshot_check.cmake
#
# One headless --screenshot run over the committed CJK fixture site (with the
# fixture fontconfig config supplied through the CTest ENVIRONMENT) must exit 0,
# write a binary 320x288 PPM (the full fixture page, including the wrapped Han
# paragraph) and paint real anti-aliased glyphs at the frozen text regions:
# heading, paragraphs, bold/italic text and the two wrapped lines of Han fallback
# text. The fixture glyphs are synthetic block art, so this check counts ink
# presence and blended (anti-aliased) pixels instead of asserting exact coverage
# bytes; the exact box geometry is asserted by shell.cjk_dump_boxes/shell.cjk_layout
# and documented in tests/fixtures/cjk_site/README.md.

foreach(required TOYBROWSER FIXTURE_DIR OUTPUT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "cjk_screenshot_check.cmake: missing -D${required}")
  endif()
endforeach()

set(width 320)
set(height 288)
set(header_size 15)  # "P6\n" + "320 288\n" + "255\n"

file(REMOVE "${OUTPUT}")
execute_process(
  COMMAND "${TOYBROWSER}" --size 320x288 --screenshot "${OUTPUT}"
          "file://${FIXTURE_DIR}/index.html"
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
# explicit prefix and compare the exact P6 header.
file(READ "${OUTPUT}" ppm_head LIMIT 15)
string(SUBSTRING "${ppm_head}" 0 15 ppm_header)
if(NOT ppm_header STREQUAL "P6\n320 288\n255\n")
  message(FATAL_ERROR "screenshot does not start with a 320x288 P6 header: '${ppm_header}'")
endif()

file(SIZE "${OUTPUT}" screenshot_size)
math(EXPR expected_size "${header_size} + ${width} * ${height} * 3")
if(NOT screenshot_size EQUAL expected_size)
  message(FATAL_ERROR "unexpected PPM size ${screenshot_size}, expected ${expected_size}")
endif()

# Read the raster as lowercase hex; every RGB pixel is six hex characters.
file(READ "${OUTPUT}" ppm_hex HEX)
string(TOLOWER "${ppm_hex}" ppm_hex)

# Counts non-white pixels ("ink") and pixels that are neither white nor opaque
# black ("blended") in a half-open device-pixel rectangle. Real-font rendering is
# anti-aliased, so every asserted region must contain blended pixels; the frozen
# bitmap path can never satisfy that.
function(check_ink_region name x0 y0 x1 y1)
  set(non_white 0)
  set(blended 0)
  math(EXPR last_y "${y1} - 1")
  math(EXPR last_x "${x1} - 1")
  foreach(y RANGE ${y0} ${last_y})
    foreach(x RANGE ${x0} ${last_x})
      math(EXPR pixel_index "${y} * ${width} + ${x}")
      math(EXPR hex_offset "${header_size} * 2 + ${pixel_index} * 6")
      string(SUBSTRING "${ppm_hex}" ${hex_offset} 6 pixel_hex)
      if(NOT pixel_hex STREQUAL "ffffff")
        math(EXPR non_white "${non_white} + 1")
        if(NOT pixel_hex STREQUAL "000000")
          math(EXPR blended "${blended} + 1")
        endif()
      endif()
    endforeach()
  endforeach()
  if(non_white EQUAL 0)
    message(FATAL_ERROR "no ink in the ${name} region (${x0},${y0})-(${x1},${y1})")
  endif()
  if(blended EQUAL 0)
    message(FATAL_ERROR
            "no anti-aliased pixels in the ${name} region (${x0},${y0})-(${x1},${y1}); "
            "the real-font path did not paint there")
  endif()
  message(STATUS "${name}: ${non_white} ink pixel(s), ${blended} blended")
endfunction()

# These rectangles are the frozen glyph extents of the fixture page at 320x288
# (documented in tests/fixtures/cjk_site/README.md): the 32 px heading, the 16 px
# mixed Latin/Han paragraph, the bold paragraph, the italic paragraph and the two
# wrapped lines of the long Han paragraph.
check_ink_region("heading" 8 20 76 60)
check_ink_region("paragraph-latin" 8 88 56 106)
check_ink_region("paragraph-han-fallback" 66 88 88 106)
check_ink_region("paragraph-bold" 8 136 42 154)
check_ink_region("paragraph-italic" 8 184 40 202)
check_ink_region("wrapped-line-1" 8 232 311 248)
check_ink_region("wrapped-line-2" 8 248 98 264)

message(STATUS "wrote ${OUTPUT} (${screenshot_size} bytes): ${screenshot_stdout}")
