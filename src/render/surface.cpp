#include "toy/render/surface.h"

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace toy::render {
namespace {

std::size_t PixelCount(int width, int height) {
  assert(width > 0 && height > 0);
  return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

}  // namespace

Surface::Surface(int width, int height)
    : width_(width), height_(height), pixels_(PixelCount(width, height), 0) {}

int Surface::width() const noexcept { return width_; }

int Surface::height() const noexcept { return height_; }

int Surface::stride() const noexcept { return width_; }

Argb* Surface::pixels() noexcept { return pixels_.data(); }

const Argb* Surface::pixels() const noexcept { return pixels_.data(); }

void Surface::Fill(Argb color) { std::fill(pixels_.begin(), pixels_.end(), color); }

void Surface::FillRect(int x, int y, int w, int h, Argb color) {
  if (w <= 0 || h <= 0) {
    return;
  }
  // Compute the clipped bounds in 64-bit so that x + w and y + h cannot overflow.
  const int left = std::max(x, 0);
  const int top = std::max(y, 0);
  const int right =
      static_cast<int>(std::min(static_cast<long long>(x) + w, static_cast<long long>(width_)));
  const int bottom =
      static_cast<int>(std::min(static_cast<long long>(y) + h, static_cast<long long>(height_)));
  if (left >= right || top >= bottom) {
    return;
  }
  for (int row = top; row < bottom; ++row) {
    Argb* row_start =
        pixels_.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width_);
    std::fill(row_start + left, row_start + right, color);
  }
}

Argb Surface::At(int x, int y) const {
  assert(x >= 0 && x < width_ && y >= 0 && y < height_);
  const std::size_t index =
      static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
  return pixels_[index];
}

}  // namespace toy::render
