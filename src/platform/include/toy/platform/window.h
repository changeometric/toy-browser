#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace toy::platform {

// 32-bit pixel: 0xAARRGGBB, straight alpha, row-major, top-left origin. The
// renderer produces the same layout, so surfaces are presented by copying
// rows instead of converting them.
using Argb = std::uint32_t;

// A non-owning view of a pixel buffer owned by the caller (normally a
// toy::render::Surface).
struct PixelView {
  // Pixel data; never null when width and height are positive.
  const Argb* pixels = nullptr;
  int width = 0;
  int height = 0;
  // Row stride in pixels (Argb elements), not bytes; 0 means width.
  int stride = 0;
};

enum class EventType { kExpose, kResize, kClose, kKeyDown };

struct Event {
  EventType type = EventType::kExpose;
  // Current window size for kExpose and kResize.
  int width = 0;
  int height = 0;
  // X11 keysym for kKeyDown.
  std::uint32_t key = 0;
};

// An OS window that presents pixel surfaces and forwards input events.
// In M1 the only implementation is the X11 backend. Everything is
// single-threaded and driven by WaitEvent.
class Window {
 public:
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  virtual ~Window() = default;

  // Creates and maps a window. Returns nullptr and fills *error (when non-null)
  // if there is no usable display, which is not a crash: callers can fall back
  // to the headless path. width and height must be positive.
  static std::unique_ptr<Window> Create(int width, int height, std::string_view title,
                                        std::string* error);

  // Copies the view into the window. The view is expected to match the window
  // size; out-of-bounds parts are clipped instead of read or written.
  virtual void Present(const PixelView& view) = 0;

  // Blocks until the next event is available. Returns false once the window is
  // gone; in that case a kClose event is still delivered. After a false return
  // the window must not be used anymore.
  virtual bool WaitEvent(Event* event) = 0;

  virtual int width() const = 0;
  virtual int height() const = 0;

 protected:
  Window() = default;
};

}  // namespace toy::platform
