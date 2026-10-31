#include "page.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "text_metrics.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/net/http.h"
#include "toy/net/url.h"
#include "toy/paint/paint.h"
#include "toy/render/image.h"
#include "toy/style/computed_style.h"

namespace toy::shell {
namespace {

// First attribute with `name`; the DOM stores names lowercase and in source
// order, so this is also the "first src" the HTML parser saw.
const dom::Attribute* FindFirstAttribute(const dom::Node& node, std::string_view name) {
  for (const dom::Attribute& attribute : node.attributes) {
    if (attribute.name == name) {
      return &attribute;
    }
  }
  return nullptr;
}

// Fetches and decodes one <img> and records it under its DOM node id. Every
// failure below is an ordinary, silently skipped image: it leaves the page
// load successful and only omits the map entries, so the element lays out as an
// empty inline. That is the documented M8a MVP limit (no broken-image icon,
// no stderr noise), not a hidden error.
void LoadOneImage(const dom::Document& document, const net::Url& page_url, dom::NodeId id,
                  Page* page) {
  const dom::Attribute* src = FindFirstAttribute(document.Get(id), "src");
  if (src == nullptr || src->value.empty()) {
    return;
  }
  std::string error;
  // ResolveUrl also rejects references with an unsupported scheme (anything but
  // http/file), so no unsupported fetch is ever attempted.
  const std::optional<net::Url> resolved = net::ResolveUrl(page_url, src->value, &error);
  if (!resolved) {
    return;
  }
  net::Response response;
  if (!net::Fetch(*resolved, net::FetchOptions{}, &response, &error)) {
    return;
  }
  render::Image image;
  if (!render::DecodePng(response.body, &image, &error)) {
    return;
  }
  // One fetch and decode per <img> occurrence: duplicate URLs are fetched twice
  // on purpose (no image cache in this MVP).
  page->replaced_sizes[id] =
      layout::ReplacedSize{static_cast<float>(image.width), static_cast<float>(image.height)};
  page->images[id] = std::make_shared<const render::Image>(std::move(image));
}

// Walks the whole DOM iteratively (documents may be deeply nested) and loads
// every <img> in document order. Iterative traversal keeps the shell free of
// recursion, matching the DOM/layout conventions.
void LoadImages(const dom::Document& document, const net::Url& page_url, Page* page) {
  std::vector<dom::NodeId> pending;
  pending.push_back(document.root());
  while (!pending.empty()) {
    const dom::NodeId id = pending.back();
    pending.pop_back();
    const dom::Node& node = document.Get(id);
    if (node.type == dom::NodeType::kElement && node.name == "img") {
      LoadOneImage(document, page_url, id, page);
    }
    for (dom::NodeId child = node.last_child; child != dom::kInvalidNode;
         child = document.Get(child).prev_sibling) {
      pending.push_back(child);
    }
  }
}

}  // namespace

void RelayoutPage(Page* page, int viewport_width) {
  // Field-wise setup on purpose: LayoutOptions may grow fields, and a named
  // assignment keeps every default visible here.
  layout::LayoutOptions options;
  options.viewport_width_px = static_cast<float>(viewport_width);
  options.replaced_sizes = page->replaced_sizes.empty() ? nullptr : &page->replaced_sizes;
  page->boxes = layout::LayoutDocument(page->document, page->styles, options, MeasureText);
}

bool LoadPage(const net::Url& url, int viewport_width, Page* page, std::string* error) {
  net::Response response;
  std::string fetch_error;
  if (!net::Fetch(url, net::FetchOptions{}, &response, &fetch_error)) {
    if (error != nullptr) {
      *error = "cannot fetch '" + net::SerializeUrl(url) + "': " + fetch_error;
    }
    return false;
  }

  // Build the whole page before the caller swaps it in: the only failure above
  // happens first, so the caller's current page stays untouched.
  Page loaded;
  loaded.final_url = response.final_url;
  loaded.document = dom::ParseDocument(response.body);
  loaded.styles = style::ComputeStyles(loaded.document);
  // Image sources resolve against the page's final URL (after redirects), like
  // the other page-relative references the shell resolves.
  LoadImages(loaded.document, loaded.final_url, &loaded);
  RelayoutPage(&loaded, viewport_width);

  *page = std::move(loaded);
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

}  // namespace toy::shell
