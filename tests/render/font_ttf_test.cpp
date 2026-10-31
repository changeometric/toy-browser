// Tests for toy::render::Font: loading (file and memory), failure handling,
// design-unit metrics and harfbuzz-shaped advances for the committed
// tests/fixtures/fonts/toy_test.ttf. The fixture directory is argv[1].

#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/check.h"
#include "toy/render/font.h"

namespace {

using toy::render::Font;
using toy::render::kMaxRealFontSizePx;

// Shaped advances are reported in 26.6 fixed point, so a design advance can be
// one 1/64 px unit off. All metric assertions use this tolerance; anything
// larger is a real metric bug.
constexpr float kEpsilon = 1e-3f;

bool NearlyEqual(float actual, float expected) { return std::fabs(actual - expected) <= kEpsilon; }

std::optional<std::string> ReadFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) {
    return std::nullopt;
  }
  return bytes;
}

float Infinity() { return std::numeric_limits<float>::infinity(); }
float NotANumber() { return std::numeric_limits<float>::quiet_NaN(); }

void TestLoadFromFile(const std::string& directory) {
  std::string error;
  const std::shared_ptr<const Font> font = Font::LoadFromFile(directory + "/toy_test.ttf", &error);
  toy_test::Check(font != nullptr, "toy_test.ttf loads through Font::LoadFromFile");
  toy_test::Check(error.empty(), "a successful file load leaves the error untouched");
  if (font == nullptr) {
    return;
  }
  toy_test::Check(NearlyEqual(font->AscentPx(16.0f), 12.8f),
                  "the fixture font's ascent is 800/1000 em");
}

void TestLoadFromMemoryCopiesBytes(const std::string& directory) {
  const std::optional<std::string> bytes = ReadFile(directory + "/toy_test.ttf");
  toy_test::Check(bytes.has_value(), "toy_test.ttf can be read for the memory-load test");
  if (!bytes.has_value()) {
    return;
  }
  std::string error;
  const std::shared_ptr<const Font> font = Font::LoadFromMemory(*bytes, &error);
  toy_test::Check(font != nullptr, "toy_test.ttf loads through Font::LoadFromMemory");
  toy_test::Check(error.empty(), "a successful memory load leaves the error untouched");
  if (font == nullptr) {
    return;
  }
  toy_test::Check(NearlyEqual(font->MeasureTextWidth("ABC", 16.0f), 28.78125f),
                  "a memory-loaded font shapes like a file-loaded font");

  // LoadFromMemory must copy the bytes: corrupting the caller's buffer after the
  // load may not disturb the font.
  std::string mutable_bytes = *bytes;
  const std::shared_ptr<const Font> copied = Font::LoadFromMemory(mutable_bytes, nullptr);
  toy_test::Check(copied != nullptr, "a memory load with a null error pointer succeeds");
  if (copied == nullptr) {
    return;
  }
  mutable_bytes.assign(mutable_bytes.size(), '\0');
  toy_test::Check(NearlyEqual(copied->MeasureTextWidth("A", 16.0f), 9.59375f),
                  "LoadFromMemory copies the font image, so the caller may release it");
}

void TestLoadFailures(const std::string& directory) {
  std::string error;
  toy_test::Check(Font::LoadFromFile(directory + "/definitely_missing.ttf", &error) == nullptr,
                  "a missing font file fails to load");
  toy_test::Check(!error.empty(), "a missing font file sets an error message");

  error.clear();
  toy_test::Check(Font::LoadFromFile("", &error) == nullptr, "an empty path fails to load");
  toy_test::Check(!error.empty(), "an empty path sets an error message");

  error.clear();
  toy_test::Check(Font::LoadFromMemory("this is not a font file", &error) == nullptr,
                  "garbage font bytes fail to load");
  toy_test::Check(!error.empty(), "garbage font bytes set an error message");

  error.clear();
  toy_test::Check(Font::LoadFromMemory(std::string_view(), &error) == nullptr,
                  "empty font bytes fail to load");
  toy_test::Check(!error.empty(), "empty font bytes set an error message");

  const std::optional<std::string> bytes = ReadFile(directory + "/toy_test.ttf");
  if (bytes.has_value()) {
    error.clear();
    toy_test::Check(
        Font::LoadFromMemory(std::string_view(*bytes).substr(0, 100), &error) == nullptr,
        "a truncated font fails to load");
    toy_test::Check(!error.empty(), "a truncated font sets an error message");
  }

  // A null error pointer is allowed for every outcome.
  toy_test::Check(Font::LoadFromMemory("garbage", nullptr) == nullptr,
                  "a failed memory load tolerates a null error");
  toy_test::Check(Font::LoadFromFile(directory + "/definitely_missing.ttf", nullptr) == nullptr,
                  "a failed file load tolerates a null error");
}

void TestLoadsNeverThrow(const std::string& directory) {
  bool threw = false;
  try {
    std::string error;
    (void)Font::LoadFromFile(directory + "/definitely_missing.ttf", &error);
    (void)Font::LoadFromFile("", &error);
    (void)Font::LoadFromMemory("garbage", &error);
    (void)Font::LoadFromMemory(std::string_view(), &error);
    (void)Font::LoadFromMemory(std::string(1024, '\0'), &error);
  } catch (...) {
    threw = true;
  }
  toy_test::Check(!threw, "font loading never throws, even for malformed input");
}

void TestMetrics(const Font& font) {
  toy_test::Check(NearlyEqual(font.AscentPx(16.0f), 12.8f),
                  "AscentPx(16) is 16 * 800 / 1000 = 12.8 px");
  toy_test::Check(NearlyEqual(font.LineHeightPx(16.0f), 16.0f),
                  "LineHeightPx(16) is 16 * 1000 / 1000 = 16 px");
  toy_test::Check(NearlyEqual(font.AscentPx(32.0f), 25.6f), "AscentPx(32) is 25.6 px");
  toy_test::Check(NearlyEqual(font.LineHeightPx(32.0f), 32.0f), "LineHeightPx(32) is 32 px");

  toy_test::Check(font.AscentPx(0.0f) == 0.0f && font.AscentPx(-1.0f) == 0.0f &&
                      font.AscentPx(Infinity()) == 0.0f && font.AscentPx(NotANumber()) == 0.0f,
                  "AscentPx is zero for non-positive and non-finite sizes");
  toy_test::Check(font.LineHeightPx(0.0f) == 0.0f && font.LineHeightPx(-1.0f) == 0.0f &&
                      font.LineHeightPx(Infinity()) == 0.0f &&
                      font.LineHeightPx(NotANumber()) == 0.0f,
                  "LineHeightPx is zero for non-positive and non-finite sizes");

  // Report-only metrics come from the design units, never from a shaped size.
  (void)font.MeasureTextWidth("A", 32.0f);
  toy_test::Check(NearlyEqual(font.AscentPx(16.0f), 12.8f),
                  "AscentPx does not depend on the last shaped size");
  toy_test::Check(NearlyEqual(font.LineHeightPx(16.0f), 16.0f),
                  "LineHeightPx does not depend on the last shaped size");
}

void TestMeasureTextWidth(const Font& font) {
  // The fixture is monospace: every glyph, including .notdef, advances 600
  // design units. Harfbuzz reports 614/64 px at 16 px (the design value 9.6 is
  // rounded into 26.6), so tests assert the shaped value, not 9.6.
  toy_test::Check(NearlyEqual(font.MeasureTextWidth("A", 16.0f), 9.59375f),
                  "MeasureTextWidth(\"A\", 16) is 614/64 px");
  toy_test::Check(NearlyEqual(font.MeasureTextWidth("AB", 16.0f), 19.1875f),
                  "MeasureTextWidth(\"AB\", 16) is 1228/64 px");
  toy_test::Check(NearlyEqual(font.MeasureTextWidth("ABC", 16.0f), 28.78125f),
                  "MeasureTextWidth(\"ABC\", 16) is 1842/64 px");
  toy_test::Check(NearlyEqual(font.MeasureTextWidth("A B", 16.0f), 28.78125f),
                  "the fixture space advances like a glyph (3 cells)");
  toy_test::Check(NearlyEqual(font.MeasureTextWidth(" ", 16.0f), 9.59375f),
                  "a lone space measures one cell");
  toy_test::Check(NearlyEqual(font.MeasureTextWidth(".", 16.0f), 9.59375f),
                  "punctuation measures one cell");
  toy_test::Check(NearlyEqual(font.MeasureTextWidth("abc", 16.0f), 28.78125f),
                  "lowercase letters measure one cell each");
  toy_test::Check(NearlyEqual(font.MeasureTextWidth("X", 16.0f), 9.59375f),
                  "a missing code point shapes to .notdef with the design advance");
  toy_test::Check(NearlyEqual(font.MeasureTextWidth("A", 32.0f), 19.203125f),
                  "MeasureTextWidth(\"A\", 32) uses the shaped 32 px advance");

  toy_test::Check(font.MeasureTextWidth("", 16.0f) == 0.0f, "empty text measures zero");
  toy_test::Check(font.MeasureTextWidth("A", 0.0f) == 0.0f, "size 0 measures zero");
  toy_test::Check(font.MeasureTextWidth("A", -16.0f) == 0.0f, "a negative size measures zero");
  toy_test::Check(font.MeasureTextWidth("A", Infinity()) == 0.0f, "an infinite size measures zero");
  toy_test::Check(font.MeasureTextWidth("A", NotANumber()) == 0.0f, "a NaN size measures zero");

  toy_test::Check(font.MeasureTextWidth("ABC", 16.0f) == font.MeasureTextWidth("ABC", 16.0f),
                  "repeated measurements are identical");
}

// The render-side shaped-run cache (M11) must be invisible in the numbers: a
// fresh Font, a cache-warm Font and the same Font after far more distinct
// (size, text) pairs than the documented budget all report bit-identical
// widths. Eviction may make a later call slower; it must never change a value.
void TestCacheDoesNotChangeMeasurements(const std::string& directory) {
  std::string error;
  const std::shared_ptr<const Font> warm = Font::LoadFromFile(directory + "/toy_test.ttf", &error);
  const std::shared_ptr<const Font> fresh = Font::LoadFromFile(directory + "/toy_test.ttf", &error);
  toy_test::Check(warm != nullptr && fresh != nullptr,
                  "the fixture font loads twice for the cache tests");
  if (warm == nullptr || fresh == nullptr) {
    return;
  }

  const char* const kTexts[] = {"A",          "ABC", "Hello, World!",           "aa bb cc",
                                "0123456789", "?",   "\xe4\xbd\xa0\xe5\xa5\xbd"};
  const float kSizes[] = {1.0f,  8.0f,  11.0f, 12.5f,  13.3333f,          16.0f,
                          16.5f, 32.0f, 64.0f, 123.5f, kMaxRealFontSizePx};
  std::vector<float> baseline;
  for (const char* text : kTexts) {
    for (float size : kSizes) {
      baseline.push_back(warm->MeasureTextWidth(text, size));
    }
  }

  // Repeating the same measurements must reuse the cache without changing a bit.
  std::size_t index = 0;
  bool repeat_equal = true;
  for (const char* text : kTexts) {
    for (float size : kSizes) {
      repeat_equal = repeat_equal && warm->MeasureTextWidth(text, size) == baseline[index++];
    }
  }
  toy_test::Check(repeat_equal, "repeated measurements from a warm cache are bit-identical");

  // A fresh instance (empty caches) must agree bit for bit: shaping results are
  // a property of the font file and size, not of the cache state.
  index = 0;
  bool fresh_equal = true;
  for (const char* text : kTexts) {
    for (float size : kSizes) {
      fresh_equal = fresh_equal && fresh->MeasureTextWidth(text, size) == baseline[index++];
    }
  }
  toy_test::Check(fresh_equal, "a fresh Font measures exactly like a cache-warm one");

  // 20000 distinct texts at 17 distinct sizes evict the documented shaped-run
  // entry budget (16384 entries) several times over. Re-check the early results.
  for (int i = 0; i < 20000; ++i) {
    const std::string text = "stress" + std::to_string(i);
    (void)warm->MeasureTextWidth(text, 9.0f + static_cast<float>(i % 17));
  }
  index = 0;
  bool evicted_equal = true;
  for (const char* text : kTexts) {
    for (float size : kSizes) {
      evicted_equal = evicted_equal && warm->MeasureTextWidth(text, size) == baseline[index++];
    }
  }
  toy_test::Check(evicted_equal,
                  "evicting the shaped-run cache does not change earlier measurements");

  // A fresh font still agrees after the stress: the cache cannot leak state.
  index = 0;
  bool fresh_after_stress = true;
  for (const char* text : kTexts) {
    for (float size : kSizes) {
      fresh_after_stress =
          fresh_after_stress && fresh->MeasureTextWidth(text, size) == baseline[index++];
    }
  }
  toy_test::Check(fresh_after_stress, "cache stress on one Font never affects another Font");
}

void TestFontFromFileAndMemoryAgree(const std::string& directory) {
  const std::optional<std::string> bytes = ReadFile(directory + "/toy_test.ttf");
  if (!bytes.has_value()) {
    toy_test::Check(false, "toy_test.ttf can be read for the file/memory comparison");
    return;
  }
  std::string error;
  const std::shared_ptr<const Font> from_memory = Font::LoadFromMemory(*bytes, &error);
  const std::shared_ptr<const Font> from_file =
      Font::LoadFromFile(directory + "/toy_test.ttf", &error);
  toy_test::Check(from_memory != nullptr && from_file != nullptr,
                  "the fixture font loads both ways for the comparison");
  if (from_memory == nullptr || from_file == nullptr) {
    return;
  }
  toy_test::Check(
      from_memory->MeasureTextWidth("ABC", 16.0f) == from_file->MeasureTextWidth("ABC", 16.0f),
      "file and memory loads of the same font measure identically");
  toy_test::Check(from_memory->AscentPx(16.0f) == from_file->AscentPx(16.0f),
                  "file and memory loads report the same ascent");
}

void TestSizeClamp(const Font& font) {
  constexpr float kHugeSize = 100000.0f;
  // Sizes above the frozen bound behave exactly like the bound: the clamp
  // happens before any metric, shaping or rasterization math, so these are
  // bit-identical values, not merely close ones.
  toy_test::Check(font.AscentPx(kHugeSize) == font.AscentPx(kMaxRealFontSizePx),
                  "AscentPx clamps sizes above kMaxRealFontSizePx exactly");
  toy_test::Check(font.LineHeightPx(kHugeSize) == font.LineHeightPx(kMaxRealFontSizePx),
                  "LineHeightPx clamps sizes above kMaxRealFontSizePx exactly");
  toy_test::Check(
      font.MeasureTextWidth("ABC", kHugeSize) == font.MeasureTextWidth("ABC", kMaxRealFontSizePx),
      "MeasureTextWidth clamps sizes above kMaxRealFontSizePx exactly");
  toy_test::Check(font.MeasureTextWidth("", kHugeSize) == 0.0f,
                  "empty text still measures zero above the bound");

  // The bound itself is an ordinary size that follows the fixture design metrics.
  toy_test::Check(NearlyEqual(font.AscentPx(kMaxRealFontSizePx), 0.8f * kMaxRealFontSizePx),
                  "AscentPx at the bound uses 800/1000 em");
  toy_test::Check(NearlyEqual(font.LineHeightPx(kMaxRealFontSizePx), kMaxRealFontSizePx),
                  "LineHeightPx at the bound uses 1000/1000 em");
  toy_test::Check(NearlyEqual(font.MeasureTextWidth("A", kMaxRealFontSizePx), 2457.59375f),
                  "the shaped advance at the bound is 2457.59375 px");
}

}  // namespace

int main(int argc, char** argv) {
  toy_test::Check(argc >= 2, "the font fixture directory is passed as argv[1]");
  if (argc < 2) {
    return toy_test::Finish("render.font_ttf");
  }
  const std::string directory = argv[1];

  TestLoadFromFile(directory);
  TestLoadFromMemoryCopiesBytes(directory);
  TestLoadFailures(directory);
  TestLoadsNeverThrow(directory);
  TestFontFromFileAndMemoryAgree(directory);

  std::string error;
  const std::shared_ptr<const Font> font = Font::LoadFromFile(directory + "/toy_test.ttf", &error);
  if (font == nullptr) {
    toy_test::Check(false, "the fixture font loads for the metric tests");
  } else {
    TestMetrics(*font);
    TestMeasureTextWidth(*font);
    TestSizeClamp(*font);
    TestCacheDoesNotChangeMeasurements(directory);
  }
  return toy_test::Finish("render.font_ttf");
}
