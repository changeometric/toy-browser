// Regression test: out-of-range window sizes are rejected with a usage error
// instead of aborting inside the renderer (std::length_error).

#include <cstdio>
#include <string>

#include "toy/shell/application.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "size limit test failed: %s\n", description);
    ++g_failures;
  }
}

toy::shell::Options OptionsFor(int width, int height, const std::string& screenshot_path) {
  toy::shell::Options options;
  options.width = width;
  options.height = height;
  options.screenshot_path = screenshot_path;
  return options;
}

}  // namespace

int main() {
  // These sizes used to abort with std::length_error before the fix.
  Check(toy::shell::Run(OptionsFor(2000000000, 2000000000, "size_limit_huge.ppm")) == 2,
        "a huge size is a usage error");
  Check(toy::shell::Run(OptionsFor(8193, 1, "size_limit_side.ppm")) == 2,
        "a side above the per-side limit is a usage error");
  Check(toy::shell::Run(OptionsFor(8192, 2049, "size_limit_area.ppm")) == 2,
        "a size above the pixel cap is a usage error");

  // A valid small size must still render, so the limit does not reject valid input.
  const char* const kValidPath = "size_limit_valid.ppm";
  Check(toy::shell::Run(OptionsFor(1, 1, kValidPath)) == 0, "a 1x1 screenshot still succeeds");
  if (std::remove(kValidPath) != 0) {
    Check(false, "the valid screenshot file is removable");
  }

  if (g_failures != 0) {
    std::fprintf(stderr, "size limit test: %d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("size limit test passed: out-of-range sizes rejected, 1x1 accepted\n");
  return 0;
}
