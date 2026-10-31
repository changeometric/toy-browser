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

enum class EventType { kExpose, kResize, kClose, kKeyDown, kMouseDown, kMouseWheel };

// Keys the browser uses, normalized by the backend so callers never see X11
// keysyms. Any other key reports kUnknown.
enum class Key { kUnknown, kLeft, kRight, kUp, kDown, kPageUp, kPageDown, kHome, kEnd };

struct Event {
  EventType type = EventType::kExpose;
  // Current window size for kExpose and kResize (also filled in for the other
  // event types so a handler can always mirror the window size).
  int width = 0;
  int height = 0;
  // kKeyDown: the normalized key and its modifier state.
  Key key = Key::kUnknown;
  bool alt = false;
  bool ctrl = false;
  bool shift = false;
  // kMouseDown: pointer position in window device pixels, top-left origin. Only
  // the primary (left) button produces this event; other buttons are ignored.
  int x = 0;
  int y = 0;
  // kMouseWheel: wheel notches, positive scrolling towards the end of the
  // document (X11 buttons 4 and 5 map to -1 and +1).
  int wheel_delta = 0;
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

  // Sets the window title from UTF-8 text (best effort: characters the window
  // manager cannot display may be replaced; the backend must never fail).
  virtual void SetTitle(std::string_view title_utf8) = 0;

  // Blocks until the next event is available. Returns false once the window is
  // gone; in that case a kClose event is still delivered. After a false return
  // the window must not be used anymore.
  //
  // A backend may coalesce bursts of same-kind events that are already queued:
  // consecutive Expose events are reported as one kExpose (one repaint covers
  // every damaged region), and consecutive ConfigureNotify events are reported
  // as one kResize carrying the final size. A configure burst that leaves the
  // size unchanged produces no kResize at all; WaitEvent keeps blocking for the
  // next event. Only consecutive Expose/ConfigureNotify events may be merged:
  // every input and close event is delivered exactly once, in arrival order,
  // and the blocking semantics above apply either way.
  virtual bool WaitEvent(Event* event) = 0;

  virtual int width() const = 0;
  virtual int height() const = 0;

 protected:
  Window() = default;
};

}  // namespace toy::platform
