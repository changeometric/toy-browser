// M8b end-to-end real-font test: shell font selection -> layout metrics through
// the shell MeasureText adapter -> paint with DrawTextWithFont, plus the built-in
// bitmap fallback. Everything runs against the committed file:// fixture font and
// font site; no display, no network and no installed font are needed.
//
// Geometry is frozen from `toybrowser --font <fixture> --size 320x200
// --dump-boxes tests/fixtures/font_site/index.html` (1 CSS px = 1 device px).
// The fixture font is monospace: upem 1000, ascent 800, descent 200, advance 600
// units (0.6 em). At 16 px one glyph advances 9.6 px and one line is 16 px; at the
// h1 size 32 px it advances 19.2 px and one line is 32 px. Relevant dump lines:
//
//   <html> block x=0 y=0 width=320 height=416
//   <body> block x=8 y=8 width=304 height=400
//   <h1> block x=8 y=24 width=304 height=32
//     text "ABC" x=8 y=24 width=58 height=32
//   <p> block x=8 y=88 width=304 height=16
//     text "AB" x=8 y=88 width=19 height=16
//     text "Cab" x=37 y=88 width=29 height=16
//     text "012." x=75 y=88 width=38 height=16
//   <p> block x=8 y=184 width=304 height=16
//     <img> inline x=8 y=184 width=16 height=8   (flag_16x8.png)
//     text "AB" x=34 y=184 width=19 height=16
//   ... filler paragraphs; the page is taller than the 200 px viewport.

#include <cstdio>
#include <memory>
#include <string>

#include "support/check.h"
#include "toy/render/font.h"
#include "toy/render/surface.h"
#include "toy/shell/browser.h"

namespace {

constexpr int kViewportWidth = 320;
constexpr int kViewportHeight = 200;
constexpr int kContentHeight = 416;
constexpr int kMaxScroll = kContentHeight - kViewportHeight;

constexpr toy::render::Argb kWhite = 0xFFFFFFFFu;
constexpr toy::render::Argb kBlack = 0xFF000000u;
constexpr toy::render::Argb kRed = 0xFFFF0000u;
constexpr toy::render::Argb kBlue = 0xFF0000FFu;

// Frozen geometry of the fixture page at 320x200 with the fixture font.
constexpr int kHeadingX0 = 8;
constexpr int kHeadingY0 = 24;
constexpr int kHeadingX1 = 66;  // "ABC" is 57.6 px wide at 32 px
constexpr int kHeadingY1 = 56;
constexpr int kParagraphX0 = 8;
constexpr int kParagraphY0 = 88;
constexpr int kParagraphX1 = 114;  // "AB Cab 012." ends at 113.6 px
constexpr int kParagraphY1 = 104;
constexpr int kImageX = 8;
constexpr int kImageY = 184;
constexpr int kImageWidth = 16;
constexpr int kImageHeight = 8;

std::string FileUrl(const std::string& directory, const std::string& name) {
  return "file://" + directory + "/" + name;
}

int CountNonWhiteInRect(const toy::render::Surface& surface, int x0, int y0, int x1, int y1) {
  int count = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      if (surface.At(x, y) != kWhite) {
        ++count;
      }
    }
  }
  return count;
}

// Pixels neither the opaque text colour nor white: real-font anti-aliasing
// composites the black text with the white canvas, so ink has intermediate
// coverage values. The built-in bitmap font must never produce one.
int CountBlendedInRect(const toy::render::Surface& surface, int x0, int y0, int x1, int y1) {
  int count = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const toy::render::Argb pixel = surface.At(x, y);
      if (pixel != kWhite && pixel != kBlack) {
        ++count;
      }
    }
  }
  return count;
}

int CountColorInRect(const toy::render::Surface& surface, int x0, int y0, int x1, int y1,
                     toy::render::Argb color) {
  int count = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      if (surface.At(x, y) == color) {
        ++count;
      }
    }
  }
  return count;
}

bool SamePixels(const toy::render::Surface& a, const toy::render::Surface& b) {
  if (a.width() != b.width() || a.height() != b.height()) {
    return false;
  }
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      if (a.At(x, y) != b.At(x, y)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <font_site-directory>\n", argv[0]);
    return 1;
  }
  const std::string site = argv[1];
  const std::string index_url = FileUrl(site, "index.html");
  // The committed fixture font lives next to the site; both are in the repo.
  const std::string font_path = site + "/../fonts/toy_test.ttf";

  std::string error;
  const std::shared_ptr<const toy::render::Font> font =
      toy::render::Font::LoadFromFile(font_path, &error);
  toy_test::Check(font != nullptr, "the committed fixture font loads");
  if (!font) {
    std::fprintf(stderr, "  cannot load '%s': %s\n", font_path.c_str(), error.c_str());
    return toy_test::Finish("shell fonts test");
  }
  toy_test::Check(error.empty(), "a successful font load clears the error");

  // --- Real font: layout metrics and anti-aliased painting -------------------
  toy::shell::Browser browser(kViewportWidth, kViewportHeight, font);
  toy_test::Check(browser.Open(index_url, &error), "Open(font_site/index.html) succeeds");
  toy_test::Check(error.empty(), "a successful load clears the error");
  toy_test::Check(browser.has_page(), "the font fixture page is current");
  toy_test::Check(browser.title() == "ABC", "the title comes from <title>");
  toy_test::Check(browser.scroll_y() == 0, "a fresh page starts at scroll 0");
  toy_test::Check(browser.content_height() == kContentHeight,
                  "the content height comes from the real font's line height");

  toy::render::Surface real(kViewportWidth, kViewportHeight);
  browser.Paint(real);

  // The heading and a paragraph carry real-glyph ink with anti-aliased coverage.
  toy_test::Check(CountNonWhiteInRect(real, kHeadingX0, kHeadingY0, kHeadingX1, kHeadingY1) > 0,
                  "the real-font heading paints ink at its dump geometry");
  toy_test::Check(CountBlendedInRect(real, kHeadingX0, kHeadingY0, kHeadingX1, kHeadingY1) > 0,
                  "the real-font heading is anti-aliased (blended pixels exist)");
  toy_test::Check(
      CountNonWhiteInRect(real, kParagraphX0, kParagraphY0, kParagraphX1, kParagraphY1) > 0,
      "the real-font paragraph paints ink at its dump geometry");
  toy_test::Check(
      CountBlendedInRect(real, kParagraphX0, kParagraphY0, kParagraphX1, kParagraphY1) > 0,
      "the real-font paragraph is anti-aliased (blended pixels exist)");

  // The image next to real text is still painted from the page's image map.
  toy_test::Check(real.At(kImageX, kImageY) == kRed, "the fixture image paints its red half");
  toy_test::Check(real.At(kImageX + 8, kImageY) == kBlue, "the fixture image paints its blue half");
  toy_test::Check(real.At(kImageX, kImageY + kImageHeight - 1) == kRed,
                  "the fixture image keeps its height");
  toy_test::Check(real.At(kImageX + kImageWidth - 1, kImageY + kImageHeight - 1) == kBlue,
                  "the fixture image keeps its width");
  toy_test::Check(real.At(kImageX + kImageWidth, kImageY) == kWhite,
                  "the space after the image stays white");

  // --- Scrolling: real-font content is clipped like any other page ------------
  toy_test::Check(browser.ScrollToBottom(), "ScrollToBottom changes the offset");
  toy_test::Check(browser.scroll_y() == kMaxScroll,
                  "the bottom offset is content_height - viewport_height");
  toy::render::Surface scrolled(kViewportWidth, kViewportHeight);
  browser.Paint(scrolled);
  toy_test::Check(CountColorInRect(scrolled, 0, 0, kViewportWidth, kViewportHeight, kRed) == 0,
                  "an image scrolled above the viewport paints no red");
  toy_test::Check(CountColorInRect(scrolled, 0, 0, kViewportWidth, kViewportHeight, kBlue) == 0,
                  "an image scrolled above the viewport paints no blue");
  toy_test::Check(CountNonWhiteInRect(scrolled, 0, 0, kViewportWidth, kViewportHeight) > 0,
                  "the visible real-font text is still painted after scrolling");

  // --- Default session: the built-in bitmap path is still the fallback --------
  toy::shell::Browser bitmap(kViewportWidth, kViewportHeight);
  toy_test::Check(bitmap.Open(index_url, &error), "the same page loads with the bitmap font");
  toy_test::Check(bitmap.content_height() == kContentHeight,
                  "the bitmap font lays out the fixture at the same frozen height");
  toy::render::Surface fallback(kViewportWidth, kViewportHeight);
  bitmap.Paint(fallback);
  const int fallback_ink =
      CountNonWhiteInRect(fallback, kHeadingX0, kHeadingY0, kHeadingX1, kHeadingY1);
  toy_test::Check(fallback_ink > 0, "the bitmap fallback paints text ink");
  toy_test::Check(fallback_ink == CountColorInRect(fallback, kHeadingX0, kHeadingY0, kHeadingX1,
                                                   kHeadingY1, kBlack),
                  "the bitmap fallback ink is opaque black (no anti-aliasing)");
  toy_test::Check(!SamePixels(real, fallback),
                  "real-font painting differs from the bitmap fallback");

  return toy_test::Finish("shell fonts test");
}
