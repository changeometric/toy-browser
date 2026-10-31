// M10 automatic font selection tests: toy::shell::Fonts resolves the computed
// CSS font family/weight/style and the page language through the generated
// deterministic fontconfig config (fixture config: the committed self-authored
// fonts are the only ones fontconfig can see). No display, no network and no
// installed font are needed.
//
// Mode "system" (the default): real-font selection and per-run coverage
// fallback for mixed Latin/Han text. Mode "bitmap" (run with the generated empty
// config): fontconfig finds no font, so the same object must degrade to the
// frozen built-in bitmap metrics and return no runs, while explicit FromFile
// selection still works.
//
// The fixture metrics are documented in tests/fixtures/fonts/README.md: at
// 16 px one glyph advances 9.6 px (regular), 10.4 px (bold), 9.92 px (italic)
// and 11.2 px (CJK); shaping may be one 26.6 unit off, so the checks use a small
// tolerance and also compare against the committed faces' own shaped widths.

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "support/check.h"
#include "toy/layout/box.h"
#include "toy/render/font.h"
#include "toy/shell/fonts.h"
#include "toy/style/computed_style.h"

namespace {

using toy::layout::TextMetrics;
using toy::paint::FontRun;
using toy::render::Font;
using toy::shell::Fonts;
using toy::style::ComputedStyle;
using toy::style::FontStyle;

constexpr float kFontSizePx = 16.0f;
// Shaping uses 26.6 fixed point; one unit is 1/64 px. The tolerance is below a
// whole device pixel but above the fixed-point rounding of the documented
// advances.
constexpr float kTolerancePx = 0.05f;

// The mixed Latin/Han string used by the run-segmentation checks: "Hello 中文".
// Plain UTF-8 literals keep the expected text readable.
constexpr std::string_view kMixedText = "Hello \u4e2d\u6587";
constexpr std::string_view kLatinPart = "Hello ";
constexpr std::string_view kHanPart = "\u4e2d\u6587";

bool NearlyEqual(float actual, float expected) {
  return std::fabs(actual - expected) <= kTolerancePx;
}

// True when two faces shape `sample` to the same advance width: the fixture
// faces are distinguishable by one glyph (regular 600, bold 650, italic 620,
// CJK 700 units per em).
bool SameShapedAdvance(const std::shared_ptr<const Font>& a, const std::shared_ptr<const Font>& b,
                       std::string_view sample) {
  return std::fabs(a->MeasureTextWidth(sample, kFontSizePx) -
                   b->MeasureTextWidth(sample, kFontSizePx)) <= 0.001f;
}

std::shared_ptr<const Font> LoadFixtureFont(const std::string& directory, const std::string& file) {
  std::string error;
  const std::shared_ptr<const Font> font = Font::LoadFromFile(directory + "/" + file, &error);
  if (!font) {
    std::fprintf(stderr, "font selection test: cannot load %s/%s: %s\n", directory.c_str(),
                 file.c_str(), error.c_str());
  }
  return font;
}

// Concatenates the run texts: the provider contract requires the runs to cover
// the box text exactly, in order.
std::string ConcatenateRuns(const std::vector<FontRun>& runs) {
  std::string text;
  for (const FontRun& run : runs) {
    text.append(run.text);
  }
  return text;
}

float SumRunAdvances(const std::vector<FontRun>& runs) {
  float advance_px = 0.0f;
  for (const FontRun& run : runs) {
    advance_px += run.font->MeasureTextWidth(run.text, kFontSizePx);
  }
  return advance_px;
}

// Tests the automatic system-font mode with the fixture config.
void TestSystemFonts(const std::string& fixture_directory,
                     const std::shared_ptr<const Font>& regular,
                     const std::shared_ptr<const Font>& bold,
                     const std::shared_ptr<const Font>& italic,
                     const std::shared_ptr<const Font>& cjk) {
  const std::shared_ptr<const Fonts> fonts = Fonts::SystemDefault();
  toy_test::Check(fonts != nullptr, "SystemDefault never returns null");
  if (!fonts) {
    return;
  }

  // --- The default computed style is the inherited "serif" family ------------
  const ComputedStyle base;
  toy_test::Check(base.font_family == std::vector<std::string>{"serif"},
                  "the initial computed font-family is serif");
  const TextMetrics regular_metrics = fonts->MeasureText("A", base, "");
  toy_test::Check(NearlyEqual(regular_metrics.advance_px, 9.6f),
                  "default serif resolves to the regular fixture face (0.6 em advance)");
  toy_test::Check(SameShapedAdvance(fonts->Runs("A", base, "").front().font, regular, "A"),
                  "the default serif run uses the committed regular fixture face");
  toy_test::Check(NearlyEqual(regular_metrics.line_height_px, 16.0f),
                  "the fixture line height is one em at 16 px");
  toy_test::Check(NearlyEqual(regular_metrics.ascent_px, 12.8f),
                  "the fixture ascent is 0.8 em at 16 px");

  // Empty text reports the face metrics layout needs for empty lines: advance 0
  // with the line height and ascent of the style's font.
  const TextMetrics empty_metrics = fonts->MeasureText("", base, "");
  toy_test::Check(empty_metrics.advance_px == 0.0f,
                  "empty text has zero advance in system-font mode");
  toy_test::Check(NearlyEqual(empty_metrics.line_height_px, 16.0f) &&
                      NearlyEqual(empty_metrics.ascent_px, 12.8f),
                  "empty text reports the primary face's line height and ascent");
  toy_test::Check(fonts->Runs("", base, "").empty(), "empty text has no real-font runs");

  // --- CSS weight and style select the bold/italic fixture faces -------------
  ComputedStyle bold_style;
  bold_style.font_weight = 700;
  toy_test::Check(NearlyEqual(fonts->MeasureText("A", bold_style, "").advance_px, 10.4f),
                  "font-weight: 700 selects the bold fixture face (0.65 em advance)");
  toy_test::Check(SameShapedAdvance(fonts->Runs("A", bold_style, "").front().font, bold, "A"),
                  "the weight 700 run uses the committed bold fixture face");

  ComputedStyle italic_style;
  italic_style.font_style = FontStyle::kItalic;
  toy_test::Check(NearlyEqual(fonts->MeasureText("A", italic_style, "").advance_px, 9.92f),
                  "font-style: italic selects the italic fixture face (0.62 em advance)");
  toy_test::Check(SameShapedAdvance(fonts->Runs("A", italic_style, "").front().font, italic, "A"),
                  "the italic run uses the committed italic fixture face");

  // --- Generic families are mapped by the fixture config ---------------------
  for (const char* family : {"monospace", "sans-serif", "sans"}) {
    ComputedStyle generic_style;
    generic_style.font_family = {family};
    toy_test::Check(NearlyEqual(fonts->MeasureText("A", generic_style, "").advance_px, 9.6f),
                    "the generic family resolves to the regular fixture face");
  }

  // --- Explicit family names and unknown families ----------------------------
  ComputedStyle named_style;
  named_style.font_family = {"Toy Test Font"};
  toy_test::Check(NearlyEqual(fonts->MeasureText("A", named_style, "").advance_px, 9.6f),
                  "the explicit fixture family resolves to its regular face");

  // An unknown family is not an error: fontconfig returns its best match and the
  // selection still produces usable runs (the fixture set is deterministic).
  ComputedStyle unknown_style;
  unknown_style.font_family = {"No Such Fixture Family"};
  const std::vector<FontRun> unknown_runs = fonts->Runs("Hello", unknown_style, "");
  toy_test::Check(!unknown_runs.empty(), "an unknown family still yields real-font runs");
  toy_test::Check(
      !unknown_runs.empty() && fonts->MeasureText("Hello", unknown_style, "").advance_px > 0.0f,
      "an unknown family still has positive metrics");

  // --- Per-run character-coverage fallback for mixed Latin/Han text ----------
  const std::vector<FontRun> runs = fonts->Runs(kMixedText, base, "");
  toy_test::Check(runs.size() == 2, "mixed Latin/Han text splits into two coverage runs");
  if (runs.size() == 2) {
    toy_test::Check(runs[0].text == kLatinPart,
                    "the first run is the Latin text plus the covered space");
    toy_test::Check(runs[1].text == kHanPart, "the second run is the uncovered Han text");
    toy_test::Check(SameShapedAdvance(runs[0].font, regular, "A"),
                    "the Latin run uses the preferred regular fixture face");
    toy_test::Check(SameShapedAdvance(runs[1].font, cjk, "\u4e2d"),
                    "the Han run falls back to the Toy CJK fixture face");
    toy_test::Check(ConcatenateRuns(runs) == kMixedText,
                    "the runs concatenate exactly to the input text");
    const TextMetrics mixed_metrics = fonts->MeasureText(kMixedText, base, "");
    toy_test::Check(NearlyEqual(mixed_metrics.advance_px, SumRunAdvances(runs)),
                    "measurement equals the sum of the run advances");
    toy_test::Check(NearlyEqual(mixed_metrics.advance_px, 79.96875f),
                    "mixed text measures 79.96875 px at 16 px");
  }

  // English+Chinese mixed text must go through the character-coverage fallback
  // with no .notdef gap: each Han code point resolves to a face that shapes it
  // with the 0.7 em CJK advance (11.203125 px at 16 px), never to the Latin
  // face's .notdef advance (9.59375 px). This is the required Chinese scope; other
  // scripts are best-effort through the same generic fontconfig coverage.
  for (const char* han_character : {"\u4e2d", "\u6587"}) {
    const std::vector<FontRun> character_runs = fonts->Runs(han_character, base, "");
    toy_test::Check(character_runs.size() == 1,
                    "a single Han code point resolves to exactly one run");
    if (character_runs.size() == 1) {
      toy_test::Check(SameShapedAdvance(character_runs[0].font, cjk, "\u4e2d"),
                      "the Han run uses the fallback CJK face, not a .notdef gap");
      toy_test::Check(
          NearlyEqual(character_runs[0].font->MeasureTextWidth(han_character, kFontSizePx), 11.2f),
          "the Han code point has the CJK 0.7 em advance (no .notdef width)");
    }
  }

  // The Han-only text box needs the fallback even for a single word, and the
  // CJK face has its own 0.7 em advance.
  const std::vector<FontRun> han_runs = fonts->Runs(kHanPart, base, "");
  toy_test::Check(han_runs.size() == 1 && han_runs[0].text == kHanPart,
                  "Han-only text is one run in the fallback face");
  toy_test::Check(NearlyEqual(fonts->MeasureText(kHanPart, base, "").advance_px, 22.40625f),
                  "two Han glyphs advance 22.40625 px at 16 px");

  // --- The page language hint is a preference, not a filter ------------------
  // The fixture fonts carry no language metadata, so FC_LANG must not drop them:
  // segmentation and metrics stay identical to the no-language result.
  const std::vector<FontRun> zh_runs = fonts->Runs(kMixedText, base, "zh-CN");
  toy_test::Check(zh_runs.size() == runs.size(),
                  "a zh-CN language hint keeps the mixed-text coverage runs");
  toy_test::Check(NearlyEqual(fonts->MeasureText(kMixedText, base, "zh-CN").advance_px, 79.96875f),
                  "a zh-CN language hint does not break matching or metrics");
  toy_test::Check(fonts->Runs("Hello", base, "en").size() == 1,
                  "an unrelated language hint still resolves the regular face");

  // --- FromFile: one exact face, no character fallback -----------------------
  std::string error;
  const std::shared_ptr<const Fonts> single =
      Fonts::FromFile(fixture_directory + "/toy_test.ttf", &error);
  toy_test::Check(single != nullptr, "FromFile loads the committed fixture font");
  toy_test::Check(error.empty(), "a successful FromFile clears the error");
  if (single) {
    const std::vector<FontRun> single_runs = single->Runs(kMixedText, base, "");
    toy_test::Check(single_runs.size() == 1 && single_runs[0].text == kMixedText,
                    "FromFile paints the whole text with the one selected face");
    toy_test::Check(NearlyEqual(single->MeasureText(kMixedText, base, "").advance_px,
                                regular->MeasureTextWidth(kMixedText, kFontSizePx)),
                    "FromFile metrics come from the one selected face (Han renders .notdef)");
  }

  // --- FromFamily: explicit family with installed-font character fallback -----
  const std::shared_ptr<const Fonts> family = Fonts::FromFamily("Toy Test Font", &error);
  toy_test::Check(family != nullptr, "FromFamily resolves an installed fixture family");
  toy_test::Check(error.empty(), "a successful FromFamily clears the error");
  if (family) {
    toy_test::Check(NearlyEqual(family->MeasureText("A", base, "").advance_px, 9.6f),
                    "FromFamily prefers the requested regular face for Latin text");
    const std::vector<FontRun> family_runs = family->Runs(kMixedText, base, "");
    toy_test::Check(
        family_runs.size() == 2 && SameShapedAdvance(family_runs[1].font, cjk, "\u4e2d"),
        "FromFamily still falls back by character coverage for Han text");
  }
  const std::shared_ptr<const Fonts> generic_family = Fonts::FromFamily("sans-serif", &error);
  toy_test::Check(generic_family != nullptr &&
                      NearlyEqual(generic_family->MeasureText("A", base, "").advance_px, 9.6f),
                  "FromFamily accepts a generic family name");

  // A file that cannot be loaded is an ordinary error with a readable message.
  error.clear();
  const std::shared_ptr<const Fonts> missing =
      Fonts::FromFile(fixture_directory + "/missing.ttf", &error);
  toy_test::Check(missing == nullptr, "FromFile returns null for a missing file");
  toy_test::Check(!error.empty(), "a failed FromFile sets a readable error");
}

// Tests the bitmap degradation: with the generated empty fontconfig config no
// font is available, so the automatic selection must fall back to the built-in
// bitmap metrics and runs while explicit file selection keeps working.
void TestBitmapFallback(const std::string& fixture_directory) {
  const std::shared_ptr<const Fonts> fonts = Fonts::SystemDefault();
  toy_test::Check(fonts != nullptr, "SystemDefault returns the bitmap-mode object");
  if (!fonts) {
    return;
  }
  const ComputedStyle base;
  // The built-in bitmap font advances one 8/16 em cell per code point and one
  // cell of line height, with ascent == line height (its cells are top-aligned).
  const TextMetrics metrics = fonts->MeasureText("AB", base, "");
  toy_test::Check(NearlyEqual(metrics.advance_px, 16.0f),
                  "bitmap mode uses the built-in 8 px advance per code point");
  toy_test::Check(
      NearlyEqual(metrics.line_height_px, 16.0f) && NearlyEqual(metrics.ascent_px, 16.0f),
      "bitmap mode reports the built-in line height as its ascent");
  toy_test::Check(fonts->Runs("Hello", base, "").empty(),
                  "bitmap mode returns no real-font runs (paint keeps the bitmap path)");
  toy_test::Check(fonts->Runs(kHanPart, base, "").empty(),
                  "bitmap mode has no Han fallback either");

  // An explicit family that fontconfig cannot resolve is a reported error, never
  // a silent bitmap fallback.
  std::string error;
  const std::shared_ptr<const Fonts> family = Fonts::FromFamily("serif", &error);
  toy_test::Check(family == nullptr, "FromFamily fails when fontconfig has no font");
  toy_test::Check(!error.empty(), "the failed FromFamily sets a readable error");

  // Explicit file selection does not depend on fontconfig at all.
  const std::shared_ptr<const Fonts> single =
      Fonts::FromFile(fixture_directory + "/toy_test.ttf", &error);
  toy_test::Check(single != nullptr, "FromFile still works in bitmap mode");
  if (single) {
    const std::vector<FontRun> runs = single->Runs("Hello", base, "");
    toy_test::Check(runs.size() == 1 && runs[0].text == "Hello",
                    "FromFile returns one run per text box in bitmap mode");
    toy_test::Check(NearlyEqual(single->MeasureText("A", base, "").advance_px, 9.6f),
                    "FromFile keeps the real face metrics in bitmap mode");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr, "usage: %s <fonts-fixture-directory> [bitmap]\n", argv[0]);
    return 1;
  }
  const std::string fixture_directory = argv[1];
  const bool bitmap_mode = argc == 3 && std::string_view(argv[2]) == "bitmap";

  toy_test::Check(LoadFixtureFont(fixture_directory, "toy_test.ttf") != nullptr,
                  "the regular fixture font loads");
  toy_test::Check(LoadFixtureFont(fixture_directory, "toy_cjk.ttf") != nullptr,
                  "the CJK fixture font loads");
  if (bitmap_mode) {
    TestBitmapFallback(fixture_directory);
    return toy_test::Finish("font selection (bitmap)");
  }

  const std::shared_ptr<const Font> regular = LoadFixtureFont(fixture_directory, "toy_test.ttf");
  const std::shared_ptr<const Font> bold = LoadFixtureFont(fixture_directory, "toy_test_bold.ttf");
  const std::shared_ptr<const Font> italic =
      LoadFixtureFont(fixture_directory, "toy_test_italic.ttf");
  const std::shared_ptr<const Font> cjk = LoadFixtureFont(fixture_directory, "toy_cjk.ttf");
  if (!regular || !bold || !italic || !cjk) {
    return 1;
  }
  TestSystemFonts(fixture_directory, regular, bold, italic, cjk);
  return toy_test::Finish("font selection");
}
