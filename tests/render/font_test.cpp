// Tests for toy::render font metrics: the monospace 8x16 bitmap font at a
// 16 CSS px em, lenient UTF-8 decoding and line heights.

#include "toy/render/font.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "support/check.h"

namespace {

using toy::render::kFontDesignSizePx;
using toy::render::kGlyphAdvanceUnits;
using toy::render::kLineHeightUnits;
using toy::render::LineHeightPx;
using toy::render::MeasureTextWidth;

constexpr float kTolerance = 1e-4f;

// All expected values below are exactly representable; the tolerance only keeps
// the checks from depending on a particular summation order.
bool NearlyEqual(float lhs, float rhs) {
  const float difference = lhs > rhs ? lhs - rhs : rhs - lhs;
  return difference <= kTolerance;
}

void CheckWidth(std::string_view text, float font_size_px, float expected,
                const char* description) {
  toy_test::Check(NearlyEqual(MeasureTextWidth(text, font_size_px), expected), description);
}

void TestConstants() {
  toy_test::Check(kFontDesignSizePx == 16.0f, "design size is a 16 px em");
  toy_test::Check(kGlyphAdvanceUnits == 8, "glyph advance is 8 design units");
  toy_test::Check(kLineHeightUnits == 16, "line height is 16 design units");
  CheckWidth("H", kFontDesignSizePx, static_cast<float>(kGlyphAdvanceUnits),
             "one cell is 8 px at the design size");
  toy_test::Check(LineHeightPx(kFontDesignSizePx) == kFontDesignSizePx,
                  "line height is one em at the design size");
}

void TestEmptyText() {
  CheckWidth("", 16.0f, 0.0f, "empty text has zero width at 16 px");
  CheckWidth("", 32.0f, 0.0f, "empty text has zero width at 32 px");
  CheckWidth("", 0.0f, 0.0f, "empty text has zero width at a zero font size");
}

void TestAsciiWidths() {
  CheckWidth("Hello world", 16.0f, 88.0f, "\"Hello world\" is 88 px at 16 px");
  CheckWidth("Hello world", 32.0f, 176.0f, "\"Hello world\" is 176 px at 32 px");
  CheckWidth("Hello", 16.0f, 40.0f, "\"Hello\" is 40 px at 16 px");
  CheckWidth("H", 16.0f, 8.0f, "a single ASCII letter is one cell");
  CheckWidth("Hello world", 8.0f, 44.0f, "width scales linearly with the font size");
}

void TestOneCellPerCharacter() {
  // Every printable ASCII character advances exactly one cell.
  bool one_cell_each = true;
  std::string printable_ascii;
  for (char character = '!'; character <= '~'; ++character) {
    printable_ascii.push_back(character);
    one_cell_each = one_cell_each && MeasureTextWidth(std::string(1, character), 16.0f) == 8.0f;
  }
  toy_test::Check(one_cell_each, "every printable ASCII character is one 8 px cell at 16 px");
  CheckWidth(printable_ascii, 16.0f, static_cast<float>(printable_ascii.size()) * 8.0f,
             "a printable ASCII string is one cell per character");
  CheckWidth(printable_ascii, 32.0f, static_cast<float>(printable_ascii.size()) * 16.0f,
             "the same string is one 16 px cell per character at 32 px");
}

void TestLineHeights() {
  toy_test::Check(LineHeightPx(16.0f) == 16.0f, "line height at 16 px is 16 px");
  toy_test::Check(LineHeightPx(32.0f) == 32.0f, "line height at 32 px is 32 px");
  toy_test::Check(LineHeightPx(12.5f) == 12.5f, "line height is one em for a fractional size");
  toy_test::Check(LineHeightPx(0.0f) == 0.0f, "zero font size has zero line height");
  toy_test::Check(LineHeightPx(-16.0f) == 0.0f, "negative font size has zero line height");
}

void TestZeroAndNegativeFontSizes() {
  CheckWidth("Hello", 0.0f, 0.0f, "zero font size has zero width");
  CheckWidth("Hello", -16.0f, 0.0f, "negative font size has zero width");
  CheckWidth("a\xFFz", 0.0f, 0.0f, "zero font size has zero width with malformed text");
}

void TestUtf8CodePointCount() {
  // Two-, three- and four-byte sequences each advance one cell.
  CheckWidth("\xC3\xA9", 16.0f, 8.0f, "U+00E9 (two-byte UTF-8) is one cell");
  CheckWidth("\xE2\x82\xAC", 16.0f, 8.0f, "U+20AC (three-byte UTF-8) is one cell");
  CheckWidth("\xF0\x9F\x98\x80", 16.0f, 8.0f, "U+1F600 (four-byte UTF-8) is one cell");
  // U+00E9, U+20AC and U+1F600 after one ASCII letter: four code points.
  CheckWidth("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", 16.0f, 32.0f,
             "mixed one-, two-, three- and four-byte sequences count one cell each");
  // Three three-byte code points.
  CheckWidth("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 16.0f, 24.0f,
             "three multi-byte code points are three cells");
}

void TestMalformedUtf8() {
  CheckWidth("\xFF", 16.0f, 8.0f, "a lone invalid byte is one cell");
  CheckWidth("\x80", 16.0f, 8.0f, "a stray continuation byte is one cell");
  CheckWidth("a\xFFz", 16.0f, 24.0f, "an invalid byte between ASCII characters is one cell");
  // An incomplete sequence cannot be decoded: each of its bytes counts once.
  CheckWidth("\xE2\x82", 16.0f, 16.0f, "the two bytes of an incomplete sequence are two cells");
  CheckWidth("\xC3", 16.0f, 8.0f, "a lone sequence lead byte is one cell");
}

}  // namespace

int main() {
  TestConstants();
  TestEmptyText();
  TestAsciiWidths();
  TestOneCellPerCharacter();
  TestLineHeights();
  TestZeroAndNegativeFontSizes();
  TestUtf8CodePointCount();
  TestMalformedUtf8();
  return toy_test::Finish("render.font_metrics");
}
