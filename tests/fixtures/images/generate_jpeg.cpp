// Regenerates the committed JPEG fixtures in this directory with the approved
// libjpeg (libjpeg-turbo) dependency. This is a development tool, not part of
// the CMake build; README.md documents the exact compile and run commands.
//
// The pixel content is documented in README.md. JPEG is lossy and the exact
// bytes depend on the libjpeg version, so the tests compare decoded pixels with
// a documented tolerance instead of whole files. The fixtures contain no
// third-party data: every pixel is generated here.
//
// Usage (from the repository root):
//   mkdir -p .tmp
//   g++ -std=c++20 -O2 -Wall -Wextra tests/fixtures/images/generate_jpeg.cpp
//       -o .tmp/generate_jpeg -ljpeg
//   .tmp/generate_jpeg tests/fixtures/images

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// <jpeglib.h> is a C header that needs <cstddef> and <cstdio> first in C++ mode.
#include <jpeglib.h>

namespace {

constexpr int kQuality = 100;

struct ErrorManager {
  jpeg_error_mgr base;
  char message[JMSG_LENGTH_MAX];
};

[[noreturn]] void Fail(const std::string& message) {
  std::fprintf(stderr, "generate_jpeg: %s\n", message.c_str());
  std::exit(EXIT_FAILURE);
}

void OnJpegError(j_common_ptr cinfo) {
  auto* error = reinterpret_cast<ErrorManager*>(cinfo->err);
  (*cinfo->err->format_message)(cinfo, error->message);
  Fail(error->message);
}

struct Rgb {
  unsigned char red;
  unsigned char green;
  unsigned char blue;
};

struct Cmyk {
  unsigned char cyan;
  unsigned char magenta;
  unsigned char yellow;
  unsigned char black;
};

// Fills a width x height sample buffer with four quadrants (top-left,
// top-right, bottom-left, bottom-right). All fixtures are even-sized, so every
// quadrant gets exactly a quarter of the pixels.
template <typename Pixel, typename Store>
std::vector<unsigned char> Quadrants(int width, int height, Pixel top_left, Pixel top_right,
                                     Pixel bottom_left, Pixel bottom_right, Store store) {
  std::vector<unsigned char> samples(static_cast<std::size_t>(width) *
                                     static_cast<std::size_t>(height) * sizeof(Pixel));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const Pixel& pixel = y < height / 2 ? (x < width / 2 ? top_left : top_right)
                                          : (x < width / 2 ? bottom_left : bottom_right);
      store(pixel, &samples[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                            sizeof(Pixel)]);
    }
  }
  return samples;
}

std::vector<unsigned char> RgbQuadrants(int width, int height, Rgb top_left, Rgb top_right,
                                        Rgb bottom_left, Rgb bottom_right) {
  return Quadrants<Rgb>(width, height, top_left, top_right, bottom_left, bottom_right,
                        [](const Rgb& pixel, unsigned char* destination) {
                          destination[0] = pixel.red;
                          destination[1] = pixel.green;
                          destination[2] = pixel.blue;
                        });
}

std::vector<unsigned char> GrayQuadrants(int width, int height, unsigned char top_left,
                                         unsigned char top_right, unsigned char bottom_left,
                                         unsigned char bottom_right) {
  struct Gray {
    unsigned char value;
  };
  return Quadrants<Gray>(
      width, height, Gray{top_left}, Gray{top_right}, Gray{bottom_left}, Gray{bottom_right},
      [](const Gray& pixel, unsigned char* destination) { destination[0] = pixel.value; });
}

std::vector<unsigned char> CmykQuadrants(int width, int height, Cmyk top_left, Cmyk top_right,
                                         Cmyk bottom_left, Cmyk bottom_right) {
  return Quadrants<Cmyk>(width, height, top_left, top_right, bottom_left, bottom_right,
                         [](const Cmyk& pixel, unsigned char* destination) {
                           destination[0] = pixel.cyan;
                           destination[1] = pixel.magenta;
                           destination[2] = pixel.yellow;
                           destination[3] = pixel.black;
                         });
}

std::vector<unsigned char> SolidRgb(int width, int height, Rgb color) {
  return RgbQuadrants(width, height, color, color, color, color);
}

// Writes one JPEG file from already converted samples. The compressor uses
// quality 100 and 1x1 sampling for every component (no chroma subsampling), so
// the decoded colors stay close to the stored samples.
void WriteJpeg(const std::string& path, int width, int height,
               const std::vector<unsigned char>& samples, int components,
               J_COLOR_SPACE in_color_space, bool progressive) {
  const std::size_t expected_size = static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height) *
                                    static_cast<std::size_t>(components);
  if (samples.size() != expected_size) {
    Fail("sample buffer size mismatch for " + path);
  }

  jpeg_compress_struct cinfo{};
  ErrorManager error{};
  cinfo.err = jpeg_std_error(&error.base);
  error.base.error_exit = OnJpegError;
  jpeg_create_compress(&cinfo);

  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    Fail("cannot open " + path + " for writing");
  }
  jpeg_stdio_dest(&cinfo, file);

  cinfo.image_width = static_cast<JDIMENSION>(width);
  cinfo.image_height = static_cast<JDIMENSION>(height);
  cinfo.input_components = components;
  cinfo.in_color_space = in_color_space;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, kQuality, TRUE);
  for (int component = 0; component < cinfo.num_components; ++component) {
    cinfo.comp_info[component].h_samp_factor = 1;
    cinfo.comp_info[component].v_samp_factor = 1;
  }
  if (progressive) {
    jpeg_simple_progression(&cinfo);
  }

  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row =
        const_cast<JSAMPLE*>(samples.data() + static_cast<std::size_t>(cinfo.next_scanline) *
                                                  static_cast<std::size_t>(width) *
                                                  static_cast<std::size_t>(components));
    jpeg_write_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  if (std::fclose(file) != 0) {
    Fail("cannot close " + path);
  }
}

std::vector<unsigned char> ReadFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("cannot read " + path);
  }
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
  if (input.bad() || bytes.empty()) {
    Fail("cannot read " + path);
  }
  return bytes;
}

// Copies `source` up to the middle of its entropy-coded scan: the header stays
// complete and the scan breaks off partway through, which is the truncation the
// decoder must reject. The scan begins after the SOS marker segment.
void WriteTruncatedCopy(const std::string& source, const std::string& destination) {
  const std::vector<unsigned char> bytes = ReadFile(source);
  std::size_t scan_start = 0;
  for (std::size_t i = 0; i + 3 < bytes.size(); ++i) {
    if (bytes[i] == 0xFF && bytes[i + 1] == 0xDA) {
      const std::size_t segment_length =
          (static_cast<std::size_t>(bytes[i + 2]) << 8) | bytes[i + 3];
      scan_start = i + 2 + segment_length;
      break;
    }
  }
  if (scan_start == 0 || scan_start >= bytes.size()) {
    Fail("cannot locate the scan in " + source);
  }
  const std::size_t keep = scan_start + (bytes.size() - scan_start) / 2;
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("cannot open " + destination + " for writing");
  }
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(keep));
  if (!output) {
    Fail("cannot write " + destination);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string directory = argc >= 2 ? argv[1] : ".";
  const auto path = [&directory](const char* name) { return directory + "/" + name; };

  // Baseline RGB: 8x4 quadrants red, green, blue, white.
  WriteJpeg(path("jpeg_rgb_16x8.jpg"), 16, 8,
            RgbQuadrants(16, 8, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}), 3, JCS_RGB,
            false);

  // Grayscale: 8x4 quadrants 0, 64, 128, 255.
  WriteJpeg(path("jpeg_gray_16x8.jpg"), 16, 8, GrayQuadrants(16, 8, 0, 64, 128, 255), 1,
            JCS_GRAYSCALE, false);

  // Progressive RGB with the same quadrants as the baseline fixture.
  WriteJpeg(path("jpeg_progressive_16x8.jpg"), 16, 8,
            RgbQuadrants(16, 8, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}), 3, JCS_RGB,
            true);

  // CMYK: libjpeg writes an Adobe CMYK JPEG. The stored samples are inverted
  // (255 = no ink), so the four quadrant values correspond to red, green, blue
  // and black after the c*k/255 conversion back to RGB.
  WriteJpeg(
      path("jpeg_cmyk_16x8.jpg"), 16, 8,
      CmykQuadrants(16, 8, {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {0, 0, 0, 0}), 4,
      JCS_CMYK, false);

  // A valid 9000 px wide image: the decoder must reject it before allocating.
  WriteJpeg(path("jpeg_too_wide_9000x1.jpg"), 9000, 1, SolidRgb(9000, 1, {255, 255, 255}), 3,
            JCS_RGB, false);

  // A valid header whose entropy-coded scan breaks off partway through.
  WriteTruncatedCopy(path("jpeg_rgb_16x8.jpg"), path("jpeg_truncated_16x8.jpg"));

  std::printf("generated JPEG fixtures in %s\n", directory.c_str());
  return 0;
}
