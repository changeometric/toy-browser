#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "toy/layout/box.h"
#include "toy/paint/paint.h"
#include "toy/style/computed_style.h"

namespace toy::shell {

// Per-session font selection for layout metrics and painting. The shell owns one
// Fonts object and shares it between the layout measurement adapter and the
// paint::FontRunsProvider, so the font chosen for a text box is identical in
// layout and painting.
//
// Modes:
// - SystemDefault(): normal browser behavior, no CLI flags needed. The computed
//   CSS font-family list, font-weight and font-style are matched through the
//   approved fontconfig library, and text is split into runs by character
//   coverage so scripts missing from the preferred font (for example Han text on
//   a Latin page) fall back to an installed font that has those characters. The
//   page language hint (the <html lang> attribute value, or empty) is passed to
//   fontconfig so language-specific faces are preferred where fontconfig
//   distinguishes them.
// - FromFile(path): one exact face for every text (--font PATH). There is no
//   character fallback: code points the face does not map render its .notdef.
// - FromFamily(family): the family is the preferred family for every text
//   (--font-family NAME); character fallback still searches installed fonts.
//
// When fontconfig cannot provide any usable font (initialization failure, no
// matching font, every candidate fails to load), the object operates in bitmap
// mode: MeasureText returns the built-in bitmap metrics and Runs returns empty, so
// layout and painting use the built-in 8x16 bitmap path. A page load never fails
// because system fonts are missing; only explicit file/family selection can fail.
//
// Threading/lifetime: single-threaded like the rest of the MVP, immutable after
// creation, shared by every page load, relayout and paint of one session. Results
// are deterministic for a fixed fontconfig configuration and font set (tests set
// FONTCONFIG_FILE before the process starts; the fontconfig configuration is
// initialized once per process). Since M11 the object keeps private, bounded
// caches (resolved styles, text metrics and ordered runs) that make repeated
// measure/paint calls cheap; a cache hit returns exactly what a fresh computation
// would, a cache allocation failure is treated as a miss, and the caches only
// change speed, never results.
class Fonts {
 public:
  // Normal browser mode: fontconfig matching from the computed styles, degrading
  // to the bitmap font when no usable system font is available. Never returns
  // null.
  static std::shared_ptr<const Fonts> SystemDefault();

  // One explicit font file (--font). Returns null with a readable *error when the
  // file cannot be loaded; the caller reports it as a runtime error.
  static std::shared_ptr<const Fonts> FromFile(std::string_view path, std::string* error);

  // One explicitly requested font family (--font-family), with character fallback
  // among installed fonts. Returns null with a readable *error when fontconfig
  // cannot provide a usable font for the family.
  static std::shared_ptr<const Fonts> FromFamily(std::string_view family, std::string* error);

  ~Fonts();
  Fonts(const Fonts&) = delete;
  Fonts& operator=(const Fonts&) = delete;

  // Metrics for layout: the sum of the selected runs' advance widths, the maximum
  // line height and the maximum ascent (CSS px). In bitmap mode this is the
  // built-in bitmap font's metrics. `language` is the page language hint and may
  // be empty.
  layout::TextMetrics MeasureText(std::string_view text_utf8, const style::ComputedStyle& style,
                                  std::string_view language) const;

  // The ordered, non-empty real-font runs for a text box, ready for
  // paint::FontRunsProvider, or empty when the bitmap font applies. Runs
  // concatenate exactly to `text_utf8`. `language` is the page language hint and
  // may be empty.
  std::vector<paint::FontRun> Runs(std::string_view text_utf8, const style::ComputedStyle& style,
                                   std::string_view language) const;

 private:
  Fonts();
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace toy::shell
