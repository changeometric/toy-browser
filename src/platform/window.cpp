// X11 implementation of toy::platform::Window (libX11, approved for M1).
//
// Plain XPutImage: the window keeps one client-side XImage that Present fills
// with rows from the caller's PixelView. No XShm, no double buffering and no
// other X11 extensions yet (see docs/dependencies.md).

#include "toy/platform/window.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

class X11Window final : public Window {
 public:
  X11Window(Display* display, ::Window window, GC gc, XImage* image, Visual* visual, int depth,
            int width, int height, Atom wm_delete)
      : display_(display),
        window_(window),
        gc_(gc),
        image_(image),
        visual_(visual),
        depth_(depth),
        width_(width),
        height_(height),
        wm_delete_(wm_delete) {}

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

  bool WaitEvent(Event* event) override {
    if (closed_ || event == nullptr) {
      return false;
    }
    for (;;) {
      XEvent xevent;
      if (XNextEvent(display_, &xevent) != 0) {
        std::fprintf(stderr, "toybrowser: XNextEvent failed, treating the window as closed\n");
        closed_ = true;
        event->type = EventType::kClose;
        event->width = width_;
        event->height = height_;
        return false;
      }
      switch (xevent.type) {
        case Expose:
          event->type = EventType::kExpose;
          event->width = width_;
          event->height = height_;
          return true;
        case ConfigureNotify:
          width_ = xevent.xconfigure.width;
          height_ = xevent.xconfigure.height;
          event->type = EventType::kResize;
          event->width = width_;
          event->height = height_;
          return true;
        case KeyPress: {
          const KeySym keysym = XLookupKeysym(&xevent.xkey, 0);
          event->type = EventType::kKeyDown;
          event->width = width_;
          event->height = height_;
          event->key = static_cast<std::uint32_t>(keysym);
          return true;
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
  Display* display_;
  ::Window window_ = 0;
  GC gc_ = nullptr;
  XImage* image_ = nullptr;
  Visual* visual_ = nullptr;
  int depth_ = 0;
  int width_ = 0;
  int height_ = 0;
  Atom wm_delete_ = None;
  bool closed_ = false;
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
  attributes.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask;
  const ::Window window =
      XCreateWindow(display, RootWindow(display, screen), 0, 0, static_cast<unsigned int>(width),
                    static_cast<unsigned int>(height), 0, depth, InputOutput, visual,
                    CWBackPixel | CWEventMask, &attributes);
  if (window == 0) {
    return fail("cannot create an X11 window");
  }

  const std::string title_text(title);
  XStoreName(display, window, title_text.c_str());

  Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  if (wm_delete == None || XSetWMProtocols(display, window, &wm_delete, 1) == 0) {
    XDestroyWindow(display, window);
    return fail("cannot register the WM_DELETE_WINDOW protocol");
  }

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
  return std::unique_ptr<Window>(
      new X11Window(display, window, gc, image, visual, depth, width, height, wm_delete));
}

}  // namespace toy::platform
