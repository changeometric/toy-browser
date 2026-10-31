#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "toy/render/image.h"

// <jpeglib.h> is a C header that needs <cstddef> and <cstdio> first in C++ mode.
// <jerror.h> supplies the warning codes used below.
#include <jerror.h>
#include <jpeglib.h>

namespace toy::render {
namespace {

// libjpeg's RGB output writes one byte per channel; its CMYK output four.
constexpr int kRgbChannels = 3;
constexpr int kCmykChannels = 4;

void SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

std::string DescribeSize(int width, int height) {
  return std::to_string(width) + "x" + std::to_string(height);
}

// Rejects a decoded size that breaks the frozen image bounds. Returns an empty
// string when the size is acceptable, otherwise the human-readable reason.
// Identical to the libpng decoder's rule: the bounds are inclusive maxima.
std::string CheckSize(int width, int height) {
  if (width == 0 || height == 0) {
    return "image has a zero dimension";
  }
  if (width > kMaxImageDimension || height > kMaxImageDimension) {
    return DescribeSize(width, height) + " exceeds the image dimension limit of " +
           std::to_string(kMaxImageDimension);
  }
  // Both sides are bounded here, so the multiplication below cannot overflow.
  if (static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) > kMaxImagePixels) {
    return DescribeSize(width, height) + " exceeds the image pixel limit of " +
           std::to_string(kMaxImagePixels);
  }
  return std::string();
}

// Converts one Adobe-style, inverted CMYK sample quadruple to opaque ARGB.
// libjpeg-turbo's libjpeg API cannot convert CMYK/YCCK to RGB itself
// (JERR_CONVERSION_NOTIMPL), so CMYK streams are decoded to JCS_CMYK and the
// inverted samples (255 = no ink) are folded with the conventional channel
// formula from libjpeg-turbo's cmyk.h: channel = sample * black / 255, rounded.
Argb InvertedCmykToArgb(unsigned int cyan, unsigned int magenta, unsigned int yellow,
                        unsigned int black) {
  const auto channel = [black](unsigned int sample) -> Argb {
    return static_cast<Argb>((sample * black + 127u) / 255u);
  };
  return 0xFF000000u | (channel(cyan) << 16) | (channel(magenta) << 8) | channel(yellow);
}

struct DecodeContext;

// The error manager installed in libjpeg's `cinfo->err`. `base` must stay the
// first member so the callbacks can recover the manager from the plain
// j_common_ptr libjpeg hands them; the static_asserts enforce that assumption.
struct ErrorManager {
  jpeg_error_mgr base;
  DecodeContext* context;
};

static_assert(std::is_standard_layout_v<ErrorManager>,
              "libjpeg callbacks recover the ErrorManager from its first member");
static_assert(offsetof(ErrorManager, base) == 0, "libjpeg stores the address of the base member");

// Decoder state shared by the callbacks and the decoding function. It lives on
// the heap and is owned by a unique_ptr created before setjmp, so a longjmp out
// of libjpeg never skips a C++ destructor or leaks the scanline/pixel buffers.
struct DecodeContext {
  jpeg_decompress_struct cinfo{};
  ErrorManager error{};
  std::jmp_buf jump{};
  char message[JMSG_LENGTH_MAX]{};
  std::vector<unsigned char> scanline;
  std::vector<Argb> pixels;
  int width = 0;
  int height = 0;
};

DecodeContext& ContextFrom(j_common_ptr cinfo) {
  auto* manager = reinterpret_cast<ErrorManager*>(cinfo->err);
  return *manager->context;
}

// libjpeg's classic failure path: format the message, then longjmp back to the
// decoder. Library code never prints, so output_message below is a no-op.
void HandleFatalError(j_common_ptr cinfo) {
  DecodeContext& context = ContextFrom(cinfo);
  (*cinfo->err->format_message)(cinfo, context.message);
  std::longjmp(context.jump, 1);
}

// libjpeg reports recoverable corruption through warnings, and it also reports
// truncation only as a warning: after a premature end of input it fills the
// missing samples with gray instead of failing. Those truncation warnings are
// promoted to decode errors so a partial image is never returned as complete.
// Other warnings (for example the common "extraneous bytes before marker" from
// slightly sloppy encoders) are benign for display and stay silent, so a
// real-world JPEG still opens. The committed fixtures are well formed and emit
// no warnings at all.
bool IsTruncationWarning(int message_code) {
  // JWRN_JPEG_EOF is "Premature end of JPEG file" and JWRN_HIT_MARKER is
  // "Corrupt JPEG data: premature end of data segment"; the too-few-scanlines
  // case is the fatal libjpeg error JERR_TOO_LITTLE_DATA, not a warning.
  return message_code == JWRN_JPEG_EOF || message_code == JWRN_HIT_MARKER;
}

void HandleWarning(j_common_ptr cinfo, int message_level) {
  if (message_level >= 0 || !IsTruncationWarning(cinfo->err->msg_code)) {
    return;
  }
  DecodeContext& context = ContextFrom(cinfo);
  (*cinfo->err->format_message)(cinfo, context.message);
  std::longjmp(context.jump, 1);
}

// Never called (HandleFatalError and HandleWarning are installed instead of the
// default handlers), but a no-op keeps libjpeg from ever writing to stderr.
void HandleOutputMessage(j_common_ptr /*cinfo*/) {}

}  // namespace

bool DecodeJpeg(std::string_view bytes, Image* out, std::string* error) {
  if (out == nullptr) {
    SetError(error, "DecodeJpeg: the output image must not be null");
    return false;
  }
  if (bytes.empty()) {
    SetError(error, "DecodeJpeg: empty JPEG data");
    return false;
  }

  // All decoder state and buffers live in this heap context, created before
  // setjmp so the longjmp handler can always destroy the decompressor and
  // release the vectors.
  auto context = std::make_unique<DecodeContext>();
  context->cinfo.err = jpeg_std_error(&context->error.base);
  context->error.context = context.get();
  context->error.base.error_exit = HandleFatalError;
  context->error.base.emit_message = HandleWarning;
  context->error.base.output_message = HandleOutputMessage;

  if (setjmp(context->jump) != 0) {
    // A libjpeg fatal error or a promoted truncation warning brought us here.
    // The decompressor is always destroyed, even when jpeg_create_decompress
    // itself failed; jpeg_destroy_decompress is safe on a partially
    // initialized struct.
    const std::string message = context->message[0] != '\0' ? std::string(context->message)
                                                            : std::string("unknown libjpeg error");
    jpeg_destroy_decompress(&context->cinfo);
    SetError(error, "DecodeJpeg: " + message);
    return false;
  }

  jpeg_create_decompress(&context->cinfo);
  jpeg_mem_src(&context->cinfo, reinterpret_cast<const unsigned char*>(bytes.data()),
               static_cast<unsigned long>(bytes.size()));
  if (jpeg_read_header(&context->cinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&context->cinfo);
    SetError(error, "DecodeJpeg: unsupported JPEG header");
    return false;
  }

  // Validate the header-reported size before allocating the pixel buffer, so a
  // hostile or corrupt header cannot request unbounded memory.
  context->width = static_cast<int>(context->cinfo.image_width);
  context->height = static_cast<int>(context->cinfo.image_height);
  {
    const std::string size_error = CheckSize(context->width, context->height);
    if (!size_error.empty()) {
      jpeg_destroy_decompress(&context->cinfo);
      SetError(error, "DecodeJpeg: " + size_error);
      return false;
    }
  }

  // Grayscale, RGB and YCbCr streams are converted to RGB by libjpeg itself.
  // CMYK and YCCK streams cannot be converted by libjpeg-turbo's libjpeg API,
  // so they are decoded to 4-channel CMYK and converted per row below.
  const bool cmyk_source =
      context->cinfo.jpeg_color_space == JCS_CMYK || context->cinfo.jpeg_color_space == JCS_YCCK;
  context->cinfo.out_color_space = cmyk_source ? JCS_CMYK : JCS_RGB;
  jpeg_start_decompress(&context->cinfo);
  const int expected_components = cmyk_source ? kCmykChannels : kRgbChannels;
  if (context->cinfo.output_width != static_cast<JDIMENSION>(context->width) ||
      context->cinfo.output_height != static_cast<JDIMENSION>(context->height) ||
      context->cinfo.output_components != expected_components) {
    jpeg_destroy_decompress(&context->cinfo);
    SetError(error, "DecodeJpeg: unsupported JPEG color format");
    return false;
  }

  // EXIF orientation, ICC profiles and other metadata are deliberately ignored:
  // pixels are decoded in the stored image orientation and color space; only
  // the color-space conversion needed for display happens here.
  const std::size_t pixel_count =
      static_cast<std::size_t>(context->width) * static_cast<std::size_t>(context->height);
  try {
    context->scanline.resize(static_cast<std::size_t>(context->width) *
                             static_cast<std::size_t>(expected_components));
    context->pixels.resize(pixel_count);
  } catch (const std::bad_alloc&) {
    jpeg_destroy_decompress(&context->cinfo);
    SetError(error, "DecodeJpeg: out of memory for " +
                        DescribeSize(context->width, context->height) + " pixel data");
    return false;
  }

  while (context->cinfo.output_scanline < context->cinfo.output_height) {
    const std::size_t row_start = static_cast<std::size_t>(context->cinfo.output_scanline);
    JSAMPROW row_pointer = context->scanline.data();
    if (jpeg_read_scanlines(&context->cinfo, &row_pointer, 1) != 1) {
      // Defensive: without a libjpeg error the call returns one scanline.
      jpeg_destroy_decompress(&context->cinfo);
      SetError(error, "DecodeJpeg: libjpeg returned no scanline");
      return false;
    }
    const unsigned char* sample = context->scanline.data();
    Argb* destination =
        context->pixels.data() + row_start * static_cast<std::size_t>(context->width);
    for (int x = 0; x < context->width; ++x) {
      if (cmyk_source) {
        const std::size_t offset = static_cast<std::size_t>(x) * kCmykChannels;
        destination[x] = InvertedCmykToArgb(sample[offset], sample[offset + 1], sample[offset + 2],
                                            sample[offset + 3]);
      } else {
        const std::size_t offset = static_cast<std::size_t>(x) * kRgbChannels;
        destination[x] = 0xFF000000u | (static_cast<Argb>(sample[offset]) << 16) |
                         (static_cast<Argb>(sample[offset + 1]) << 8) |
                         static_cast<Argb>(sample[offset + 2]);
      }
    }
  }

  jpeg_finish_decompress(&context->cinfo);
  jpeg_destroy_decompress(&context->cinfo);

  // Only assign to `out` once the whole image decoded, so a failure leaves the
  // caller's image untouched.
  Image decoded;
  decoded.width = context->width;
  decoded.height = context->height;
  decoded.pixels = std::move(context->pixels);
  *out = std::move(decoded);
  return true;
}

}  // namespace toy::render
