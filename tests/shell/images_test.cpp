// M8a end-to-end image test: fetch -> parse -> style -> decode -> layout ->
// paint through toy::shell::Browser against the committed file:// image fixture
// site. No display, no network and no installed font are needed.
//
// The expected geometry is frozen from a `toybrowser --size 320x200 --dump-boxes`
// run on tests/fixtures/img_site/index.html (1 CSS px = 1 device px). Relevant
// dump lines:
//
//   <p> block x=8 y=24  width=304 height=16
//     text "Before" x=8 y=24 width=48 height=16
//     <img> inline x=64 y=24 width=16 height=8      (flag_16x8.png at 1:1)
//     text "after" x=88 y=24 width=40 height=16
//   <p> block x=8 y=72  width=304 height=32
//     <img> inline x=8 y=72 width=64 height=32      (flag_16x8.png, CSS-scaled)
//   <p> block x=8 y=136 width=304 height=8
//     <img> inline x=8 y=136 width=8 height=8       (alpha_8x8.png at 1:1)
//   <p> block x=8 y=176 width=304 height=16
//     <img> inline x=8 y=176 width=4 height=4       (gray_4x4.png at 1:1)
//     text "gray" x=20 y=176 width=32 height=16
//   <p> block x=8 y=224 width=304 height=16
//     <img> inline x=8 y=224 width=32 height=16     (palette_4x2.png, CSS-scaled)
//     text "palette" x=48 y=224 width=56 height=16
//   <p> block x=8 y=272 width=304 height=16
//     text "Missing" x=8 y=272 width=56 height=16
//     text "image" x=72 y=272 width=40 height=16    (missing.png has no box)
//
// The page is 568 device px tall, so a 320x200 viewport has 368 px of scroll
// range and the palette image at y=224 is clipped at scroll 0.

#include <cstdio>
#include <string>

#include "support/check.h"
#include "toy/render/surface.h"
#include "toy/shell/browser.h"

namespace {

constexpr int kViewportWidth = 320;
constexpr int kViewportHeight = 200;
constexpr int kContentHeight = 568;
constexpr int kMaxScroll = kContentHeight - kViewportHeight;

// Exact pixels: fixture colours and the frozen SrcOver result of alpha_8x8's
// #ff0000 at alpha 128 over the white canvas (render::DrawDisplayList's formula,
// documented in docs/handoff.md).
constexpr toy::render::Argb kWhite = 0xFFFFFFFFu;
constexpr toy::render::Argb kRed = 0xFFFF0000u;
constexpr toy::render::Argb kBlue = 0xFF0000FFu;
constexpr toy::render::Argb kGreen = 0xFF00FF00u;
constexpr toy::render::Argb kHalfRedOverWhite = 0xFFFF7F7Fu;
constexpr toy::render::Argb kGray0 = 0xFF000000u;
constexpr toy::render::Argb kGray64 = 0xFF404040u;
constexpr toy::render::Argb kGray128 = 0xFF808080u;

std::string FileUrl(const std::string& directory, const std::string& name) {
  return "file://" + directory + "/" + name;
}

int CountColor(const toy::render::Surface& surface, toy::render::Argb color) {
  int count = 0;
  for (int y = 0; y < surface.height(); ++y) {
    for (int x = 0; x < surface.width(); ++x) {
      if (surface.At(x, y) == color) {
        ++count;
      }
    }
  }
  return count;
}

// Exact-count helper: a wrong scale, blend, paint order or clip changes the
// number of pixels of a fixture colour.
void CheckCount(const toy::render::Surface& surface, toy::render::Argb color, int expected,
                const char* description) {
  const int actual = CountColor(surface, color);
  if (actual != expected) {
    std::fprintf(stderr, "  %s: %d pixel(s), expected %d\n", description, actual, expected);
  }
  toy_test::Check(actual == expected, description);
}

// Number of pixels of any of the fixture image colours inside a rectangle
// (half-open). Used to prove a skipped image paints nothing.
int CountImageColorsInRect(const toy::render::Surface& surface, int x0, int y0, int x1, int y1) {
  int count = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const toy::render::Argb pixel = surface.At(x, y);
      if (pixel == kRed || pixel == kBlue || pixel == kGreen || pixel == kHalfRedOverWhite ||
          pixel == kGray64 || pixel == kGray128) {
        ++count;
      }
    }
  }
  return count;
}

// Non-white pixel count inside a rectangle, to prove text is still painted.
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

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <img_site-directory>\n", argv[0]);
    return 1;
  }
  const std::string site = argv[1];
  const std::string index_url = FileUrl(site, "index.html");

  toy::shell::Browser browser(kViewportWidth, kViewportHeight);
  std::string error;

  // --- Load: the missing image is skipped, the page load still succeeds ------
  toy_test::Check(browser.Open(index_url, &error),
                  "Open(index.html) succeeds although the page has a missing image");
  toy_test::Check(error.empty(), "a successful load clears the error");
  toy_test::Check(browser.has_page(), "the fixture page is current");
  toy_test::Check(browser.title() == "Image pipeline", "the title comes from <title>");
  toy_test::Check(browser.scroll_y() == 0, "a fresh page starts at scroll 0");
  toy_test::Check(browser.content_height() == kContentHeight,
                  "the content height matches the frozen box dump (skipped image = empty inline)");

  // --- Scroll 0: image geometry, nearest-neighbour scaling, alpha blending ---
  {
    toy::render::Surface surface(kViewportWidth, kViewportHeight);
    browser.Paint(surface);

    // flag_16x8.png at 1:1 (x=64 y=24): columns 64-71 red, 72-79 blue, 8 rows.
    toy_test::Check(surface.At(64, 24) == kRed, "1:1 flag left half is red");
    toy_test::Check(surface.At(71, 31) == kRed, "1:1 flag red half keeps its 8x8 box");
    toy_test::Check(surface.At(72, 24) == kBlue, "1:1 flag right half is blue");
    toy_test::Check(surface.At(79, 31) == kBlue, "1:1 flag blue half keeps its 8x8 box");
    toy_test::Check(surface.At(80, 24) == kWhite, "the space after the 1:1 flag stays white");

    // flag_16x8.png scaled to 64x32 (x=8 y=72): nearest neighbour splits at the
    // middle, and no smoothing may leak across the boundary.
    toy_test::Check(surface.At(8, 72) == kRed, "scaled flag left half is red");
    toy_test::Check(surface.At(39, 103) == kRed, "scaled flag red half ends at x=39");
    toy_test::Check(surface.At(40, 72) == kBlue, "scaled flag right half starts at x=40");
    toy_test::Check(surface.At(71, 103) == kBlue, "scaled flag blue half ends at x=71");
    toy_test::Check(surface.At(8, 71) == kWhite, "the line above the scaled flag stays white");

    // alpha_8x8.png at 1:1 (x=8 y=136): the alpha-128 red half blends over white,
    // the opaque green half paints its own colour.
    toy_test::Check(surface.At(8, 136) == kHalfRedOverWhite, "alpha red over white is blended");
    toy_test::Check(surface.At(11, 143) == kHalfRedOverWhite, "the blended half keeps its box");
    toy_test::Check(surface.At(12, 136) == kGreen, "the opaque green half is not blended");
    toy_test::Check(surface.At(15, 143) == kGreen, "the green half keeps its box");
    toy_test::Check(surface.At(16, 136) == kWhite, "the space after the alpha image is white");

    // gray_4x4.png at 1:1 (x=8 y=176): grayscale rows decode to opaque gray.
    toy_test::Check(surface.At(8, 176) == kGray0, "gray image column 0 is black");
    toy_test::Check(surface.At(9, 176) == kGray64, "gray image column 1 is dark gray");
    toy_test::Check(surface.At(10, 176) == kGray128, "gray image column 2 is mid gray");
    toy_test::Check(surface.At(11, 176) == kWhite, "gray image column 3 is white");
    toy_test::Check(surface.At(9, 179) == kGray64, "gray image rows repeat the same values");

    // Exact colour counts prove scale, blend and viewport clipping together. The
    // palette image at y=224 lies completely below the 200 px viewport, so none of
    // its pixels may appear.
    CheckCount(surface, kRed, 1088, "scroll 0: exact red pixel count");
    CheckCount(surface, kBlue, 1088, "scroll 0: exact blue pixel count");
    CheckCount(surface, kGreen, 32, "scroll 0: exact green pixel count");
    CheckCount(surface, kHalfRedOverWhite, 32, "scroll 0: exact blended pixel count");
    CheckCount(surface, kGray64, 4, "scroll 0: exact dark-gray pixel count");
    CheckCount(surface, kGray128, 4, "scroll 0: exact mid-gray pixel count");
  }

  // --- The skipped image line: text only, no image pixels --------------------
  // Scroll to page y=200 so the "Missing <img src="missing.png"> image ..." line
  // (dump y=272) is fully visible; its skipped inline must paint nothing.
  {
    // The previous block left the page at scroll 0, so ScrollToTop is a no-op here.
    browser.ScrollToTop();
    toy_test::Check(browser.scroll_y() == 0,
                    "the page is at scroll 0 before the missing-image check");
    toy_test::Check(browser.ScrollBy(200), "scroll 200 px to the missing-image line");
    toy_test::Check(browser.scroll_y() == 200, "the scroll offset is 200 px");

    toy::render::Surface surface(kViewportWidth, kViewportHeight);
    browser.Paint(surface);
    toy_test::Check(CountImageColorsInRect(surface, 8, 72, 304, 88) == 0,
                    "the missing image paints no image pixels");
    toy_test::Check(CountNonWhiteInRect(surface, 8, 72, 304, 88) > 0,
                    "the missing image keeps the surrounding text painted");
  }

  // --- Scroll to the bottom: the top images are clipped away ------------------
  {
    toy_test::Check(browser.ScrollToBottom(), "ScrollToBottom changes the offset");
    toy_test::Check(browser.scroll_y() == kMaxScroll,
                    "the bottom offset is content_height - viewport_height");

    toy::render::Surface surface(kViewportWidth, kViewportHeight);
    browser.Paint(surface);
    CheckCount(surface, kRed, 0, "bottom scroll: images scrolled out paint no red");
    CheckCount(surface, kBlue, 0, "bottom scroll: images scrolled out paint no blue");
    CheckCount(surface, kGreen, 0, "bottom scroll: images scrolled out paint no green");
    CheckCount(surface, kHalfRedOverWhite, 0, "bottom scroll: no blended alpha pixel");
    toy_test::Check(CountNonWhiteInRect(surface, 0, 0, kViewportWidth, kViewportHeight) > 0,
                    "bottom scroll: the visible filler text is still painted");
  }

  // --- Partial scroll: translated and clipped image geometry ------------------
  // At scroll 80 the scaled flag starts 8 px above the viewport (rows 0-23
  // visible) and the palette image is fully visible; at scroll 32 the palette
  // image is cut by the bottom edge (only its first 8 rows are visible).
  {
    toy_test::Check(browser.ScrollToTop(), "ScrollToTop before the partial-scroll cases");
    toy_test::Check(browser.scroll_y() == 0, "ScrollToTop returns to the top");
    toy_test::Check(browser.ScrollBy(80), "scroll 80 px");
    toy_test::Check(browser.scroll_y() == 80, "the scroll offset is 80 px");

    toy::render::Surface surface(kViewportWidth, kViewportHeight);
    browser.Paint(surface);
    // The scaled flag is translated to y=-8: its first visible row is at y=0.
    toy_test::Check(surface.At(8, 0) == kRed, "a partially scrolled image starts at the clip edge");
    toy_test::Check(surface.At(40, 0) == kBlue, "the partially scrolled image keeps its scale");
    toy_test::Check(surface.At(8, 23) == kRed, "the last visible row of the clipped image");
    toy_test::Check(surface.At(71, 23) == kBlue, "the clipped image's last visible column");
    toy_test::Check(surface.At(8, 24) == kWhite, "nothing is painted below the clipped image");
    // Palette image at page y=224 is now fully on screen.
    toy_test::Check(surface.At(8, 144) == kRed, "palette row 0 column 0 is red");
    toy_test::Check(surface.At(16, 144) == kGreen, "palette row 0 column 1 is green");
    toy_test::Check(surface.At(24, 144) == kBlue, "palette row 0 column 2 is blue");
    toy_test::Check(surface.At(8, 152) == kBlue, "palette row 1 column 0 is blue");
    toy_test::Check(surface.At(16, 152) == kGreen, "palette row 1 column 1 is green");
    toy_test::Check(surface.At(24, 152) == kRed, "palette row 1 column 2 is red");
    toy_test::Check(surface.At(32, 152) == kBlue, "palette row 1 column 3 is blue");
    // Exact counts: the 1:1 flag is scrolled out, the scaled flag shows 24 of its
    // 32 rows, and both palette rows are visible.
    CheckCount(surface, kRed, 960, "scroll 80: exact red pixel count");
    CheckCount(surface, kBlue, 960, "scroll 80: exact blue pixel count");
    CheckCount(surface, kGreen, 160, "scroll 80: exact green pixel count");
  }
  {
    toy_test::Check(browser.ScrollToTop(), "ScrollToTop before the bottom-edge clip");
    toy_test::Check(browser.ScrollBy(32), "scroll 32 px");
    toy_test::Check(browser.scroll_y() == 32, "the scroll offset is 32 px");

    toy::render::Surface surface(kViewportWidth, kViewportHeight);
    browser.Paint(surface);
    // The palette image starts at page y=224 -> device y=192; only 8 of its 16
    // rows fit into the 200 px viewport.
    toy_test::Check(surface.At(8, 192) == kRed, "the bottom-clipped image paints its first row");
    toy_test::Check(surface.At(16, 192) == kGreen, "the bottom-clipped row keeps its scale");
    toy_test::Check(surface.At(24, 192) == kBlue, "the bottom-clipped row keeps its colours");
    toy_test::Check(surface.At(8, 199) == kRed, "the last visible row of the clipped image");
    toy_test::Check(surface.At(8, 191) == kWhite, "the row above the image is still white");
    CheckCount(surface, kRed, 1152, "scroll 32: exact red pixel count");
    CheckCount(surface, kBlue, 1088, "scroll 32: exact blue pixel count");
    CheckCount(surface, kGreen, 96, "scroll 32: exact green pixel count");
  }

  // --- Relayout: resizing re-lays-out with the stored images (no refetch) ----
  // At a 200 px viewport the same text wraps into a taller page (648 px), while
  // the replaced boxes keep their intrinsic/CSS sizes and the decoded images are
  // still painted from the page's image map.
  {
    toy_test::Check(browser.ScrollToTop(), "ScrollToTop before the relayout");
    browser.SetViewport(200, 200);
    toy_test::Check(browser.viewport_width() == 200, "SetViewport updates the viewport width");
    toy_test::Check(browser.content_height() == 648,
                    "the relayout at 200 px uses the frozen 200 px box dump height");

    toy::render::Surface surface(200, 200);
    browser.Paint(surface);
    toy_test::Check(surface.At(64, 24) == kRed, "after relayout the 1:1 flag is still painted");
    toy_test::Check(surface.At(72, 24) == kBlue, "after relayout the 1:1 flag keeps its scale");
    toy_test::Check(surface.At(8, 72) == kRed, "after relayout the scaled flag is still painted");
    toy_test::Check(surface.At(40, 72) == kBlue, "after relayout the scaled flag keeps its scale");
    toy_test::Check(surface.At(8, 136) == kHalfRedOverWhite,
                    "after relayout the alpha image is still blended");
    toy_test::Check(surface.At(9, 176) == kGray64,
                    "after relayout the gray image is still painted");
    CheckCount(surface, kRed, 1088, "relayout 200: exact red pixel count");
    CheckCount(surface, kBlue, 1088, "relayout 200: exact blue pixel count");
    CheckCount(surface, kGreen, 32, "relayout 200: exact green pixel count");
  }

  // The session survives the whole image scenario set unchanged.
  toy_test::Check(browser.has_page(), "the page is still loaded at the end");
  return toy_test::Finish("shell images test");
}
