// Tests for toy::render::Surface: geometry, fills and rectangle clipping.

#include "toy/render/surface.h"

#include <cstdio>

namespace {

using toy::render::Surface;

int g_failures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "surface test failed: %s\n", description);
    ++g_failures;
  }
}

int CountPixels(const Surface& surface, toy::render::Argb color) {
  int count = 0;
  for (int y = 0; y < surface.height(); ++y) {
    for (int x = 0; x < surface.width(); ++x) {
      if (surface.At(x, y) == color) {
        ++count;
      }
    }
  }
  return count;
}

void TestGeometry() {
  Surface surface(4, 3);
  Check(surface.width() == 4, "width is 4");
  Check(surface.height() == 3, "height is 3");
  Check(surface.stride() == 4, "stride equals width");
  Check(surface.pixels() != nullptr, "pixels are allocated");
  Check(CountPixels(surface, 0) == 12, "new surface is transparent black");
}

void TestRowMajorLayout() {
  Surface surface(3, 2);
  surface.FillRect(1, 1, 1, 1, 0xFF112233);
  Check(surface.pixels()[1 * surface.stride() + 1] == 0xFF112233,
        "At(x, y) maps to pixels()[y * stride + x]");
}

void TestFill() {
  Surface surface(3, 2);
  surface.Fill(0xFF102030);
  Check(CountPixels(surface, 0xFF102030) == 6, "Fill covers every pixel");
}

void TestFillRect() {
  Surface surface(6, 4);
  surface.Fill(0xFF000000);
  surface.FillRect(1, 1, 2, 2, 0xFFFF0000);
  Check(surface.At(1, 1) == 0xFFFF0000, "FillRect fills its top-left pixel");
  Check(surface.At(2, 2) == 0xFFFF0000, "FillRect fills its bottom-right pixel");
  Check(surface.At(0, 1) != 0xFFFF0000, "FillRect does not paint left of the rectangle");
  Check(surface.At(3, 1) != 0xFFFF0000, "FillRect does not paint right of the rectangle");
  Check(surface.At(1, 0) != 0xFFFF0000, "FillRect does not paint above the rectangle");
  Check(surface.At(1, 3) != 0xFFFF0000, "FillRect does not paint below the rectangle");
  Check(CountPixels(surface, 0xFFFF0000) == 4, "FillRect paints exactly w * h pixels");
}

void TestFillRectClipping() {
  // Negative origin: only the pixel at (0, 0) falls inside the surface.
  Surface surface(4, 4);
  surface.Fill(0xFF000000);
  surface.FillRect(-2, -2, 3, 3, 0xFF00FF00);
  Check(surface.At(0, 0) == 0xFF00FF00, "clipped FillRect paints the in-bounds corner");
  Check(CountPixels(surface, 0xFF00FF00) == 1, "clipped FillRect paints only the in-bounds area");

  // Rectangle extending past the right and bottom edges.
  surface.Fill(0xFF000000);
  surface.FillRect(3, 3, 10, 10, 0xFF0000FF);
  Check(surface.At(3, 3) == 0xFF0000FF, "clipped FillRect paints the in-bounds corner");
  Check(CountPixels(surface, 0xFF0000FF) == 1, "clipped FillRect paints only the in-bounds area");

  // Rectangle larger than the surface paints everything.
  surface.Fill(0xFF000000);
  surface.FillRect(-10, -10, 100, 100, 0xFFFFFFFF);
  Check(CountPixels(surface, 0xFFFFFFFF) == 16, "oversized FillRect covers the whole surface");

  // Fully outside rectangles and empty sizes are no-ops.
  surface.Fill(0xFF000000);
  surface.FillRect(10, 10, 5, 5, 0xFFFF0000);
  surface.FillRect(-10, -10, 5, 5, 0xFFFF0000);
  surface.FillRect(1, 1, 0, 2, 0xFFFF0000);
  surface.FillRect(1, 1, 2, -1, 0xFFFF0000);
  Check(CountPixels(surface, 0xFFFF0000) == 0, "out-of-bounds and empty FillRects are no-ops");
}

}  // namespace

int main() {
  TestGeometry();
  TestRowMajorLayout();
  TestFill();
  TestFillRect();
  TestFillRectClipping();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d surface check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("surface tests passed\n");
  return 0;
}
