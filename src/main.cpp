// The toy browser's command-line entry point.
//
// Parses the options, then hands them to toy::shell::Run. Fetching and layout
// work through --dump-body, --dump-dom, --dump-style and --dump-boxes; the
// window and --screenshot render the page given by the optional positional URL,
// or the M1 placeholder page when no URL is given (see docs/roadmap.md).

#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>

#include "toy/shell/application.h"

namespace {

constexpr std::string_view kProgramName = "toybrowser";

void PrintUsage(std::FILE* stream, const char* argv0) {
  std::fprintf(
      stream,
      "Usage: %s [options] [url]\n"
      "\n"
      "A toy web browser built from scratch. Fetching, layout and painting work via\n"
      "--dump-body, --dump-dom, --dump-style, --dump-boxes and --screenshot; the window and\n"
      "--screenshot render a real page when a URL is given, and the placeholder page otherwise.\n"
      "\n"
      "Options:\n"
      "  -h, --help         Show this help and exit.\n"
      "  -v, --version      Show the version and exit.\n"
      "  --headless         No window; requires a --dump-* mode or --screenshot.\n"
      "  --screenshot PATH  Write one frame to PATH as a PPM image; implies --headless.\n"
      "  --dump-body URL    Fetch URL and write the raw response body to stdout; no window.\n"
      "  --dump-dom URL     Fetch URL as HTML and write the DOM dump to stdout; no window.\n"
      "  --dump-style URL   Fetch URL and write its computed style tree to stdout; no window.\n"
      "  --dump-boxes URL   Fetch URL and write its laid-out box tree to stdout; no window.\n"
      "  --size WxH         Window size in pixels (default 800x600; limits below).\n"
      "  --font PATH        Use the real font file PATH for layout and painting. Allowed\n"
      "                     with window mode, --screenshot and --dump-boxes. Without it\n"
      "                     (and without --font-family) the built-in bitmap font is used.\n"
      "  --font-family NAME Use the installed font that fontconfig matches for family\n"
      "                     NAME (best match; unknown families fall back). Mutually\n"
      "                     exclusive with --font.\n"
      "\n"
      "Optional URL:\n"
      "  url                Page URL for window mode and --screenshot; without it the\n"
      "                     placeholder page is shown. The --dump-* modes take the URL as\n"
      "                     their flag argument instead.\n"
      "\n"
      "Window size limits: 1..8192 per side, 16777216 pixels in total.\n",
      argv0);
}

int FailUsage(const char* argv0, const std::string& message) {
  std::fprintf(stderr, "%s: %s\n\n", kProgramName.data(), message.c_str());
  PrintUsage(stderr, argv0);
  return 2;
}

bool ParsePositiveInt(std::string_view text, int* value) {
  if (text.empty()) {
    return false;
  }
  int parsed = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (error != std::errc() || end != text.data() + text.size() || parsed <= 0) {
    return false;
  }
  *value = parsed;
  return true;
}

bool ParseSize(std::string_view text, int* width, int* height) {
  const std::size_t separator = text.find('x');
  if (separator == std::string_view::npos) {
    return false;
  }
  return ParsePositiveInt(text.substr(0, separator), width) &&
         ParsePositiveInt(text.substr(separator + 1), height);
}

}  // namespace

int main(int argc, char** argv) {
  toy::shell::Options options;
  bool headless = false;
  bool has_page_url = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view argument = argv[i];
    if (argument == "-h" || argument == "--help") {
      PrintUsage(stdout, argv[0]);
      return 0;
    }
    if (argument == "-v" || argument == "--version") {
      std::printf("%s %s\n", kProgramName.data(), TOYBROWSER_VERSION);
      return 0;
    }
    if (argument == "--headless") {
      headless = true;
      continue;
    }
    if (argument == "--screenshot") {
      if (i + 1 >= argc) {
        return FailUsage(argv[0], "--screenshot requires a file path");
      }
      options.screenshot_path = argv[++i];
      headless = true;
      continue;
    }
    if (argument == "--dump-body") {
      if (i + 1 >= argc) {
        return FailUsage(argv[0], "--dump-body requires a URL");
      }
      const std::string_view url = argv[++i];
      if (url.empty()) {
        return FailUsage(argv[0], "--dump-body requires a non-empty URL");
      }
      options.dump_body_url = url;
      continue;
    }
    if (argument == "--dump-dom") {
      if (i + 1 >= argc) {
        return FailUsage(argv[0], "--dump-dom requires a URL");
      }
      const std::string_view url = argv[++i];
      if (url.empty()) {
        return FailUsage(argv[0], "--dump-dom requires a non-empty URL");
      }
      options.dump_dom_url = url;
      continue;
    }
    if (argument == "--dump-style") {
      if (i + 1 >= argc) {
        return FailUsage(argv[0], "--dump-style requires a URL");
      }
      const std::string_view url = argv[++i];
      if (url.empty()) {
        return FailUsage(argv[0], "--dump-style requires a non-empty URL");
      }
      options.dump_style_url = url;
      continue;
    }
    if (argument == "--dump-boxes") {
      if (i + 1 >= argc) {
        return FailUsage(argv[0], "--dump-boxes requires a URL");
      }
      const std::string_view url = argv[++i];
      if (url.empty()) {
        return FailUsage(argv[0], "--dump-boxes requires a non-empty URL");
      }
      options.dump_boxes_url = url;
      continue;
    }
    if (argument == "--font") {
      if (i + 1 >= argc) {
        return FailUsage(argv[0], "--font requires a font file path");
      }
      const std::string_view path = argv[++i];
      if (path.empty()) {
        return FailUsage(argv[0], "--font requires a non-empty font file path");
      }
      options.font_path = path;
      continue;
    }
    if (argument == "--font-family") {
      if (i + 1 >= argc) {
        return FailUsage(argv[0], "--font-family requires a family name");
      }
      const std::string_view family = argv[++i];
      if (family.empty()) {
        return FailUsage(argv[0], "--font-family requires a non-empty family name");
      }
      options.font_family = family;
      continue;
    }
    if (argument == "--size") {
      if (i + 1 >= argc) {
        return FailUsage(argv[0], "--size requires a value of the form WxH");
      }
      const std::string_view size = argv[++i];
      if (!ParseSize(size, &options.width, &options.height)) {
        return FailUsage(argv[0], "invalid window size '" + std::string(size) +
                                      "' (expected WxH with positive integers)");
      }
      continue;
    }
    if (!argument.empty() && argument.front() == '-') {
      return FailUsage(argv[0], "unknown option '" + std::string(argument) + "'");
    }
    // The only remaining positional argument is the optional page URL.
    if (has_page_url) {
      return FailUsage(argv[0], "at most one page URL may be given");
    }
    has_page_url = true;
    if (argument.empty()) {
      return FailUsage(argv[0], "the page URL must not be empty");
    }
    options.page_url = std::string(argument);
  }

  const bool has_screenshot = !options.screenshot_path.empty();
  const bool has_dump_body = !options.dump_body_url.empty();
  const bool has_dump_dom = !options.dump_dom_url.empty();
  const bool has_dump_style = !options.dump_style_url.empty();
  const bool has_dump_boxes = !options.dump_boxes_url.empty();
  const int headless_modes = static_cast<int>(has_screenshot) + static_cast<int>(has_dump_body) +
                             static_cast<int>(has_dump_dom) + static_cast<int>(has_dump_style) +
                             static_cast<int>(has_dump_boxes);
  if (headless_modes > 1) {
    return FailUsage(argv[0],
                     "--screenshot, --dump-body, --dump-dom, --dump-style and --dump-boxes are "
                     "mutually exclusive");
  }
  if (headless && headless_modes == 0) {
    return FailUsage(argv[0],
                     "--headless requires --screenshot, --dump-body, --dump-dom, --dump-style or "
                     "--dump-boxes");
  }
  // The dump modes take their URL as the flag's own argument, so a positional URL
  // would be a second, ambiguous page source.
  if (has_page_url && (has_dump_body || has_dump_dom || has_dump_style || has_dump_boxes)) {
    return FailUsage(argv[0],
                     "a page URL cannot be combined with a --dump-* mode; pass the URL to the "
                     "flag instead");
  }
  // A font selects layout metrics and painting, so exactly one selection is allowed
  // and only the modes that lay the page out can use it. --dump-boxes lays out, so
  // it accepts the flags; --dump-body/--dump-dom/--dump-style never do.
  const bool has_font = !options.font_path.empty();
  const bool has_font_family = !options.font_family.empty();
  if (has_font && has_font_family) {
    return FailUsage(argv[0], "--font and --font-family are mutually exclusive");
  }
  if ((has_font || has_font_family) && (has_dump_body || has_dump_dom || has_dump_style)) {
    return FailUsage(argv[0],
                     "--font and --font-family cannot be combined with --dump-body, --dump-dom "
                     "or --dump-style");
  }

  return toy::shell::Run(options);
}
