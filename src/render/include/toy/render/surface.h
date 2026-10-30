#pragma once

#include <cstdint>
#include <vector>

namespace toy::render {

// Packed 32-bit color: 0xAARRGGBB with straight (non-premultiplied) alpha.
using Argb = std::uint32_t;

// A rectangular block of ARGB pixels, row-major, top-left origin.
class Surface {
 public:
  // Width and height must be positive. Pixels are initialized to transparent black.
  Surface(int width, int height);

  int width() const noexcept;
  int height() const noexcept;
  // Pixels per row. M1 has no row padding, so this always equals width().
  int stride() const noexcept;

  Argb* pixels() noexcept;
  const Argb* pixels() const noexcept;

  void Fill(Argb color);
  // Fills the rectangle, clipped to the surface bounds. Non-positive sizes do nothing.
  void FillRect(int x, int y, int w, int h, Argb color);
  // Pixel access for tests. x and y must be inside the surface (asserted in debug builds).
  Argb At(int x, int y) const;

 private:
  int width_ = 0;
  int height_ = 0;
  std::vector<Argb> pixels_;
};

}  // namespace toy::render
