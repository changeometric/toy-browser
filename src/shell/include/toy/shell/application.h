#pragma once

#include <string>

namespace toy::shell {

// Configuration for one run of the browser.
struct Options {
  int width = 800;
  int height = 600;
  std::string title = "toybrowser";
  // The headless modes below are mutually exclusive: the CLI rejects combining
  // them, and Run reports a usage error (2) for contradictory options.
  // When non-empty, render one frame to this PPM file instead of opening a window.
  // The headless path needs no display.
  std::string screenshot_path;
  // Optional page URL, used by window mode and by --screenshot. When non-empty,
  // Run fetches the page and renders it; when empty, both modes show the M1
  // placeholder page. The --dump-* modes pass their URL through their own field
  // and the CLI rejects combining them with this one.
  std::string page_url;
  // When non-empty, fetch this URL and write the raw response body to stdout.
  // No window is opened, so no display is needed.
  std::string dump_body_url;
  // When non-empty, fetch this URL, parse it as HTML and write the DOM dump to
  // stdout. Parsing never fails; only URL and fetch errors are reported.
  std::string dump_dom_url;
  // When non-empty, fetch this URL, parse it as HTML, compute styles and write
  // the computed style tree to stdout. Parsing never fails; only URL and fetch
  // errors are reported.
  std::string dump_style_url;
  // When non-empty, fetch this URL, parse it as HTML, compute styles, lay the
  // page out at the window width and write the box tree to stdout. Parsing and
  // layout never fail; only URL and fetch errors are reported.
  std::string dump_boxes_url;
  // Optional real-font selection for the layout/painting modes (window mode,
  // --screenshot and --dump-boxes). `font_path` loads that font file directly;
  // `font_family` asks fontconfig for the best installed match. At most one may be
  // non-empty; the CLI rejects both together with each other and with
  // --dump-body/--dump-dom/--dump-style, and empty strings mean the built-in
  // bitmap font (the pre-M8b behavior). A font that cannot be loaded is a runtime
  // error (exit 1).
  std::string font_path;
  std::string font_family;
};

// Runs the browser.
//
// Returns 0 on success, 1 on a runtime error (cannot open a window, cannot write
// the screenshot) and 2 on a usage error (invalid options, including a window
// size outside the supported range).
int Run(const Options& options);

}  // namespace toy::shell
