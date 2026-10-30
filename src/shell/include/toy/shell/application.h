#pragma once

#include <string>

namespace toy::shell {

// Configuration for one run of the browser.
struct Options {
  int width = 800;
  int height = 600;
  std::string title = "toybrowser";
  // When non-empty, render one frame to this PPM file instead of opening a window.
  // The headless path needs no display.
  std::string screenshot_path;
};

// Runs the browser.
//
// Returns 0 on success, 1 on a runtime error (cannot open a window, cannot write
// the screenshot) and 2 on a usage error (invalid options).
int Run(const Options& options);

}  // namespace toy::shell
