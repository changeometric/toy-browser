// Tests for toy::render JPEG decoding and content-based image dispatch: the
// committed baseline, grayscale, progressive, CMYK, too-wide and truncated
// fixtures, the straight-alpha ARGB output, clean failure on bad data with the
// caller's image untouched, and DecodeImage selecting PNG or JPEG by content.

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/check.h"
#include "toy/render/image.h"

namespace {

using toy::render::Argb;
using toy::render::DecodeImage;
using toy::render::DecodeJpeg;
using toy::render::Image;

// Tolerances documented in tests/fixtures/images/README.md. JPEG is lossy and
// the libjpeg version may round slightly differently, so the tests compare
// decoded channels against the documented values with these tolerances instead
// of comparing whole files.
constexpr int kRgbTolerance = 4;
constexpr int kGrayTolerance = 2;
constexpr int kCmykTolerance = 8;

struct Rgb {
  int red;
  int green;
  int blue;
};

constexpr Rgb kRed{255, 0, 0};
constexpr Rgb kGreen{0, 255, 0};
constexpr Rgb kBlue{0, 0, 255};
constexpr Rgb kWhite{255, 255, 255};
constexpr Rgb kBlack{0, 0, 0};

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

// Decodes tests/fixtures/images/<name> with DecodeJpeg or DecodeImage and records
// whether the file was readable.
struct DecodedFixture {
  bool readable = false;
  bool decoded = false;
  Image image;
  std::string error;
};

DecodedFixture DecodeFixture(const std::string& directory, const std::string& name,
                             bool by_content) {
  DecodedFixture result;
  const std::optional<std::string> bytes = ReadFile(directory + "/" + name);
  const std::string readable_description = "fixture is readable: " + name;
  toy_test::Check(bytes.has_value(), readable_description.c_str());
  result.readable = bytes.has_value();
  if (!bytes.has_value()) {
    return result;
  }
  result.decoded = by_content ? DecodeImage(*bytes, &result.image, &result.error)
                              : DecodeJpeg(*bytes, &result.image, &result.error);
  return result;
}

bool ChannelNear(int actual, int expected, int tolerance) {
  return std::abs(actual - expected) <= tolerance;
}

bool PixelNear(Argb pixel, Rgb expected, int tolerance) {
  return (pixel >> 24) == 0xFFu &&
         ChannelNear(static_cast<int>((pixel >> 16) & 0xFFu), expected.red, tolerance) &&
         ChannelNear(static_cast<int>((pixel >> 8) & 0xFFu), expected.green, tolerance) &&
         ChannelNear(static_cast<int>(pixel & 0xFFu), expected.blue, tolerance);
}

// Checks every pixel against the four documented quadrants, in top-left,
// top-right, bottom-left, bottom-right order.
bool AllQuadrantsNear(const Image& image, const Rgb (&quadrants)[4], int tolerance) {
  if (image.width <= 0 || image.height <= 0 ||
      image.pixels.size() !=
          static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height)) {
    return false;
  }
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const int quadrant = (y < image.height / 2 ? 0 : 2) + (x < image.width / 2 ? 0 : 1);
      if (!PixelNear(image.At(x, y), quadrants[quadrant], tolerance)) {
        return false;
      }
    }
  }
  return true;
}

bool AllPixelsOpaque(const Image& image) {
  for (Argb pixel : image.pixels) {
    if ((pixel >> 24) != 0xFFu) {
      return false;
    }
  }
  return true;
}

// A non-empty image whose exact contents must survive every failed decode.
Image Sentinel() {
  Image image;
  image.width = 3;
  image.height = 2;
  image.pixels = {0xDEADBEEFu, 0x00000001u};
  return image;
}

bool IsUntouched(const Image& image, const Image& sentinel) {
  return image.width == sentinel.width && image.height == sentinel.height &&
         image.pixels == sentinel.pixels;
}

void TestRgbFixture(const std::string& directory) {
  const DecodedFixture fixture = DecodeFixture(directory, "jpeg_rgb_16x8.jpg", false);
  if (!fixture.decoded) {
    toy_test::Check(false, "jpeg_rgb_16x8.jpg decodes");
    return;
  }
  toy_test::Check(fixture.image.width == 16 && fixture.image.height == 8,
                  "jpeg_rgb_16x8.jpg is 16x8");
  toy_test::Check(fixture.image.pixels.size() == 16u * 8u,
                  "jpeg_rgb_16x8.jpg has exactly width * height pixels");
  const Rgb quadrants[4] = {kRed, kGreen, kBlue, kWhite};
  toy_test::Check(AllQuadrantsNear(fixture.image, quadrants, kRgbTolerance),
                  "jpeg_rgb_16x8.jpg decodes red/green/blue/white quadrants");
  toy_test::Check(AllPixelsOpaque(fixture.image),
                  "jpeg_rgb_16x8.jpg decodes to opaque ARGB (alpha 255)");
}

void TestGrayscaleFixture(const std::string& directory) {
  const DecodedFixture fixture = DecodeFixture(directory, "jpeg_gray_16x8.jpg", false);
  if (!fixture.decoded) {
    toy_test::Check(false, "jpeg_gray_16x8.jpg decodes");
    return;
  }
  toy_test::Check(fixture.image.width == 16 && fixture.image.height == 8,
                  "jpeg_gray_16x8.jpg is 16x8");
  const Rgb quadrants[4] = {{0, 0, 0}, {64, 64, 64}, {128, 128, 128}, {255, 255, 255}};
  toy_test::Check(AllQuadrantsNear(fixture.image, quadrants, kGrayTolerance),
                  "jpeg_gray_16x8.jpg expands gray 0,64,128,255 to opaque RGB quadrants");
  toy_test::Check(AllPixelsOpaque(fixture.image),
                  "jpeg_gray_16x8.jpg decodes to opaque ARGB (alpha 255)");
}

void TestProgressiveFixture(const std::string& directory) {
  const DecodedFixture fixture = DecodeFixture(directory, "jpeg_progressive_16x8.jpg", false);
  if (!fixture.decoded) {
    toy_test::Check(false, "jpeg_progressive_16x8.jpg decodes");
    return;
  }
  toy_test::Check(fixture.image.width == 16 && fixture.image.height == 8,
                  "jpeg_progressive_16x8.jpg is 16x8");
  const Rgb quadrants[4] = {kRed, kGreen, kBlue, kWhite};
  toy_test::Check(AllQuadrantsNear(fixture.image, quadrants, kRgbTolerance),
                  "jpeg_progressive_16x8.jpg decodes the same quadrants as the baseline fixture");
  toy_test::Check(AllPixelsOpaque(fixture.image),
                  "jpeg_progressive_16x8.jpg decodes to opaque ARGB (alpha 255)");
}

void TestCmykFixture(const std::string& directory) {
  const DecodedFixture fixture = DecodeFixture(directory, "jpeg_cmyk_16x8.jpg", false);
  if (!fixture.decoded) {
    toy_test::Check(false, "jpeg_cmyk_16x8.jpg decodes");
    return;
  }
  toy_test::Check(fixture.image.width == 16 && fixture.image.height == 8,
                  "jpeg_cmyk_16x8.jpg is 16x8");
  const Rgb quadrants[4] = {kRed, kGreen, kBlue, kBlack};
  toy_test::Check(AllQuadrantsNear(fixture.image, quadrants, kCmykTolerance),
                  "jpeg_cmyk_16x8.jpg converts CMYK red/green/blue/black to RGB");
  toy_test::Check(AllPixelsOpaque(fixture.image),
                  "jpeg_cmyk_16x8.jpg decodes to opaque ARGB (alpha 255)");
}

void TestTooWideRejected(const std::string& directory) {
  const std::optional<std::string> bytes = ReadFile(directory + "/jpeg_too_wide_9000x1.jpg");
  toy_test::Check(bytes.has_value(), "jpeg_too_wide_9000x1.jpg is readable");
  if (!bytes.has_value()) {
    return;
  }
  const Image sentinel = Sentinel();
  Image out = sentinel;
  std::string error;
  toy_test::Check(!DecodeJpeg(*bytes, &out, &error),
                  "a 9000 px wide JPEG is rejected before allocation");
  toy_test::Check(error.starts_with("DecodeJpeg: "), "the too-wide error is prefixed");
  toy_test::Check(error.find("8192") != std::string::npos,
                  "the too-wide error names the dimension limit");
  toy_test::Check(IsUntouched(out, sentinel),
                  "the failed too-wide decode leaves the image untouched");
}

void TestTruncatedRejected(const std::string& directory) {
  const std::optional<std::string> bytes = ReadFile(directory + "/jpeg_truncated_16x8.jpg");
  toy_test::Check(bytes.has_value(), "jpeg_truncated_16x8.jpg is readable");
  if (!bytes.has_value()) {
    return;
  }
  const Image sentinel = Sentinel();
  Image out = sentinel;
  std::string error;
  toy_test::Check(!DecodeJpeg(*bytes, &out, &error), "a truncated JPEG is rejected");
  toy_test::Check(error.starts_with("DecodeJpeg: "), "the truncated error is prefixed");
  toy_test::Check(IsUntouched(out, sentinel), "the truncated decode leaves the image untouched");

  // DecodeImage must reject it the same way (this also pins that a dispatch
  // failure never produces a partially filled image). The delegated decoder's
  // readable error is passed through unchanged.
  Image dispatched = sentinel;
  std::string dispatched_error;
  toy_test::Check(!DecodeImage(*bytes, &dispatched, &dispatched_error),
                  "DecodeImage rejects a truncated JPEG");
  toy_test::Check(dispatched_error.starts_with("DecodeJpeg: "),
                  "DecodeImage reports the delegated JPEG error for a truncated JPEG");
  toy_test::Check(IsUntouched(dispatched, sentinel),
                  "DecodeImage leaves the image untouched for a truncated JPEG");
}

void TestMalformedRejected(const std::string& directory) {
  const Image sentinel = Sentinel();
  const auto check_failure = [&sentinel](std::string_view label, std::string_view bytes) {
    Image out = sentinel;
    std::string error;
    const bool ok = DecodeJpeg(bytes, &out, &error);
    const std::string failed_description = "DecodeJpeg fails: " + std::string(label);
    toy_test::Check(!ok, failed_description.c_str());
    const std::string prefixed_description =
        "DecodeJpeg failure is readable: " + std::string(label);
    toy_test::Check(error.starts_with("DecodeJpeg: "), prefixed_description.c_str());
    const std::string untouched_description =
        "DecodeJpeg failure leaves the image untouched: " + std::string(label);
    toy_test::Check(IsUntouched(out, sentinel), untouched_description.c_str());
  };

  check_failure("empty input", std::string_view());
  check_failure("not a JPEG", std::string_view("this is not a JPEG file"));

  // PNG bytes belong to DecodePng: DecodeJpeg must reject them by content.
  const std::optional<std::string> png = ReadFile(directory + "/flag_16x8.png");
  toy_test::Check(png.has_value(), "flag_16x8.png is readable for the PNG-to-JPEG case");
  if (png.has_value()) {
    check_failure("PNG bytes", std::string_view(*png));
  }

  // A null output image and a null error pointer are both handled.
  const std::optional<std::string> jpeg = ReadFile(directory + "/jpeg_rgb_16x8.jpg");
  toy_test::Check(jpeg.has_value(), "jpeg_rgb_16x8.jpg is readable for the null cases");
  if (jpeg.has_value()) {
    std::string error;
    toy_test::Check(!DecodeJpeg(*jpeg, nullptr, &error), "a null output image is rejected");
    toy_test::Check(error.starts_with("DecodeJpeg: "), "a null output image sets a readable error");
    Image out = sentinel;
    toy_test::Check(!DecodeJpeg("garbage", &out, nullptr),
                    "a failed decode tolerates a null error pointer");
    toy_test::Check(IsUntouched(out, sentinel),
                    "a failed decode with a null error still leaves the image untouched");
  }
}

// libjpeg also reports benign stream defects as warnings; those must not turn a
// displayable JPEG into a load failure. A few junk bytes before the EOI marker
// are the common "extraneous bytes before marker" case.
void TestBenignWarningsTolerated(const std::string& directory) {
  const std::optional<std::string> bytes = ReadFile(directory + "/jpeg_rgb_16x8.jpg");
  toy_test::Check(bytes.has_value(), "jpeg_rgb_16x8.jpg is readable for the warning test");
  if (!bytes.has_value()) {
    return;
  }
  const std::size_t eoi = bytes->rfind("\xFF\xD9");
  toy_test::Check(eoi != std::string::npos, "jpeg_rgb_16x8.jpg has an EOI marker");
  if (eoi == std::string::npos) {
    return;
  }
  std::string corrupted = *bytes;
  corrupted.insert(eoi, "junk bytes");
  Image image;
  std::string error;
  const bool ok = DecodeJpeg(corrupted, &image, &error);
  toy_test::Check(ok, "benign extraneous bytes before the EOI marker are tolerated");
  if (ok) {
    toy_test::Check(image.width == 16 && image.height == 8,
                    "the tolerated file still decodes as 16x8");
    const Rgb quadrants[4] = {kRed, kGreen, kBlue, kWhite};
    toy_test::Check(AllQuadrantsNear(image, quadrants, kRgbTolerance),
                    "the tolerated file still decodes its quadrants");
  }
}

void TestDecodeImageDispatchesPng(const std::string& directory) {
  const DecodedFixture fixture = DecodeFixture(directory, "flag_16x8.png", true);
  if (!fixture.decoded) {
    toy_test::Check(false, "DecodeImage dispatches PNG bytes to the PNG decoder");
    return;
  }
  toy_test::Check(fixture.image.width == 16 && fixture.image.height == 8,
                  "DecodeImage decodes flag_16x8.png as 16x8");
  // PNG decoding stays exact: the same pixels as the render.image test.
  const Rgb quadrants[4] = {kRed, kBlue, kRed, kBlue};
  toy_test::Check(AllQuadrantsNear(fixture.image, quadrants, 0),
                  "DecodeImage keeps the PNG pixels exact (red left, blue right)");
  toy_test::Check(AllPixelsOpaque(fixture.image), "DecodeImage's PNG result is opaque ARGB");
}

void TestDecodeImageDispatchesJpeg(const std::string& directory) {
  const DecodedFixture fixture = DecodeFixture(directory, "jpeg_rgb_16x8.jpg", true);
  if (!fixture.decoded) {
    toy_test::Check(false, "DecodeImage dispatches JPEG bytes to the JPEG decoder");
    return;
  }
  toy_test::Check(fixture.image.width == 16 && fixture.image.height == 8,
                  "DecodeImage decodes jpeg_rgb_16x8.jpg as 16x8");
  const Rgb quadrants[4] = {kRed, kGreen, kBlue, kWhite};
  toy_test::Check(AllQuadrantsNear(fixture.image, quadrants, kRgbTolerance),
                  "DecodeImage decodes the JPEG quadrants within tolerance");
}

void TestDecodeImageRejectsUnknownData(const std::string& directory) {
  const Image sentinel = Sentinel();
  // Unknown data fails in DecodeImage itself; a recognized-but-invalid JPEG is
  // rejected by the delegated decoder and keeps that decoder's readable prefix.
  const auto check_failure = [&sentinel](std::string_view label, std::string_view bytes,
                                         std::string_view expected_prefix) {
    Image out = sentinel;
    std::string error;
    const bool ok = DecodeImage(bytes, &out, &error);
    const std::string failed_description = "DecodeImage fails: " + std::string(label);
    toy_test::Check(!ok, failed_description.c_str());
    const std::string prefixed_description =
        "DecodeImage failure is readable: " + std::string(label);
    toy_test::Check(error.starts_with(expected_prefix), prefixed_description.c_str());
    const std::string untouched_description =
        "DecodeImage failure leaves the image untouched: " + std::string(label);
    toy_test::Check(IsUntouched(out, sentinel), untouched_description.c_str());
  };

  check_failure("empty input", std::string_view(), "DecodeImage: ");
  check_failure("garbage", std::string_view("GIF89a not a PNG or JPEG"), "DecodeImage: ");

  // A JPEG that is too wide must fail through the dispatcher as well.
  const std::optional<std::string> too_wide = ReadFile(directory + "/jpeg_too_wide_9000x1.jpg");
  toy_test::Check(too_wide.has_value(), "jpeg_too_wide_9000x1.jpg is readable for dispatch");
  if (too_wide.has_value()) {
    check_failure("too-wide JPEG", std::string_view(*too_wide), "DecodeJpeg: ");
  }
}

void TestRepeatedDecodeIsDeterministic(const std::string& directory) {
  const std::optional<std::string> bytes = ReadFile(directory + "/jpeg_rgb_16x8.jpg");
  toy_test::Check(bytes.has_value(), "jpeg_rgb_16x8.jpg is readable for the determinism test");
  if (!bytes.has_value()) {
    return;
  }
  Image first;
  Image second;
  std::string first_error;
  std::string second_error;
  toy_test::Check(DecodeJpeg(*bytes, &first, &first_error), "first JPEG decode succeeds");
  toy_test::Check(DecodeJpeg(*bytes, &second, &second_error), "second JPEG decode succeeds");
  toy_test::Check(first.width == second.width && first.height == second.height,
                  "repeated JPEG decodes agree on the dimensions");
  toy_test::Check(first.pixels == second.pixels, "repeated JPEG decodes are pixel-identical");
}

}  // namespace

int main(int argc, char** argv) {
  toy_test::Check(argc >= 2, "the fixture directory is passed as argv[1]");
  if (argc < 2) {
    return toy_test::Finish("render.jpeg");
  }
  const std::string directory = argv[1];

  TestRgbFixture(directory);
  TestGrayscaleFixture(directory);
  TestProgressiveFixture(directory);
  TestCmykFixture(directory);
  TestTooWideRejected(directory);
  TestTruncatedRejected(directory);
  TestMalformedRejected(directory);
  TestBenignWarningsTolerated(directory);
  TestDecodeImageDispatchesPng(directory);
  TestDecodeImageDispatchesJpeg(directory);
  TestDecodeImageRejectsUnknownData(directory);
  TestRepeatedDecodeIsDeterministic(directory);
  return toy_test::Finish("render.jpeg");
}
