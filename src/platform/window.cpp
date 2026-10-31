// X11 implementation of toy::platform::Window (libX11, approved for M1).
//
// Plain XPutImage: the window keeps one client-side XImage that Present fills
// with rows from the caller's PixelView. No XShm, no double buffering and no
// other X11 extensions yet (see docs/dependencies.md).
//
// Input normalization: WaitEvent turns X11 keysyms and buttons into
// platform::Event values. Only the left button produces kMouseDown; wheel
// buttons 4 and 5 become kMouseWheel with -1 (up) and +1 (down, towards the end
// of the document). SetTitle writes _NET_WM_NAME as UTF8_STRING and WM_NAME
// through XStoreName as a fallback.
//
// WaitEvent coalesces bursts of redraw/geometry events: consecutive Expose
// events become one kExpose and consecutive ConfigureNotify events become one
// kResize carrying the final size (a burst that leaves the size unchanged
// produces no event at all). Input and close events are never merged, dropped or
// reordered: a lookahead buffer holds the first non-matching event and delivers
// it on the next call.

#include "toy/platform/window.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <string>

namespace toy::platform {
namespace {

// Creates an XImage whose pixels the window owns. For depths 24 and 32 Xlib
// uses 32 bits per pixel, so the buffer is plain uint32 pixels and a row is
// width * 4 bytes, matching PixelView. XDestroyImage frees the buffer with
// free(), so it has to come from malloc.
XImage* CreateImage(Display* display, Visual* visual, int depth, int width, int height) {
  const int bytes_per_line = width * 4;
  auto* data = static_cast<char*>(
      std::malloc(static_cast<std::size_t>(bytes_per_line) * static_cast<std::size_t>(height)));
  if (data == nullptr) {
    return nullptr;
  }
  XImage* image = XCreateImage(display, visual, static_cast<unsigned int>(depth), ZPixmap, 0, data,
                               static_cast<unsigned int>(width), static_cast<unsigned int>(height),
                               32, bytes_per_line);
  if (image == nullptr) {
    std::free(data);
  }
  return image;
}

// Sets both WM_NAME (XStoreName, the classic fallback) and _NET_WM_NAME (the
// UTF-8 EWMH property modern window managers read). Best effort by contract: a
// missing atom only skips the EWMH property and a failed Xlib call leaves the
// title unchanged instead of failing the window.
void SetWindowTitle(Display* display, ::Window window, std::string_view title_utf8,
                    Atom net_wm_name, Atom utf8_string) {
  // XStoreName needs a NUL-terminated string; the copy also holds the exact
  // UTF-8 bytes published through _NET_WM_NAME.
  const std::string title_text(title_utf8);
  XStoreName(display, window, title_text.c_str());
  if (net_wm_name != None && utf8_string != None) {
    // XChangeProperty takes an int length; a title that large is absurd, but
    // clamping keeps the conversion well defined instead of overflowing.
    const std::size_t byte_count =
        std::min(title_text.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()));
    XChangeProperty(display, window, net_wm_name, utf8_string, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title_text.data()),
                    static_cast<int>(byte_count));
  }
  XFlush(display);
}

// X11 keysym -> normalized browser key. The unshifted keysym (index 0) is
// enough: the navigation keys are never shifted characters. Everything the
// browser does not use reports kUnknown, including keypad variants.
Key NormalizeKey(KeySym keysym) {
  switch (keysym) {
    case XK_Left:
      return Key::kLeft;
    case XK_Right:
      return Key::kRight;
    case XK_Up:
      return Key::kUp;
    case XK_Down:
      return Key::kDown;
    case XK_Page_Up:
      return Key::kPageUp;
    case XK_Page_Down:
      return Key::kPageDown;
    case XK_Home:
      return Key::kHome;
    case XK_End:
      return Key::kEnd;
    default:
      return Key::kUnknown;
  }
}

class X11Window final : public Window {
 public:
  X11Window(Display* display, ::Window window, GC gc, XImage* image, Visual* visual, int depth,
            int width, int height, Atom wm_delete, Atom net_wm_name, Atom utf8_string)
      : display_(display),
        window_(window),
        gc_(gc),
        image_(image),
        visual_(visual),
        depth_(depth),
        width_(width),
        height_(height),
        wm_delete_(wm_delete),
        net_wm_name_(net_wm_name),
        utf8_string_(utf8_string) {}

  ~X11Window() override {
    if (image_ != nullptr) {
      XDestroyImage(image_);
    }
    if (gc_ != nullptr) {
      XFreeGC(display_, gc_);
    }
    if (window_ != 0) {
      XDestroyWindow(display_, window_);
    }
    XCloseDisplay(display_);
  }

  void Present(const PixelView& view) override {
    if (view.pixels == nullptr || view.width <= 0 || view.height <= 0) {
      return;
    }
    const int source_stride = view.stride > 0 ? view.stride : view.width;

    if (image_ == nullptr || image_->width != view.width || image_->height != view.height) {
      XImage* image = CreateImage(display_, visual_, depth_, view.width, view.height);
      if (image == nullptr) {
        std::fprintf(stderr, "toybrowser: cannot allocate a %dx%d X11 image\n", view.width,
                     view.height);
        return;
      }
      XDestroyImage(image_);
      image_ = image;
    }

    // The view may be smaller than the window or have a short stride; never
    // read outside the caller's buffer or the XImage.
    const int copy_width = std::min({view.width, source_stride, image_->width});
    const int copy_height = std::min(view.height, image_->height);
    const std::size_t row_bytes = static_cast<std::size_t>(copy_width) * sizeof(Argb);
    for (int y = 0; y < copy_height; ++y) {
      std::memcpy(image_->data + static_cast<std::size_t>(y) * image_->bytes_per_line,
                  view.pixels + static_cast<std::size_t>(y) * source_stride, row_bytes);
    }
    XPutImage(display_, window_, gc_, image_, 0, 0, 0, 0, static_cast<unsigned int>(copy_width),
              static_cast<unsigned int>(copy_height));
    XFlush(display_);
  }

  void SetTitle(std::string_view title_utf8) override {
    if (closed_) {
      return;
    }
    SetWindowTitle(display_, window_, title_utf8, net_wm_name_, utf8_string_);
  }

  bool WaitEvent(Event* event) override {
    if (closed_ || event == nullptr) {
      return false;
    }
    for (;;) {
      XEvent xevent;
      if (!NextRawEvent(&xevent)) {
        std::fprintf(stderr, "toybrowser: XNextEvent failed, treating the window as closed\n");
        closed_ = true;
        event->type = EventType::kClose;
        event->width = width_;
        event->height = height_;
        return false;
      }
      switch (xevent.type) {
        case Expose:
          // X reports damage per region; one repaint covers them all, so merge a
          // burst of consecutive Expose events into a single kExpose.
          DrainConsecutiveEvents(Expose);
          event->type = EventType::kExpose;
          event->width = width_;
          event->height = height_;
          return true;
        case ConfigureNotify: {
          // A resize drag produces a burst of consecutive ConfigureNotify events;
          // merge them into one kResize carrying the final size. A burst that
          // leaves the size unchanged reports no event: keep waiting (blocking,
          // as before) so the caller never relayouts for a no-op configure.
          const int previous_width = width_;
          const int previous_height = height_;
          width_ = xevent.xconfigure.width;
          height_ = xevent.xconfigure.height;
          XEvent next;
          while (TryRawEvent(&next)) {
            if (next.type != ConfigureNotify) {
              pending_.push_front(next);
              break;
            }
            width_ = next.xconfigure.width;
            height_ = next.xconfigure.height;
          }
          if (width_ == previous_width && height_ == previous_height) {
            continue;
          }
          event->type = EventType::kResize;
          event->width = width_;
          event->height = height_;
          return true;
        }
        case KeyPress: {
          // index 0 is the unshifted keysym; Shift is reported separately.
          const KeySym keysym = XLookupKeysym(&xevent.xkey, 0);
          event->type = EventType::kKeyDown;
          event->width = width_;
          event->height = height_;
          event->key = NormalizeKey(keysym);
          event->alt = (xevent.xkey.state & Mod1Mask) != 0;
          event->ctrl = (xevent.xkey.state & ControlMask) != 0;
          event->shift = (xevent.xkey.state & ShiftMask) != 0;
          return true;
        }
        case ButtonPress: {
          // x/y are relative to this window and already in device pixels, with
          // the top-left origin the Event contract requires.
          const unsigned int button = xevent.xbutton.button;
          if (button == Button1) {
            event->type = EventType::kMouseDown;
            event->width = width_;
            event->height = height_;
            event->x = xevent.xbutton.x;
            event->y = xevent.xbutton.y;
            return true;
          }
          if (button == Button4 || button == Button5) {
            event->type = EventType::kMouseWheel;
            event->width = width_;
            event->height = height_;
            event->wheel_delta = button == Button4 ? -1 : 1;
            return true;
          }
          break;  // Other buttons produce no event.
        }
        case ClientMessage:
          if (static_cast<Atom>(xevent.xclient.data.l[0]) == wm_delete_) {
            closed_ = true;
            event->type = EventType::kClose;
            event->width = width_;
            event->height = height_;
            return false;
          }
          break;
        default:
          break;
      }
    }
  }

  int width() const override { return width_; }
  int height() const override { return height_; }

 private:
  // Pops the next raw X event, preferring the single lookahead slot over Xlib.
  // Blocks when no event is queued. Returns false when Xlib fails.
  bool NextRawEvent(XEvent* event) {
    if (!pending_.empty()) {
      *event = pending_.front();
      pending_.pop_front();
      return true;
    }
    return XNextEvent(display_, event) == 0;
  }

  // Pops the next raw X event only when one is already available (lookahead
  // slot or the Xlib queue). Never blocks; false means nothing is queued now.
  bool TryRawEvent(XEvent* event) {
    if (!pending_.empty()) {
      *event = pending_.front();
      pending_.pop_front();
      return true;
    }
    if (XPending(display_) <= 0) {
      return false;
    }
    return XNextEvent(display_, event) == 0;
  }

  // Consumes consecutive already-queued events of `type` after the current one.
  // The first event of another type is put back into the lookahead slot, so only
  // consecutive same-kind events are merged and later input keeps its order.
  void DrainConsecutiveEvents(int type) {
    XEvent next;
    while (TryRawEvent(&next)) {
      if (next.type != type) {
        pending_.push_front(next);
        return;
      }
    }
  }

  Display* display_;
  ::Window window_ = 0;
  GC gc_ = nullptr;
  XImage* image_ = nullptr;
  Visual* visual_ = nullptr;
  int depth_ = 0;
  int width_ = 0;
  int height_ = 0;
  Atom wm_delete_ = None;
  Atom net_wm_name_ = None;
  Atom utf8_string_ = None;
  bool closed_ = false;
  // Raw events read from Xlib while coalescing a burst but not yet delivered.
  std::deque<XEvent> pending_;
};

}  // namespace

std::unique_ptr<Window> Window::Create(int width, int height, std::string_view title,
                                       std::string* error) {
  if (width <= 0 || height <= 0) {
    if (error != nullptr) {
      *error = "window size must be positive";
    }
    return nullptr;
  }

  Display* display = XOpenDisplay(nullptr);
  if (display == nullptr) {
    if (error != nullptr) {
      *error = "cannot open an X display (DISPLAY is unset or no X server is reachable)";
    }
    return nullptr;
  }

  auto fail = [&](const char* message) -> std::unique_ptr<Window> {
    if (error != nullptr) {
      *error = message;
    }
    XCloseDisplay(display);
    return nullptr;
  };

  const int screen = DefaultScreen(display);
  const int depth = DefaultDepth(display, screen);
  Visual* visual = DefaultVisual(display, screen);
  if (visual == nullptr || visual->c_class != TrueColor || (depth != 24 && depth != 32)) {
    return fail("the X display's default visual is not 24-bit or 32-bit TrueColor");
  }

  XSetWindowAttributes attributes{};
  attributes.background_pixel = BlackPixel(display, screen);
  attributes.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask;
  const ::Window window =
      XCreateWindow(display, RootWindow(display, screen), 0, 0, static_cast<unsigned int>(width),
                    static_cast<unsigned int>(height), 0, depth, InputOutput, visual,
                    CWBackPixel | CWEventMask, &attributes);
  if (window == 0) {
    return fail("cannot create an X11 window");
  }

  Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  if (wm_delete == None || XSetWMProtocols(display, window, &wm_delete, 1) == 0) {
    XDestroyWindow(display, window);
    return fail("cannot register the WM_DELETE_WINDOW protocol");
  }

  // Title atoms are optional: SetWindowTitle falls back to XStoreName alone if
  // interning fails, so a title failure never fails window creation.
  const Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
  const Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
  SetWindowTitle(display, window, title, net_wm_name, utf8_string);

  XMapWindow(display, window);

  GC gc = XCreateGC(display, window, 0, nullptr);
  if (gc == nullptr) {
    XDestroyWindow(display, window);
    return fail("cannot create an X11 graphics context");
  }

  XImage* image = CreateImage(display, visual, depth, width, height);
  if (image == nullptr) {
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    return fail("cannot allocate the X11 image buffer");
  }

  XFlush(display);
  return std::unique_ptr<Window>(new X11Window(display, window, gc, image, visual, depth, width,
                                               height, wm_delete, net_wm_name, utf8_string));
}

}  // namespace toy::platform
