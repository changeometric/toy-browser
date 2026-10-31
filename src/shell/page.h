#pragma once

#include <memory>
#include <string>

#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/net/url.h"
#include "toy/paint/paint.h"
#include "toy/render/font.h"
#include "toy/shell/fonts.h"
#include "toy/style/computed_style.h"

namespace toy::shell {

// One loaded page: everything the shell keeps between loads. It is private to
// the shell module (not a public header and not a CMake module) and is rebuilt
// on every successful load, so a failed image can never corrupt a page that is
// already displayed.
//
// `images` and `replaced_sizes` share their keys: the DOM element node id of
// each <img>. Layout reserves the replaced box from `replaced_sizes`, paint
// draws the decoded pixels from `images`. A skipped image is absent from both
// maps and lays out as an empty inline (M8a MVP limit: no broken-image icon and
// no error output for image failures, see docs/roadmap.md).
//
// `language` is the page language hint: the value of the first `lang` attribute
// of the root <html> element in document order, ASCII-whitespace-trimmed, or
// empty when the attribute is absent. It is passed to fontconfig for every
// measurement and paint run so language-specific faces are preferred.
struct Page {
  net::Url final_url;
  std::string language;
  dom::Document document;
  style::StyleTree styles;
  layout::BoxTree boxes;
  layout::ReplacedSizes replaced_sizes;
  paint::ImageMap images;
};

// Fetches `url` with the default FetchOptions, parses the HTML and computes its
// styles, applying the page's external author stylesheets exactly like the
// normal page load path does.
//
// `url` is the requested page URL. The <link rel="stylesheet"> elements are
// discovered by one iterative pre-order DOM walk (head or body alike) and fetched
// in document order; `href` is ASCII-whitespace trimmed and resolved against the
// *final* page URL (after redirects), so a redirected page resolves its relative
// sheet references like a browser does. Sheets are filtered on `rel` (a
// "stylesheet" token without an "alternate" token), on the presence of
// `disabled`, on `media` (empty, "all" or "screen") and on `type` (empty or
// "text/css"), all first occurrences and ASCII-lowercased after trimming. Every
// applicable link is fetched once per occurrence: there is no deduplication or
// cache, like images.
//
// Only a page fetch failure returns false, with *error set to the raw net::Fetch
// error (callers add their own message/prefix); page parsing never fails.
// Stylesheet problems are never errors: a link that is filtered out, whose href
// is empty or cannot be resolved (for example an unsupported scheme), whose fetch
// fails or whose response status is outside 200..299 is skipped silently, with no
// stderr output and no effect on the page load. On success every out parameter is
// written (the out pointers must be non-null) and *error is cleared; on failure
// all outs are left untouched.
bool LoadStyledDocument(const net::Url& url, dom::Document* document,
                        style::StyleTree* styles, net::Url* final_url, std::string* error);

// Fetches `url`, parses and styles the HTML, fetches and decodes the page's
// images and finally lays the page out at `viewport_width` CSS px with `fonts`.
// `fonts` is the session's font selection, or null for the built-in bitmap font;
// layout always takes text metrics through the shell's MeasureText adapter, so
// this is the only place the two metrics sources meet.
//
// Returns false and sets *error to a ready-to-print message only when the page
// itself cannot be fetched; parse, style and layout never fail, and an image
// fetch or decode failure is skipped silently (one fetch attempt per <img>
// occurrence, no image cache). On success *page owns the complete page and
// *error is cleared; on failure *page is untouched.
bool LoadPage(const net::Url& url, int viewport_width, const std::shared_ptr<const Fonts>& fonts,
              Page* page, std::string* error);

// Re-lays out an already loaded page at a new viewport width from its stored
// DOM, styles, language, replaced sizes and images, using `fonts` for text
// metrics: it never refetches anything.
void RelayoutPage(Page* page, int viewport_width, const std::shared_ptr<const Fonts>& fonts);

}  // namespace toy::shell
