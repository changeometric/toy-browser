#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "toy/render/surface.h"

namespace toy::render {

// Bounds for decoded images, mirroring the shell's surface limits. Decoding
// rejects an image before allocating when either side or the total pixel count
// exceeds them, so a hostile or corrupt header cannot request unbounded memory.
inline constexpr int kMaxImageDimension = 8192;
inline constexpr std::int64_t kMaxImagePixels = 16'777'216;

// A decoded image ready for painting. Pixels use the Surface representation
// (0xAARRGGBB, straight alpha), row-major, top-left origin, no row padding.
// A valid image has positive dimensions and pixels.size() == width * height.
struct Image {
  int width = 0;
  int height = 0;
  std::vector<Argb> pixels;

  // Pixel at (x, y); x and y must be inside the image (asserted in debug builds).
  Argb At(int x, int y) const;
};

// Decodes one PNG image from `bytes` and fills `out`. The decoder is the approved
// libpng dependency behind this interface, so render owns image decoding and the
// rest of the project never includes a PNG header. Every PNG color type, bit
// depth and interlace mode that libpng converts to 8-bit RGBA is accepted; the
// output is always straight-alpha ARGB. The bounds are inclusive maxima, matching
// the shell's surface limits: a side larger than kMaxImageDimension or a pixel
// count larger than kMaxImagePixels is rejected before the pixel buffer is
// allocated.
//
// Returns true on success; on failure returns false, stores a human-readable
// message in *error (when not null) and leaves `out` untouched. Never throws:
// malformed, truncated or unsupported data is an ordinary error.
bool DecodePng(std::string_view bytes, Image* out, std::string* error);

// Decodes one JPEG image from `bytes` and fills `out`. The decoder is the approved
// libjpeg (libjpeg-turbo) dependency behind this interface, so libjpeg types stay
// in render's private implementation and no other module includes a JPEG header.
// Baseline and progressive streams are accepted; grayscale, RGB and CMYK/YCCK
// sources are converted to RGB by libjpeg and the output is always straight-alpha
// ARGB with alpha 255 (JPEG has no alpha channel). EXIF orientation, ICC profiles
// and other metadata are ignored: pixels are decoded in the stored image
// orientation. The same bounds as PNG apply (inclusive maxima: a side larger than
// kMaxImageDimension or a pixel count larger than kMaxImagePixels is rejected
// before the pixel buffer is allocated).
//
// Returns true on success; on failure returns false, stores a human-readable
// message in *error (when not null) and leaves `out` untouched. Never throws:
// malformed, truncated or unsupported data is an ordinary error.
bool DecodeJpeg(std::string_view bytes, Image* out, std::string* error);

// Decodes one image by CONTENT, never by file extension or caller hint: the PNG
// signature selects DecodePng and a JPEG SOI marker (FF D8 FF) selects DecodeJpeg.
// Unknown or malformed data is an ordinary error. The output bounds, ARGB pixel
// representation and never-throw error contract are identical to the format
// specific decoders. Page loaders should use this entry point so a page's images
// can mix formats without the shell knowing any codec details.
bool DecodeImage(std::string_view bytes, Image* out, std::string* error);

}  // namespace toy::render
