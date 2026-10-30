// Verifies the M6 vertical slice end to end and headlessly: shell::Run loads a
// real HTML page from file://, computes styles, lays it out at 200x100, builds
// the display list, rasterizes it and writes a deterministic PPM. The expected
// geometry is the frozen M5 layout for the fixture; the pixel assertions prove
// that paint uses the computed colors and paints only inside the text cells.

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "support/check.h"
#include "toy/shell/application.h"

namespace {

constexpr int kWidth = 200;
constexpr int kHeight = 100;

// Text cells of the fixture under frozen M5 layout semantics: h1 is set at 32px
// (two 16px-wide cells), p at 16px (five 8px-wide cells), body has an 8px margin.
constexpr int kHeadingLeft = 8;
constexpr int kHeadingRight = 40;
constexpr int kHeadingTop = 24;
constexpr int kHeadingBottom = 56;
constexpr int kParagraphLeft = 8;
constexpr int kParagraphRight = 48;
constexpr int kParagraphTop = 88;
constexpr int kParagraphBottom = 100;

using Rgb = std::array<unsigned char, 3>;

constexpr Rgb kWhite{0xFF, 0xFF, 0xFF};
constexpr Rgb kPureRed{0xFF, 0x00, 0x00};

struct Image {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;  // RGB, three bytes per pixel
};

bool ReadFile(const std::string& path, std::vector<unsigned char>* bytes) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  bytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return true;
}

bool ParseInt(const std::string& text, int* value) {
  if (text.empty()) {
    return false;
  }
  int parsed = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (error != std::errc() || end != text.data() + text.size() || parsed <= 0) {
    return false;
  }
  *value = parsed;
  return true;
}

// Parses the binary PPM that render::WritePpm produces: "P6\n<w> <h>\n255\n"
// followed by RGB bytes.
bool ParsePpm(const std::vector<unsigned char>& bytes, Image* image) {
  std::istringstream stream(std::string(bytes.begin(), bytes.end()));
  std::string magic;
  std::string width_text;
  std::string height_text;
  std::string maxval_text;
  if (!(stream >> magic >> width_text >> height_text >> maxval_text)) {
    return false;
  }
  if (magic != "P6" || maxval_text != "255" || !ParseInt(width_text, &image->width) ||
      !ParseInt(height_text, &image->height)) {
    return false;
  }
  // Token extraction stops before the whitespace that ends the header; consume
  // exactly that one separator so the raster data starts at the next byte.
  stream.get();
  image->pixels.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());

  const std::size_t pixel_count =
      static_cast<std::size_t>(image->width) * static_cast<std::size_t>(image->height);
  return image->pixels.size() == pixel_count * 3;
}

Rgb Pixel(const Image& image, int x, int y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                              static_cast<std::size_t>(x)) *
                             3;
  return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2]};
}

// True when the pixel is darker than mid-gray in at least one channel: enough to
// tell painted black text from the white page behind it.
bool IsDark(const Rgb& pixel) { return pixel[0] < 0x80 || pixel[1] < 0x80 || pixel[2] < 0x80; }

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <fixture.html> <output.ppm>\n", argv[0]);
    return 1;
  }
  const std::string fixture = argv[1];
  const std::string output = argv[2];
  const std::string second_output = output + ".twice.ppm";

  toy::shell::Options options;
  options.width = kWidth;
  options.height = kHeight;
  options.screenshot_path = output;
  options.page_url = "file://" + fixture;
  toy_test::Check(toy::shell::Run(options) == 0,
                  "shell::Run loads the page and writes the screenshot");

  std::vector<unsigned char> first_bytes;
  toy_test::Check(ReadFile(output, &first_bytes), "the screenshot file exists");
  Image image;
  const bool parsed = ParsePpm(first_bytes, &image);
  toy_test::Check(parsed, "the screenshot is a P6 PPM with a full RGB raster");
  toy_test::Check(image.width == kWidth && image.height == kHeight,
                  "the screenshot has the requested 200x100 size");
  if (!parsed || image.width != kWidth || image.height != kHeight) {
    // Without a usable raster the pixel checks would read out of bounds.
    return toy_test::Finish("shell screenshot page test");
  }

  // The page background is white everywhere content does not paint.
  toy_test::Check(Pixel(image, 0, 0) == kWhite, "the pixel at (0,0) is white");
  toy_test::Check(Pixel(image, 199, 0) == kWhite, "the background right of the body is white");
  toy_test::Check(Pixel(image, 100, 60) == kWhite,
                  "the background between the heading and the paragraph is white");
  toy_test::Check(Pixel(image, 0, 99) == kWhite, "the bottom-left background is white");

  // The h1 rule makes the heading text pure red, and only inside its text cell.
  int red_in_heading = 0;
  int red_outside_heading = 0;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      if (Pixel(image, x, y) != kPureRed) {
        continue;
      }
      const bool in_heading =
          x >= kHeadingLeft && x < kHeadingRight && y >= kHeadingTop && y < kHeadingBottom;
      if (in_heading) {
        ++red_in_heading;
      } else {
        ++red_outside_heading;
      }
    }
  }
  toy_test::Check(red_in_heading > 0, "the heading text is painted pure red inside its cell");
  toy_test::Check(red_outside_heading == 0,
                  "no red pixel is painted outside the heading text cell");

  // The default black text color paints dark glyphs inside the paragraph cell.
  bool dark_in_paragraph = false;
  for (int y = kParagraphTop; y < kParagraphBottom; ++y) {
    for (int x = kParagraphLeft; x < kParagraphRight; ++x) {
      if (IsDark(Pixel(image, x, y))) {
        dark_in_paragraph = true;
      }
    }
  }
  toy_test::Check(dark_in_paragraph, "the paragraph text is painted dark inside its cell");

  // Rendering is deterministic: the same page twice gives byte-identical files.
  toy::shell::Options twice = options;
  twice.screenshot_path = second_output;
  toy_test::Check(toy::shell::Run(twice) == 0, "a second run renders the same page");
  std::vector<unsigned char> second_bytes;
  toy_test::Check(ReadFile(second_output, &second_bytes), "the second screenshot file exists");
  toy_test::Check(second_bytes == first_bytes, "two runs produce byte-identical PPM files");

  if (toy_test::failures == 0) {
    std::remove(output.c_str());
    std::remove(second_output.c_str());
  }
  return toy_test::Finish("shell screenshot page test");
}
