// The toy browser's command-line entry point.
//
// Parses the options, then hands them to toy::shell::Run. Page loading arrives
// in later milestones (see docs/roadmap.md).

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
      "Usage: %s [options]\n"
      "\n"
      "A toy web browser built from scratch. Page loading arrives in later milestones.\n"
      "\n"
      "Options:\n"
      "  -h, --help         Show this help and exit.\n"
      "  -v, --version      Show the version and exit.\n"
      "  --headless         No window; requires --screenshot, --dump-body or --dump-dom.\n"
      "  --screenshot PATH  Write one frame to PATH as a PPM image; implies --headless.\n"
      "  --dump-body URL    Fetch URL and write the raw response body to stdout; no window.\n"
      "  --dump-dom URL     Fetch URL as HTML and write the DOM dump to stdout; no window.\n"
      "  --size WxH         Window size in pixels (default 800x600).\n",
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
    return FailUsage(argv[0], "unknown option '" + std::string(argument) + "'");
  }

  const bool has_screenshot = !options.screenshot_path.empty();
  const bool has_dump_body = !options.dump_body_url.empty();
  const bool has_dump_dom = !options.dump_dom_url.empty();
  const int headless_modes = static_cast<int>(has_screenshot) + static_cast<int>(has_dump_body) +
                             static_cast<int>(has_dump_dom);
  if (headless_modes > 1) {
    return FailUsage(argv[0], "--screenshot, --dump-body and --dump-dom are mutually exclusive");
  }
  if (headless && headless_modes == 0) {
    return FailUsage(argv[0], "--headless requires --screenshot, --dump-body or --dump-dom");
  }

  return toy::shell::Run(options);
}
