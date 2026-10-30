#pragma once

#include <string>

namespace toy::shell {

// Configuration for one run of the browser.
struct Options {
  int width = 800;
  int height = 600;
  std::string title = "toybrowser";
  // The two headless modes below are mutually exclusive: the CLI rejects
  // combining them, and Run reports a usage error (2) for contradictory options.
  // When non-empty, render one frame to this PPM file instead of opening a window.
  // The headless path needs no display.
  std::string screenshot_path;
  // When non-empty, fetch this URL and write the raw response body to stdout.
  // No window is opened, so no display is needed.
  std::string dump_body_url;
};

// Runs the browser.
//
// Returns 0 on success, 1 on a runtime error (cannot open a window, cannot write
// the screenshot) and 2 on a usage error (invalid options).
int Run(const Options& options);

}  // namespace toy::shell
