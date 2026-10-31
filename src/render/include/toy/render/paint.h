#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "toy/render/image.h"
#include "toy/render/surface.h"

namespace toy::render {

// One entry of the flat display list. Coordinates are device pixels with a
// top-left origin (1 CSS px = 1 device px in the MVP). Colors are 0xAARRGGBB
// with straight alpha. `text` and `font_size_px` are meaningful for kDrawText
// only, `width`/`height` for kFillRect, kDrawImage and kPushClip, and `image`
// for kDrawImage only.
enum class CommandKind { kFillRect, kDrawText, kPushClip, kPopClip, kDrawImage };

struct Command {
  CommandKind kind = CommandKind::kFillRect;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  Argb color = 0;
  std::string text;
  float font_size_px = 0.0f;
  // Shared, non-owning for the caller: the display list keeps the image alive,
  // so a command may outlive the page data it was built from.
  std::shared_ptr<const Image> image;
};

// A flat list of painting commands. The builders only append; validation is
// the rasterizer's job, so an invalid command is simply a no-op when drawn.
class DisplayList {
 public:
  void FillRect(int x, int y, int width, int height, Argb color);
  void DrawText(std::string_view text_utf8, int x, int y, float font_size_px, Argb color);
  // Draws `image` scaled into the device-pixel rectangle (x, y, width, height).
  // Scaling is nearest-neighbour: destination pixel (dx, dy) samples source
  // pixel ((dx - x) * image->width / width, (dy - y) * image->height / height)
  // with integer floor division, so no interpolation or anti-aliasing is added.
  // Pixels are composited with the same SrcOver formula as fills; a null image,
  // a non-positive size or an empty image is a no-op when drawn.
  void DrawImage(std::shared_ptr<const Image> image, int x, int y, int width, int height);
  void PushClip(int x, int y, int width, int height);
  void PopClip();

  const std::vector<Command>& commands() const;
  std::size_t size() const;

 private:
  std::vector<Command> commands_;
};

// Draws the list into `surface` (it does not clear it first). The initial clip
// is the whole surface; commands execute in order. Pure and deterministic:
// two surfaces that are equal before the call are equal after it.
void DrawDisplayList(const DisplayList& list, Surface& surface);

}  // namespace toy::render
