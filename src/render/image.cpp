#include "toy/render/image.h"

#include <png.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toy::render {
namespace {

// libpng's simplified read API writes one byte per RGBA channel.
constexpr std::size_t kRgbaChannels = 4;

void SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

std::string LibpngMessage(const png_image& image) {
  return image.message[0] != '\0' ? std::string(image.message)
                                  : std::string("unknown libpng error");
}

std::string DescribeSize(int width, int height) {
  return std::to_string(width) + "x" + std::to_string(height);
}

// Rejects a decoded size that breaks the frozen image bounds. Returns an empty
// string when the size is acceptable, otherwise the human-readable reason.
std::string CheckSize(png_uint_32 width, png_uint_32 height) {
  if (width == 0 || height == 0) {
    return "image has a zero dimension";
  }
  // The frozen image.h bounds are inclusive maxima: only sizes above them are
  // rejected, matching the shell's surface limits.
  if (width > static_cast<png_uint_32>(kMaxImageDimension) ||
      height > static_cast<png_uint_32>(kMaxImageDimension)) {
    // Both sides are bounded here, so the multiplication below cannot overflow.
    return DescribeSize(static_cast<int>(width), static_cast<int>(height)) +
           " exceeds the image dimension limit of " + std::to_string(kMaxImageDimension);
  }
  if (static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) > kMaxImagePixels) {
    return DescribeSize(static_cast<int>(width), static_cast<int>(height)) +
           " exceeds the image pixel limit of " + std::to_string(kMaxImagePixels);
  }
  return std::string();
}

}  // namespace

Argb Image::At(int x, int y) const {
  assert(x >= 0 && x < width);
  assert(y >= 0 && y < height);
  return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x)];
}

bool DecodePng(std::string_view bytes, Image* out, std::string* error) {
  if (out == nullptr) {
    SetError(error, "DecodePng: the output image must not be null");
    return false;
  }
  if (bytes.empty()) {
    SetError(error, "DecodePng: empty PNG data");
    return false;
  }

  // The simplified API requires the version field to be set and the opaque
  // pointer to start as null; brace initialization provides both.
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  if (png_image_begin_read_from_memory(&png, bytes.data(), bytes.size()) == 0) {
    SetError(error, "DecodePng: not a decodable PNG (" + LibpngMessage(png) + ")");
    return false;
  }

  // Validate the header-reported size before allocating the pixel buffer, so a
  // hostile or corrupt header cannot request unbounded memory.
  const std::string size_error = CheckSize(png.width, png.height);
  if (!size_error.empty()) {
    png_image_free(&png);
    SetError(error, "DecodePng: " + size_error);
    return false;
  }

  const int width = static_cast<int>(png.width);
  const int height = static_cast<int>(png.height);
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

  // Ask for 8-bit straight-alpha RGBA; libpng expands grayscale, palette and
  // 16-bit input to this form during png_image_finish_read.
  png.format = PNG_FORMAT_RGBA;
  std::vector<unsigned char> rgba;
  try {
    rgba.resize(pixel_count * kRgbaChannels);
  } catch (const std::bad_alloc&) {
    png_image_free(&png);
    SetError(error,
             "DecodePng: out of memory for " + DescribeSize(width, height) + " RGBA pixel data");
    return false;
  }
  if (png_image_finish_read(&png, nullptr, rgba.data(), 0, nullptr) == 0) {
    const std::string message = LibpngMessage(png);
    png_image_free(&png);
    SetError(error, "DecodePng: cannot decode PNG pixel data (" + message + ")");
    return false;
  }
  // finish_read releases the read state; png_image_free is still safe and marks
  // the structure as cleaned up.
  png_image_free(&png);

  std::vector<Argb> pixels;
  try {
    pixels.resize(pixel_count);
  } catch (const std::bad_alloc&) {
    SetError(error, "DecodePng: out of memory for " + DescribeSize(width, height) + " ARGB pixels");
    return false;
  }
  for (std::size_t i = 0; i < pixel_count; ++i) {
    const unsigned char* sample = rgba.data() + i * kRgbaChannels;
    // Build 0xAARRGGBB from the RGBA bytes explicitly; no host byte order is
    // assumed for either side.
    pixels[i] = (static_cast<Argb>(sample[3]) << 24) | (static_cast<Argb>(sample[0]) << 16) |
                (static_cast<Argb>(sample[1]) << 8) | static_cast<Argb>(sample[2]);
  }

  // Only assign to `out` once the whole image decoded, so a failure leaves the
  // caller's image untouched.
  Image decoded;
  decoded.width = width;
  decoded.height = height;
  decoded.pixels = std::move(pixels);
  *out = std::move(decoded);
  return true;
}

bool DecodeImage(std::string_view bytes, Image* out, std::string* error) {
  if (out == nullptr) {
    SetError(error, "DecodeImage: the output image must not be null");
    return false;
  }
  if (bytes.empty()) {
    SetError(error, "DecodeImage: empty image data");
    return false;
  }
  // Detect the format by content, never by file extension: the PNG signature
  // selects the PNG decoder and a JPEG SOI marker (FF D8 FF) the JPEG decoder.
  constexpr std::string_view kPngSignature("\x89PNG\r\n\x1a\n", 8);
  constexpr std::string_view kJpegSignature("\xFF\xD8\xFF", 3);
  if (bytes.starts_with(kPngSignature)) {
    return DecodePng(bytes, out, error);
  }
  if (bytes.starts_with(kJpegSignature)) {
    return DecodeJpeg(bytes, out, error);
  }
  SetError(error, "DecodeImage: unrecognized image format (expected PNG or JPEG)");
  return false;
}

}  // namespace toy::render
