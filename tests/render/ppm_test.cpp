// Tests for toy::render::WritePpm: P6 header, RGB bytes and alpha dropping.

#include "toy/render/ppm.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "toy/render/surface.h"

namespace {

using toy::render::Surface;

constexpr const char* kPpmPath = "render_ppm_test.ppm";

int g_failures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "ppm test failed: %s\n", description);
    ++g_failures;
  }
}

std::vector<unsigned char> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  Surface surface(2, 2);
  surface.FillRect(0, 0, 1, 1, 0xFFFF0000);
  surface.FillRect(1, 0, 1, 1, 0xFF00FF00);
  surface.FillRect(0, 1, 1, 1, 0xFF0000FF);
  surface.FillRect(1, 1, 1, 1, 0x80010203);

  Check(toy::render::WritePpm(surface, kPpmPath), "WritePpm reports success");

  constexpr std::string_view kHeader = "P6\n2 2\n255\n";
  const std::vector<unsigned char> expected_pixels = {0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00,
                                                      0x00, 0x00, 0xFF, 0x01, 0x02, 0x03};
  const std::vector<unsigned char> bytes = ReadFile(kPpmPath);

  Check(bytes.size() == kHeader.size() + expected_pixels.size(),
        "file size matches header and pixels");
  if (bytes.size() == kHeader.size() + expected_pixels.size()) {
    Check(std::equal(kHeader.begin(), kHeader.end(), bytes.begin()), "P6 header has expected text");
    Check(std::equal(expected_pixels.begin(), expected_pixels.end(),
                     bytes.begin() + static_cast<std::ptrdiff_t>(kHeader.size())),
          "pixels are RGB bytes in row-major order with alpha dropped");
  }

  Check(std::remove(kPpmPath) == 0, "temporary PPM file is removed");

  Check(!toy::render::WritePpm(surface, "render_ppm_test_missing_dir/output.ppm"),
        "WritePpm reports failure for an unwritable path");

  if (g_failures != 0) {
    std::fprintf(stderr, "%d ppm check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("ppm tests passed\n");
  return 0;
}
