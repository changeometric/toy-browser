// The toy browser's entry point.
//
// The project is at milestone M0: this executable only reports its build
// identity and the planned command-line shape. Rendering, networking and page
// loading arrive in later milestones (see docs/roadmap.md).

#include <cstdio>
#include <string_view>

namespace {

constexpr std::string_view kProgramName = "toybrowser";

void PrintUsage(const char* argv0) {
  std::printf(
      "Usage: %s [options]\n"
      "\n"
      "A toy web browser built from scratch (milestone M0: no page loading yet).\n"
      "\n"
      "Options:\n"
      "  -h, --help     Show this help and exit.\n"
      "  -v, --version  Show the version and exit.\n",
      argv0);
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument = argv[i];
    if (argument == "-h" || argument == "--help") {
      PrintUsage(argv[0]);
      return 0;
    }
    if (argument == "-v" || argument == "--version") {
      std::printf("%s %s\n", kProgramName.data(), TOYBROWSER_VERSION);
      return 0;
    }
    std::fprintf(stderr, "%s: unknown option '%s'\n\n", kProgramName.data(), argv[i]);
    PrintUsage(argv[0]);
    return 2;
  }

  std::printf("%s %s: milestone M0 (design and skeleton), no page loading yet.\n",
              kProgramName.data(), TOYBROWSER_VERSION);
  return 0;
}
