#include "toy/render/ppm.h"

#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

namespace toy::render {

bool WritePpm(const Surface& surface, const std::string& path) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  const int width = surface.width();
  const int height = surface.height();
  file << "P6\n" << width << ' ' << height << "\n255\n";
  if (!file) {
    return false;
  }

  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<unsigned char> rgb(pixel_count * 3);
  const Argb* pixels = surface.pixels();
  for (std::size_t i = 0; i < pixel_count; ++i) {
    const Argb pixel = pixels[i];
    rgb[i * 3 + 0] = static_cast<unsigned char>((pixel >> 16) & 0xFF);
    rgb[i * 3 + 1] = static_cast<unsigned char>((pixel >> 8) & 0xFF);
    rgb[i * 3 + 2] = static_cast<unsigned char>(pixel & 0xFF);
  }

  file.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
  file.close();
  return !file.fail();
}

}  // namespace toy::render
