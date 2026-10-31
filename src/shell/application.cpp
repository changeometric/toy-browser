#include "toy/shell/application.h"

#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "page.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/net/http.h"
#include "toy/net/url.h"
#include "toy/platform/window.h"
#include "toy/render/font.h"
#include "toy/render/ppm.h"
#include "toy/render/surface.h"
#include "toy/shell/browser.h"
#include "toy/style/computed_style.h"

namespace toy::shell {
namespace {

constexpr render::Argb kPageColor = 0xFFFFFFFFu;
constexpr render::Argb kDemoBandColor = 0xFF3B6EA5u;
constexpr int kDemoBandHeight = 8;

// Keep one frame inside a sane memory budget: at 4 bytes per pixel the total
// cap is 64 MiB. Run enforces both limits for every mode, before any allocation.
constexpr int kMaxDimension = 8192;
constexpr int kMaxPixels = 16'777'216;

// Scrolling units for the window event loop: one text line (16 device px at the
// default font size), one wheel notch (three lines) and the overlap margin of a
// PageUp/PageDown jump, which scrolls one viewport minus that margin.
constexpr int kScrollPixelsPerLine = 16;
constexpr int kWheelScrollLines = 3;
constexpr int kWheelScrollPixels = kScrollPixelsPerLine * kWheelScrollLines;
constexpr int kPageScrollMarginPx = 32;

int PageScrollPixels(int viewport_height) {
  // A window shorter than the margin still scrolls by at least one pixel.
  return std::max(1, viewport_height - kPageScrollMarginPx);
}

// --- Real-font selection -----------------------------------------------------
//
// The CLI selects at most one real font: a file path (--font) or a fontconfig
// family name (--font-family). The selection is resolved once per run and then
// shared by layout metrics and painting, exactly like the built-in bitmap font
// is the shared default. All fontconfig state is process-global, so it is
// initialized exactly once and never torn down explicitly (the process is about
// to exit anyway).

// Initializes fontconfig on first use. Returns false when it cannot start at all.
bool EnsureFontconfigInitialized(std::string* error) {
  static const bool initialized = FcInit() != FcFalse;
  if (!initialized) {
    *error = "fontconfig initialization failed";
    return false;
  }
  return true;
}

// Stores fontconfig's best match for `family` in *file. Fontconfig always returns
// some installed face for a valid family name (an unknown family falls back to
// its closest match), so a match is not an error here: only a match without an
// FC_FILE entry, which should not happen, is reported. Face index and TTC
// collections are ignored deliberately (M8b MVP: face 0 of one file).
bool FontFileForFamily(const std::string& family, std::string* file, std::string* error) {
  if (!EnsureFontconfigInitialized(error)) {
    return false;
  }
  FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>(family.c_str()));
  if (pattern == nullptr) {
    *error = "fontconfig cannot parse the family name";
    return false;
  }
  FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  FcResult match_result = FcResultNoMatch;
  FcPattern* const match = FcFontMatch(nullptr, pattern, &match_result);
  FcPatternDestroy(pattern);
  if (match == nullptr) {
    *error = "fontconfig found no matching font";
    return false;
  }
  FcChar8* matched_file = nullptr;
  const FcResult file_result = FcPatternGetString(match, FC_FILE, 0, &matched_file);
  const bool has_file = file_result == FcResultMatch && matched_file != nullptr;
  if (has_file) {
    *file = reinterpret_cast<const char*>(matched_file);
  }
  FcPatternDestroy(match);
  if (!has_file) {
    *error = "the fontconfig match has no font file";
    return false;
  }
  return true;
}

// Loads the real font selected by `options` and returns it, or returns null with
// *error set. Only called when a font flag was given, so null always means a
// failure the caller must report (exit 1).
std::shared_ptr<const render::Font> LoadSelectedFont(const Options& options, std::string* error) {
  if (!options.font_family.empty()) {
    std::string family_error;
    std::string file;
    if (!FontFileForFamily(options.font_family, &file, &family_error)) {
      *error = "cannot load font family '" + options.font_family + "': " + family_error;
      return nullptr;
    }
    std::string load_error;
    std::shared_ptr<const render::Font> font = render::Font::LoadFromFile(file, &load_error);
    if (!font) {
      *error =
          "cannot load font '" + file + "' (family '" + options.font_family + "'): " + load_error;
      return nullptr;
    }
    return font;
  }
  std::string load_error;
  std::shared_ptr<const render::Font> font =
      render::Font::LoadFromFile(options.font_path, &load_error);
  if (!font) {
    *error = "cannot load font '" + options.font_path + "': " + load_error;
  }
  return font;
}

// M1 placeholder page: a white page with a blue band at the top. It proves the
// whole pixel path (surface -> presentation) works; it is also what both window
// mode and --screenshot show when no page URL is given.
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

int RunDumpDom(const Options& options) {
  std::string error;
  const std::optional<net::Url> url = net::ParseUrl(options.dump_dom_url, &error);
  if (!url) {
    std::fprintf(stderr, "toybrowser: cannot parse URL '%s': %s\n", options.dump_dom_url.c_str(),
                 error.c_str());
    return 1;
  }

  net::Response response;
  if (!net::Fetch(*url, net::FetchOptions{}, &response, &error)) {
    std::fprintf(stderr, "toybrowser: cannot fetch '%s': %s\n", options.dump_dom_url.c_str(),
                 error.c_str());
    return 1;
  }

  // HTML parsing is error-tolerant: malformed input still yields a document.
  const dom::Document document = dom::ParseDocument(response.body);
  const std::string dump = dom::DumpDocument(document);
  if (std::fwrite(dump.data(), 1, dump.size(), stdout) != dump.size()) {
    std::fprintf(stderr, "toybrowser: cannot write the DOM dump to stdout\n");
    return 1;
  }
  std::fflush(stdout);
  return 0;
}

int RunDumpStyle(const Options& options) {
  std::string error;
  const std::optional<net::Url> url = net::ParseUrl(options.dump_style_url, &error);
  if (!url) {
    std::fprintf(stderr, "toybrowser: cannot parse URL '%s': %s\n", options.dump_style_url.c_str(),
                 error.c_str());
    return 1;
  }

  net::Response response;
  if (!net::Fetch(*url, net::FetchOptions{}, &response, &error)) {
    std::fprintf(stderr, "toybrowser: cannot fetch '%s': %s\n", options.dump_style_url.c_str(),
                 error.c_str());
    return 1;
  }

  // HTML and CSS parsing are error-tolerant: malformed input still yields a tree.
  const dom::Document document = dom::ParseDocument(response.body);
  const style::StyleTree styles = style::ComputeStyles(document);
  const std::string dump = style::DumpStyleTree(document, styles);
  if (std::fwrite(dump.data(), 1, dump.size(), stdout) != dump.size()) {
    std::fprintf(stderr, "toybrowser: cannot write the style dump to stdout\n");
    return 1;
  }
  std::fflush(stdout);
  return 0;
}

int RunDumpBoxes(const Options& options, const std::shared_ptr<const render::Font>& font) {
  std::string error;
  const std::optional<net::Url> url = net::ParseUrl(options.dump_boxes_url, &error);
  if (!url) {
    std::fprintf(stderr, "toybrowser: cannot parse URL '%s': %s\n", options.dump_boxes_url.c_str(),
                 error.c_str());
    return 1;
  }

  // The box dump goes through the same page loader as painting, so the reserved
  // image boxes appear here exactly as they are laid out for the window. Image
  // failures are skipped by the loader and never fail the dump; parsing, styling
  // and layout are error-tolerant and only a page fetch error is reported.
  Page page;
  if (!LoadPage(*url, options.width, font, &page, &error)) {
    std::fprintf(stderr, "toybrowser: %s\n", error.c_str());
    return 1;
  }
  const std::string dump = layout::DumpBoxTree(page.document, page.boxes);
  if (std::fwrite(dump.data(), 1, dump.size(), stdout) != dump.size()) {
    std::fprintf(stderr, "toybrowser: cannot write the box dump to stdout\n");
    return 1;
  }
  std::fflush(stdout);
  return 0;
}

int RunHeadless(const Options& options, const std::shared_ptr<const render::Font>& font) {
  render::Surface surface(options.width, options.height);
  if (options.page_url.empty()) {
    // Backward compatible: no URL keeps the M1 placeholder path unchanged.
    PaintPlaceholderPage(surface);
  } else {
    // One Browser frame at scroll 0, through the same code path as the window;
    // for scroll 0 it stays byte-identical to the M6 painting.
    Browser browser(options.width, options.height, font);
    std::string error;
    if (!browser.Open(options.page_url, &error)) {
      std::fprintf(stderr, "toybrowser: %s\n", error.c_str());
      return 1;
    }
    browser.Paint(surface);
  }
  if (!render::WritePpm(surface, options.screenshot_path)) {
    std::fprintf(stderr, "toybrowser: cannot write screenshot '%s'\n",
                 options.screenshot_path.c_str());
    return 1;
  }
  return 0;
}

int RunWindowed(const Options& options, const std::shared_ptr<const render::Font>& font) {
  // With a page URL, open it through the Browser before creating the window so a
  // URL or fetch error fails cleanly, without ever showing an empty window.
  std::unique_ptr<Browser> browser;
  if (!options.page_url.empty()) {
    browser = std::make_unique<Browser>(options.width, options.height, font);
    std::string load_error;
    if (!browser->Open(options.page_url, &load_error)) {
      std::fprintf(stderr, "toybrowser: %s\n", load_error.c_str());
      return 1;
    }
  }

  std::string error;
  const std::string window_title = browser ? browser->title() : options.title;
  const std::unique_ptr<platform::Window> window =
      platform::Window::Create(options.width, options.height, window_title, &error);
  if (!window) {
    std::fprintf(stderr, "toybrowser: cannot open window: %s\n", error.c_str());
    return 1;
  }

  // Paints the current page (or the M1 placeholder) at the window size.
  const auto repaint = [&]() {
    render::Surface surface(window->width(), window->height());
    if (browser) {
      browser->Paint(surface);
    } else {
      PaintPlaceholderPage(surface);
    }
    window->Present(
        platform::PixelView{surface.pixels(), surface.width(), surface.height(), surface.stride()});
  };

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
      case platform::EventType::kResize:
        // The window may have been resized; re-lay-out before painting.
        if (browser) {
          browser->SetViewport(window->width(), window->height());
        }
        repaint();
        break;
      case platform::EventType::kMouseDown: {
        if (!browser) {
          break;
        }
        std::string click_error;
        if (browser->Click(event.x, event.y, &click_error)) {
          window->SetTitle(browser->title());
          repaint();
        } else if (!click_error.empty()) {
          std::fprintf(stderr, "toybrowser: %s\n", click_error.c_str());
        }
        break;
      }
      case platform::EventType::kMouseWheel:
        if (browser && browser->ScrollBy(event.wheel_delta * kWheelScrollPixels)) {
          repaint();
        }
        break;
      case platform::EventType::kKeyDown: {
        if (!browser) {
          break;
        }
        std::string navigation_error;
        bool navigated = false;
        bool scrolled = false;
        switch (event.key) {
          case platform::Key::kLeft:
            if (event.alt) {
              navigated = browser->Back(&navigation_error);
            }
            break;
          case platform::Key::kRight:
            if (event.alt) {
              navigated = browser->Forward(&navigation_error);
            }
            break;
          case platform::Key::kUp:
            scrolled = browser->ScrollBy(-kScrollPixelsPerLine);
            break;
          case platform::Key::kDown:
            scrolled = browser->ScrollBy(kScrollPixelsPerLine);
            break;
          case platform::Key::kPageUp:
            scrolled = browser->ScrollBy(-PageScrollPixels(window->height()));
            break;
          case platform::Key::kPageDown:
            scrolled = browser->ScrollBy(PageScrollPixels(window->height()));
            break;
          case platform::Key::kHome:
            scrolled = browser->ScrollToTop();
            break;
          case platform::Key::kEnd:
            scrolled = browser->ScrollToBottom();
            break;
          case platform::Key::kUnknown:
            break;
        }
        if (navigated) {
          window->SetTitle(browser->title());
          repaint();
        } else if (!navigation_error.empty()) {
          std::fprintf(stderr, "toybrowser: %s\n", navigation_error.c_str());
        } else if (scrolled) {
          repaint();
        }
        break;
      }
    }
  }
}

}  // namespace

int Run(const Options& options) {
  if (options.width < 1 || options.height < 1 || options.width > kMaxDimension ||
      options.height > kMaxDimension ||
      static_cast<long long>(options.width) * options.height > kMaxPixels) {
    std::fprintf(stderr,
                 "toybrowser: window size %dx%d is out of range (each side 1..%d, at most %d "
                 "pixels in total)\n",
                 options.width, options.height, kMaxDimension, kMaxPixels);
    return 2;
  }
  const int headless_modes = static_cast<int>(!options.screenshot_path.empty()) +
                             static_cast<int>(!options.dump_body_url.empty()) +
                             static_cast<int>(!options.dump_dom_url.empty()) +
                             static_cast<int>(!options.dump_style_url.empty()) +
                             static_cast<int>(!options.dump_boxes_url.empty());
  if (headless_modes > 1) {
    std::fprintf(stderr,
                 "toybrowser: --screenshot, --dump-body, --dump-dom, --dump-style and "
                 "--dump-boxes are mutually exclusive\n");
    return 2;
  }
  // A font selection is a usage error where it cannot take effect: two selections,
  // or a dump mode that never lays the page out. Mirrors the CLI checks so a
  // programmatic caller gets the same exit codes.
  const bool has_font = !options.font_path.empty();
  const bool has_font_family = !options.font_family.empty();
  if (has_font && has_font_family) {
    std::fprintf(stderr, "toybrowser: --font and --font-family are mutually exclusive\n");
    return 2;
  }
  if ((has_font || has_font_family) &&
      (!options.dump_body_url.empty() || !options.dump_dom_url.empty() ||
       !options.dump_style_url.empty())) {
    std::fprintf(stderr,
                 "toybrowser: --font and --font-family cannot be combined with "
                 "--dump-body, --dump-dom or --dump-style\n");
    return 2;
  }
  // Resolve the selected real font once, before any page is touched. Null with
  // no font flag means the built-in bitmap font (the pre-M8b path).
  std::shared_ptr<const render::Font> font;
  if (has_font || has_font_family) {
    std::string font_error;
    font = LoadSelectedFont(options, &font_error);
    if (!font) {
      std::fprintf(stderr, "toybrowser: %s\n", font_error.c_str());
      return 1;
    }
  }
  if (!options.dump_boxes_url.empty()) {
    return RunDumpBoxes(options, font);
  }
  if (!options.dump_style_url.empty()) {
    return RunDumpStyle(options);
  }
  if (!options.dump_dom_url.empty()) {
    return RunDumpDom(options);
  }
  if (!options.dump_body_url.empty()) {
    return RunDumpBody(options);
  }
  if (!options.screenshot_path.empty()) {
    return RunHeadless(options, font);
  }
  return RunWindowed(options, font);
}

}  // namespace toy::shell
