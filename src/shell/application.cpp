#include "toy/shell/application.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>

#include "toy/net/http.h"
#include "toy/net/url.h"
#include "toy/platform/window.h"
#include "toy/render/ppm.h"
#include "toy/render/surface.h"

namespace toy::shell {
namespace {

constexpr render::Argb kPageColor = 0xFFFFFFFFu;
constexpr render::Argb kDemoBandColor = 0xFF3B6EA5u;
constexpr int kDemoBandHeight = 8;

// M1 placeholder page: a white page with a blue band at the top. It proves the
// whole pixel path (surface -> presentation) works; real page content arrives
// with the later milestones.
void PaintPlaceholderPage(render::Surface& surface) {
  surface.Fill(kPageColor);
  surface.FillRect(0, 0, surface.width(), kDemoBandHeight, kDemoBandColor);
}

int RunDumpBody(const Options& options) {
  std::string error;
  const std::optional<net::Url> url = net::ParseUrl(options.dump_body_url, &error);
  if (!url) {
    std::fprintf(stderr, "toybrowser: cannot parse URL '%s': %s\n", options.dump_body_url.c_str(),
                 error.c_str());
    return 1;
  }

  net::Response response;
  if (!net::Fetch(*url, net::FetchOptions{}, &response, &error)) {
    std::fprintf(stderr, "toybrowser: cannot fetch '%s': %s\n", options.dump_body_url.c_str(),
                 error.c_str());
    return 1;
  }

  // Raw bytes and no trailing newline: the dump reproduces the body exactly.
  if (std::fwrite(response.body.data(), 1, response.body.size(), stdout) != response.body.size()) {
    std::fprintf(stderr, "toybrowser: cannot write the response body to stdout\n");
    return 1;
  }
  std::fflush(stdout);
  return 0;
}

int RunHeadless(const Options& options) {
  render::Surface surface(options.width, options.height);
  PaintPlaceholderPage(surface);
  if (!render::WritePpm(surface, options.screenshot_path)) {
    std::fprintf(stderr, "toybrowser: cannot write screenshot '%s'\n",
                 options.screenshot_path.c_str());
    return 1;
  }
  return 0;
}

int RunWindowed(const Options& options) {
  std::string error;
  const std::unique_ptr<platform::Window> window =
      platform::Window::Create(options.width, options.height, options.title, &error);
  if (!window) {
    std::fprintf(stderr, "toybrowser: cannot open window: %s\n", error.c_str());
    return 1;
  }

  while (true) {
    platform::Event event;
    if (!window->WaitEvent(&event)) {
      // The window is gone; there is nothing left to present.
      return 0;
    }
    switch (event.type) {
      case platform::EventType::kClose:
        return 0;
      case platform::EventType::kExpose:
      case platform::EventType::kResize: {
        // Redraw the whole page at the window's current size.
        render::Surface surface(window->width(), window->height());
        PaintPlaceholderPage(surface);
        window->Present(platform::PixelView{surface.pixels(), surface.width(), surface.height(),
                                            surface.stride()});
        break;
      }
      case platform::EventType::kKeyDown:
        break;
    }
  }
}

}  // namespace

int Run(const Options& options) {
  if (options.width <= 0 || options.height <= 0) {
    std::fprintf(stderr, "toybrowser: window size must be positive, got %dx%d\n", options.width,
                 options.height);
    return 2;
  }
  if (!options.screenshot_path.empty() && !options.dump_body_url.empty()) {
    std::fprintf(stderr, "toybrowser: --dump-body cannot be combined with --screenshot\n");
    return 2;
  }
  if (!options.dump_body_url.empty()) {
    return RunDumpBody(options);
  }
  if (!options.screenshot_path.empty()) {
    return RunHeadless(options);
  }
  return RunWindowed(options);
}

}  // namespace toy::shell
