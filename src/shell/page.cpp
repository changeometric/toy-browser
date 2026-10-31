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

bool IsAsciiWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

std::string TrimAsciiWhitespace(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && IsAsciiWhitespace(text[begin])) {
    ++begin;
  }
  while (end > begin && IsAsciiWhitespace(text[end - 1])) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::string AsciiLowercase(std::string_view text) {
  std::string lower;
  lower.reserve(text.size());
  for (const char c : text) {
    lower.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  }
  return lower;
}

// Splits `text` on ASCII whitespace into its non-empty tokens.
std::vector<std::string> SplitAsciiWhitespace(std::string_view text) {
  std::vector<std::string> tokens;
  std::size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && IsAsciiWhitespace(text[index])) {
      ++index;
    }
    const std::size_t begin = index;
    while (index < text.size() && !IsAsciiWhitespace(text[index])) {
      ++index;
    }
    if (index > begin) {
      tokens.emplace_back(text.substr(begin, index - begin));
    }
  }
  return tokens;
}

// The first attribute with `name`, ASCII-whitespace trimmed and ASCII-lowercased;
// an empty string when the attribute is absent.
std::string FirstAttributeValueLowercase(const dom::Node& node, std::string_view name) {
  const dom::Attribute* attribute = FindFirstAttribute(node, name);
  return attribute == nullptr ? std::string()
                              : AsciiLowercase(TrimAsciiWhitespace(attribute->value));
}

// The page language hint: the first `lang` attribute of the root <html> element
// in document order, trimmed, or empty. Only the root <html> element counts (not
// a lang on <body> or on descendants), which keeps the hint page-wide and simple.
std::string ExtractPageLanguage(const dom::Document& document) {
  std::vector<dom::NodeId> pending;
  pending.push_back(document.root());
  while (!pending.empty()) {
    const dom::NodeId id = pending.back();
    pending.pop_back();
    const dom::Node& node = document.Get(id);
    if (node.type == dom::NodeType::kElement && node.name == "html") {
      const dom::Attribute* language = FindFirstAttribute(node, "lang");
      return language == nullptr ? std::string() : TrimAsciiWhitespace(language->value);
    }
    for (dom::NodeId child = node.last_child; child != dom::kInvalidNode;
         child = document.Get(child).prev_sibling) {
      pending.push_back(child);
    }
  }
  return {};
}

// Fetches the author stylesheet text of one <link> element, or returns
// std::nullopt when the element is not an applicable stylesheet or cannot be
// loaded. Every skip below is deliberate and silent: the M12 contract skips
// filtered, unresolvable, failed and non-2xx links without failing the page or
// printing anything, exactly like missing images are skipped (M8a).
std::optional<std::string> FetchLinkedStyleSheet(const dom::Node& link,
                                                 const net::Url& page_url) {
  // rel: first occurrence, split on ASCII whitespace and ASCII-lowercased. A
  // "stylesheet" token applies the sheet unless an "alternate" token is present
  // (alternate sheets are out of scope for M12).
  const dom::Attribute* rel = FindFirstAttribute(link, "rel");
  if (rel == nullptr) {
    return std::nullopt;
  }
  bool stylesheet = false;
  bool alternate = false;
  for (const std::string& token : SplitAsciiWhitespace(rel->value)) {
    const std::string lower = AsciiLowercase(token);
    if (lower == "stylesheet") {
      stylesheet = true;
    } else if (lower == "alternate") {
      alternate = true;
    }
  }
  if (!stylesheet || alternate) {
    return std::nullopt;
  }
  // Any `disabled` attribute (any value, including the empty one) disables the
  // link; disabled sheets are out of scope for M12.
  if (FindFirstAttribute(link, "disabled") != nullptr) {
    return std::nullopt;
  }
  // href: first occurrence, trimmed; empty or unresolvable references are
  // skipped (ResolveUrl also rejects unsupported schemes before any fetch).
  const dom::Attribute* href = FindFirstAttribute(link, "href");
  if (href == nullptr) {
    return std::nullopt;
  }
  const std::string reference = TrimAsciiWhitespace(href->value);
  if (reference.empty()) {
    return std::nullopt;
  }
  std::string error;
  const std::optional<net::Url> url = net::ResolveUrl(page_url, reference, &error);
  if (!url) {
    return std::nullopt;
  }
  // media: first occurrence, trimmed and lowercased. This MVP applies only the
  // empty value, "all" and "screen"; media queries and comma lists are skipped.
  const std::string media = FirstAttributeValueLowercase(link, "media");
  if (!media.empty() && media != "all" && media != "screen") {
    return std::nullopt;
  }
  // type: first occurrence, trimmed and lowercased. This MVP applies only the
  // empty value and "text/css"; a parameterised value is skipped.
  const std::string type = FirstAttributeValueLowercase(link, "type");
  if (!type.empty() && type != "text/css") {
    return std::nullopt;
  }
  net::Response response;
  if (!net::Fetch(*url, net::FetchOptions{}, &response, &error)) {
    return std::nullopt;
  }
  if (response.status < 200 || response.status > 299) {
    return std::nullopt;
  }
  return std::optional<std::string>(std::move(response.body));
}

// Discovers the page's author stylesheets with one iterative pre-order DOM walk
// (documents may be deeply nested) over the whole tree, so <link> elements in the
// head and in the body alike are candidates in document order; that is also the
// fetch order. There is no deduplication or cache: every applicable <link>
// occurrence is fetched once, like every <img> occurrence.
std::vector<style::LinkedStyleSheet> LoadLinkedStyleSheets(const dom::Document& document,
                                                           const net::Url& page_url) {
  std::vector<style::LinkedStyleSheet> sheets;
  std::vector<dom::NodeId> pending;
  pending.push_back(document.root());
  while (!pending.empty()) {
    const dom::NodeId id = pending.back();
    pending.pop_back();
    const dom::Node& node = document.Get(id);
    if (node.type == dom::NodeType::kElement && node.name == "link") {
      std::optional<std::string> text = FetchLinkedStyleSheet(node, page_url);
      if (text) {
        sheets.push_back(style::LinkedStyleSheet{id, std::move(*text)});
      }
    }
    for (dom::NodeId child = node.last_child; child != dom::kInvalidNode;
         child = document.Get(child).prev_sibling) {
      pending.push_back(child);
    }
  }
  return sheets;
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
  // http/https/file), so no unsupported fetch is ever attempted.
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

void RelayoutPage(Page* page, int viewport_width, const std::shared_ptr<const Fonts>& fonts) {
  // Field-wise setup on purpose: LayoutOptions may grow fields, and a named
  // assignment keeps every default visible here.
  layout::LayoutOptions options;
  options.viewport_width_px = static_cast<float>(viewport_width);
  options.replaced_sizes = page->replaced_sizes.empty() ? nullptr : &page->replaced_sizes;
  const layout::MeasureText measure = [&fonts, &language = page->language](
                                          std::string_view text_utf8,
                                          const style::ComputedStyle& style) {
    return MeasureText(fonts.get(), text_utf8, style, language);
  };
  page->boxes = layout::LayoutDocument(page->document, page->styles, options, measure);
}

bool LoadStyledDocument(const net::Url& url, dom::Document* document,
                        style::StyleTree* styles, net::Url* final_url, std::string* error) {
  net::Response response;
  std::string fetch_error;
  if (!net::Fetch(url, net::FetchOptions{}, &response, &fetch_error)) {
    if (error != nullptr) {
      *error = fetch_error;
    }
    return false;
  }

  // HTML and CSS parsing are error-tolerant, so only the page fetch above can
  // fail. Build everything before writing the outs: on failure the caller's
  // outputs stay untouched.
  dom::Document parsed = dom::ParseDocument(response.body);
  // Author sheets resolve against the response's final URL, i.e. after redirects.
  const std::vector<style::LinkedStyleSheet> linked_sheets =
      LoadLinkedStyleSheets(parsed, response.final_url);
  style::StyleTree computed = style::ComputeStyles(parsed, linked_sheets);

  *document = std::move(parsed);
  *styles = std::move(computed);
  *final_url = response.final_url;
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool LoadPage(const net::Url& url, int viewport_width, const std::shared_ptr<const Fonts>& fonts,
              Page* page, std::string* error) {
  // Build the whole page before the caller swaps it in: the only failure
  // (a page fetch failure) is detected first, so the caller's current page stays
  // untouched.
  Page loaded;
  std::string fetch_error;
  if (!LoadStyledDocument(url, &loaded.document, &loaded.styles, &loaded.final_url, &fetch_error)) {
    if (error != nullptr) {
      *error = "cannot fetch '" + net::SerializeUrl(url) + "': " + fetch_error;
    }
    return false;
  }

  loaded.language = ExtractPageLanguage(loaded.document);
  // Image sources resolve against the page's final URL (after redirects), like
  // the other page-relative references the shell resolves.
  LoadImages(loaded.document, loaded.final_url, &loaded);
  RelayoutPage(&loaded, viewport_width, fonts);

  *page = std::move(loaded);
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

}  // namespace toy::shell
