// Tests for toy::render PNG decoding: the four committed fixtures (RGB, RGBA,
// grayscale and palette), the explicit straight-alpha ARGB conversion, clean
// failure on bad data and deterministic repeated decodes.

#include "toy/render/image.h"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/check.h"

namespace {

using toy::render::Argb;
using toy::render::DecodePng;
using toy::render::Image;

constexpr Argb kOpaqueRed = 0xFFFF0000u;
constexpr Argb kOpaqueGreen = 0xFF00FF00u;
constexpr Argb kOpaqueBlue = 0xFF0000FFu;
constexpr Argb kTranslucentRed = 0x80FF0000u;

std::optional<std::string> ReadFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) {
    return std::nullopt;
  }
  return bytes;
}

// Decodes tests/fixtures/images/<name> and records whether the file was readable.
struct DecodedFixture {
  bool readable = false;
  bool decoded = false;
  Image image;
  std::string error;
};

DecodedFixture DecodeFixture(const std::string& directory, const std::string& name) {
  DecodedFixture result;
  const std::optional<std::string> bytes = ReadFile(directory + "/" + name);
  const std::string readable_description = "fixture is readable: " + name;
  toy_test::Check(bytes.has_value(), readable_description.c_str());
  result.readable = bytes.has_value();
  if (!bytes.has_value()) {
    return result;
  }
  result.decoded = DecodePng(*bytes, &result.image, &result.error);
  return result;
}

// Checks the whole pixel buffer against `expected(x, y)`.
template <typename Expected>
bool AllPixelsMatch(const Image& image, Expected expected) {
  if (image.pixels.size() !=
      static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height)) {
    return false;
  }
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      if (image.At(x, y) != expected(x, y)) {
        return false;
      }
    }
  }
  return true;
}

bool IsOpaque(Argb color) { return (color >> 24) == 0xFFu; }

void TestFlagFixture(const std::string& directory) {
  const DecodedFixture fixture = DecodeFixture(directory, "flag_16x8.png");
  if (!fixture.decoded) {
    toy_test::Check(false, "flag_16x8.png decodes");
    return;
  }
  toy_test::Check(fixture.image.width == 16 && fixture.image.height == 8, "flag_16x8.png is 16x8");
  toy_test::Check(fixture.image.pixels.size() == 16u * 8u,
                  "flag_16x8.png has exactly width * height pixels");
  toy_test::Check(
      AllPixelsMatch(fixture.image, [](int x, int) { return x < 8 ? kOpaqueRed : kOpaqueBlue; }),
      "flag_16x8.png is opaque red in columns 0-7 and opaque blue in columns 8-15");
}

void TestAlphaFixture(const std::string& directory) {
  const DecodedFixture fixture = DecodeFixture(directory, "alpha_8x8.png");
  if (!fixture.decoded) {
    toy_test::Check(false, "alpha_8x8.png decodes");
    return;
  }
  toy_test::Check(fixture.image.width == 8 && fixture.image.height == 8, "alpha_8x8.png is 8x8");
  toy_test::Check(AllPixelsMatch(fixture.image,
                                 [](int x, int) { return x < 4 ? kTranslucentRed : kOpaqueGreen; }),
                  "alpha_8x8.png keeps straight alpha 128 red and opaque green");
  toy_test::Check(fixture.image.At(0, 0) == kTranslucentRed,
                  "alpha_8x8.png stores alpha 128 without pre-multiplying the color");
  toy_test::Check(fixture.image.At(7, 7) == kOpaqueGreen,
                  "alpha_8x8.png has an opaque green corner");
}

void TestGrayscaleFixture(const std::string& directory) {
  const DecodedFixture fixture = DecodeFixture(directory, "gray_4x4.png");
  if (!fixture.decoded) {
    toy_test::Check(false, "gray_4x4.png decodes");
    return;
  }
  toy_test::Check(fixture.image.width == 4 && fixture.image.height == 4, "gray_4x4.png is 4x4");
  const Argb levels[4] = {0xFF000000u, 0xFF404040u, 0xFF808080u, 0xFFFFFFFFu};
  toy_test::Check(AllPixelsMatch(fixture.image, [&levels](int x, int) { return levels[x]; }),
                  "gray_4x4.png expands gray 0,64,128,255 to opaque RGB rows");
}

void TestPaletteFixture(const std::string& directory) {
  const DecodedFixture fixture = DecodeFixture(directory, "palette_4x2.png");
  if (!fixture.decoded) {
    toy_test::Check(false, "palette_4x2.png decodes");
    return;
  }
  toy_test::Check(fixture.image.width == 4 && fixture.image.height == 2, "palette_4x2.png is 4x2");
  const Argb row0[4] = {kOpaqueRed, kOpaqueGreen, kOpaqueBlue, kOpaqueRed};
  const Argb row1[4] = {kOpaqueBlue, kOpaqueGreen, kOpaqueRed, kOpaqueBlue};
  toy_test::Check(
      AllPixelsMatch(fixture.image,
                     [&row0, &row1](int x, int y) { return (y == 0 ? row0 : row1)[x]; }),
      "palette_4x2.png expands its red/green/blue palette indices");
}

void TestDecodedPixelsAreOpaqueOrKeepAlpha(const std::string& directory) {
  // The RGB, grayscale and palette fixtures all lack an alpha channel, so they
  // must decode to fully opaque pixels.
  for (const char* name : {"flag_16x8.png", "gray_4x4.png", "palette_4x2.png"}) {
    const DecodedFixture fixture = DecodeFixture(directory, name);
    if (!fixture.decoded) {
      continue;
    }
    bool all_opaque = true;
    for (Argb pixel : fixture.image.pixels) {
      all_opaque = all_opaque && IsOpaque(pixel);
    }
    const std::string description = std::string("image without alpha decodes opaque: ") + name;
    toy_test::Check(all_opaque, description.c_str());
  }
}

void TestRepeatedDecodeIsDeterministic(const std::string& directory) {
  const std::optional<std::string> bytes = ReadFile(directory + "/alpha_8x8.png");
  toy_test::Check(bytes.has_value(), "alpha_8x8.png can be read for the determinism test");
  if (!bytes.has_value()) {
    return;
  }
  Image first;
  Image second;
  std::string first_error;
  std::string second_error;
  toy_test::Check(DecodePng(*bytes, &first, &first_error), "first decode succeeds");
  toy_test::Check(DecodePng(*bytes, &second, &second_error), "second decode succeeds");
  toy_test::Check(first.width == second.width && first.height == second.height,
                  "repeated decodes agree on the dimensions");
  toy_test::Check(first.pixels == second.pixels, "repeated decodes are pixel-identical");
}

// Decoding must fail cleanly: false, a non-empty error and the caller's image
// left exactly as it was.
void TestFailuresLeaveOutputUntouched(const std::string& directory) {
  const std::optional<std::string> png = ReadFile(directory + "/flag_16x8.png");
  toy_test::Check(png.has_value(), "flag_16x8.png can be read for the failure tests");
  if (!png.has_value()) {
    return;
  }

  Image sentinel;
  sentinel.width = 3;
  sentinel.height = 2;
  sentinel.pixels = {0xDEADBEEFu, 0x00000001u};

  const auto check_failure = [&sentinel](std::string_view label, std::string_view bytes) {
    Image out = sentinel;
    std::string error;
    const bool ok = DecodePng(bytes, &out, &error);
    const std::string failed_description = "decoding fails: " + std::string(label);
    toy_test::Check(!ok, failed_description.c_str());
    const std::string error_description = "failure sets an error: " + std::string(label);
    toy_test::Check(!error.empty(), error_description.c_str());
    const std::string untouched_description =
        "failure leaves the image untouched: " + std::string(label);
    toy_test::Check(out.width == sentinel.width && out.height == sentinel.height &&
                        out.pixels == sentinel.pixels,
                    untouched_description.c_str());
  };

  check_failure("empty input", std::string_view());
  check_failure("not a PNG", std::string_view("this is not a PNG file"));
  check_failure("truncated header", std::string_view(*png).substr(0, 20));
  // Three quarters of the file still parses as a PNG header but breaks off in
  // the compressed pixel data, so the read fails after the size checks.
  check_failure("truncated pixel data", std::string_view(*png).substr(0, png->size() * 3 / 4));

  // A null error pointer is allowed for every outcome.
  Image out = sentinel;
  toy_test::Check(!DecodePng("garbage", &out, nullptr), "a failed decode tolerates a null error");
  toy_test::Check(
      out.width == sentinel.width && out.height == sentinel.height && out.pixels == sentinel.pixels,
      "a failed decode with a null error still leaves the image untouched");
}

}  // namespace

int main(int argc, char** argv) {
  toy_test::Check(argc >= 2, "the fixture directory is passed as argv[1]");
  if (argc < 2) {
    return toy_test::Finish("render.image");
  }
  const std::string directory = argv[1];

  TestFlagFixture(directory);
  TestAlphaFixture(directory);
  TestGrayscaleFixture(directory);
  TestPaletteFixture(directory);
  TestDecodedPixelsAreOpaqueOrKeepAlpha(directory);
  TestRepeatedDecodeIsDeterministic(directory);
  TestFailuresLeaveOutputUntouched(directory);
  return toy_test::Finish("render.image");
}
