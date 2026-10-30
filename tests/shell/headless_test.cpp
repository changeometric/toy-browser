// Verifies the M1 headless path end to end: shell::Run writes a PPM frame with
// the expected placeholder content and needs no display.

#include <array>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "toy/shell/application.h"

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 48;
constexpr int kDemoBandHeight = 8;
const char* const kScreenshotPath = "shell_headless_test.ppm";

// Reads the next whitespace-delimited token from the PPM header.
bool ReadToken(std::istream& stream, std::string* token) {
  stream >> *token;
  return static_cast<bool>(stream);
}

int Fail(const char* message) {
  std::fprintf(stderr, "shell headless test: %s\n", message);
  return 1;
}

}  // namespace

int main() {
  toy::shell::Options options;
  options.width = kWidth;
  options.height = kHeight;
  options.title = "toybrowser";
  options.screenshot_path = kScreenshotPath;
  if (toy::shell::Run(options) != 0) {
    return Fail("shell::Run failed");
  }

  std::ifstream file(kScreenshotPath, std::ios::binary);
  if (!file) {
    return Fail("cannot open the screenshot file");
  }

  std::string magic;
  std::string width_text;
  std::string height_text;
  std::string maxval_text;
  if (!ReadToken(file, &magic) || !ReadToken(file, &width_text) || !ReadToken(file, &height_text) ||
      !ReadToken(file, &maxval_text)) {
    return Fail("malformed PPM header");
  }
  if (magic != "P6" || width_text != std::to_string(kWidth) ||
      height_text != std::to_string(kHeight) || maxval_text != "255") {
    return Fail("unexpected PPM header");
  }
  // Token extraction stops before the whitespace that ends the header; consume
  // exactly that one separator so the raster data starts at the next byte.
  file.get();

  const std::vector<unsigned char> pixels{std::istreambuf_iterator<char>(file),
                                          std::istreambuf_iterator<char>()};
  file.close();

  const std::size_t pixel_count =
      static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);
  if (pixels.size() != pixel_count * 3) {
    return Fail("unexpected PPM raster byte count");
  }

  const auto pixel = [&pixels](int x, int y) -> std::array<unsigned char, 3> {
    const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kWidth) +
                                static_cast<std::size_t>(x)) *
                               3;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2]};
  };
  constexpr std::array<unsigned char, 3> kBandColor{0x3B, 0x6E, 0xA5};
  constexpr std::array<unsigned char, 3> kPageColor{0xFF, 0xFF, 0xFF};

  if (pixel(0, 0) != kBandColor || pixel(kWidth - 1, kDemoBandHeight - 1) != kBandColor) {
    return Fail("the demo band is not painted where expected");
  }
  if (pixel(0, kDemoBandHeight) != kPageColor || pixel(kWidth - 1, kHeight - 1) != kPageColor) {
    return Fail("the page below the demo band is not white");
  }

  if (std::remove(kScreenshotPath) != 0) {
    return Fail("cannot remove the screenshot file");
  }

  std::printf("shell headless test passed: %dx%d PPM screenshot verified\n", kWidth, kHeight);
  return 0;
}
