// Window creation must fail cleanly when no display is available.
//
// The test points DISPLAY at nothing, so it never needs an X server and gives
// the same result on every machine.

#include "toy/platform/window.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

int main() {
  if (setenv("DISPLAY", "", 1) != 0) {
    std::fprintf(stderr, "window_test: setenv(DISPLAY) failed\n");
    return 1;
  }

  std::string error;
  const std::unique_ptr<toy::platform::Window> window =
      toy::platform::Window::Create(64, 48, "toybrowser test", &error);

  if (window != nullptr) {
    std::fprintf(stderr, "window_test: window creation succeeded without a display\n");
    return 1;
  }
  if (error.empty()) {
    std::fprintf(stderr, "window_test: failure did not report an error message\n");
    return 1;
  }

  std::printf("window_test: creation failed cleanly: %s\n", error.c_str());
  return 0;
}
