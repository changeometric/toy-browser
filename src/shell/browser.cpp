#include "toy/shell/browser.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "page.h"
#include "text_metrics.h"
#include "toy/dom/node.h"
#include "toy/layout/box.h"
#include "toy/net/http.h"
#include "toy/net/url.h"
#include "toy/paint/paint.h"
#include "toy/render/paint.h"
#include "toy/render/surface.h"
#include "toy/shell/fonts.h"

namespace toy::shell {
namespace {

// The canvas the shell paints behind pages, matching the M1/M6 presentation.
constexpr std::uint32_t kPageColor = 0xFFFFFFFFu;

bool IsAsciiWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

// Collapses runs of ASCII whitespace to one space and trims both ends.
std::string CollapseAsciiWhitespace(std::string_view text) {
  std::string collapsed;
  collapsed.reserve(text.size());
  bool pending_space = false;
  for (const char c : text) {
    if (IsAsciiWhitespace(c)) {
      pending_space = !collapsed.empty();
      continue;
    }
    if (pending_space) {
      collapsed.push_back(' ');
      pending_space = false;
    }
    collapsed.push_back(c);
  }
  return collapsed;
}

// Scrollable content height in device pixels. Layout geometry is CSS px and the
// MVP maps 1 CSS px = 1 device px, rounding like the box dump (std::lround).
int ContentHeightPx(const layout::BoxTree& boxes) {
  const float height = layout::ContentHeight(boxes);
  if (!(height > 0.0f)) {  // Also catches NaN.
    return 0;
  }
  const double rounded = std::round(static_cast<double>(height));
  if (rounded >= static_cast<double>(std::numeric_limits<int>::max())) {
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(rounded);
}

int MaxScrollPx(const layout::BoxTree& boxes, int viewport_height) {
  return std::max(0, ContentHeightPx(boxes) - viewport_height);
}

// Two URLs are the same page when only the fragment could differ.
bool SamePage(const net::Url& a, const net::Url& b) {
  return a.scheme == b.scheme && a.host == b.host && a.port == b.port && a.path == b.path &&
         a.query == b.query;
}

const dom::Attribute* FindAttribute(const dom::Node& node, std::string_view name) {
  for (const dom::Attribute& attribute : node.attributes) {
    if (attribute.name == name) {
      return &attribute;
    }
  }
  return nullptr;
}

// First <title> element in document order, or kInvalidNode. Iterative like the
// rest of the DOM traversal here: documents may be deeply nested.
dom::NodeId FindTitleElement(const dom::Document& document) {
  std::vector<dom::NodeId> pending;
  pending.push_back(document.root());
  while (!pending.empty()) {
    const dom::NodeId id = pending.back();
    pending.pop_back();
    const dom::Node& node = document.Get(id);
    if (node.type == dom::NodeType::kElement && node.name == "title") {
      return id;
    }
    for (dom::NodeId child = node.last_child; child != dom::kInvalidNode;
         child = document.Get(child).prev_sibling) {
      pending.push_back(child);
    }
  }
  return dom::kInvalidNode;
}

// Concatenates the text nodes under `root` in document order, excluding `root`
// itself (it is an element).
std::string TextContent(const dom::Document& document, dom::NodeId root) {
  std::string text;
  std::vector<dom::NodeId> pending;
  for (dom::NodeId child = document.Get(root).last_child; child != dom::kInvalidNode;
       child = document.Get(child).prev_sibling) {
    pending.push_back(child);
  }
  while (!pending.empty()) {
    const dom::NodeId id = pending.back();
    pending.pop_back();
    const dom::Node& node = document.Get(id);
    if (node.type == dom::NodeType::kText) {
      text += node.data;
    }
    for (dom::NodeId child = node.last_child; child != dom::kInvalidNode;
         child = document.Get(child).prev_sibling) {
      pending.push_back(child);
    }
  }
  return text;
}

// Nearest element at or above `node` with an href attribute, or kInvalidNode.
dom::NodeId FindLinkElement(const dom::Document& document, dom::NodeId node) {
  for (dom::NodeId current = node; current != dom::kInvalidNode;
       current = document.Get(current).parent) {
    const dom::Node& candidate = document.Get(current);
    if (candidate.type == dom::NodeType::kElement && FindAttribute(candidate, "href") != nullptr) {
      return current;
    }
  }
  return dom::kInvalidNode;
}

}  // namespace

Browser::Browser(int viewport_width, int viewport_height, std::shared_ptr<const Fonts> fonts)
    : viewport_width_(viewport_width), viewport_height_(viewport_height), fonts_(std::move(fonts)) {
  assert(viewport_width > 0 && viewport_height > 0);
}

Browser::~Browser() = default;

bool Browser::Load(const net::Url& url, std::string* error) {
  // The loader builds the whole page (fetch, parse, style, images, layout) before
  // it returns, so a failure leaves the current page, scroll offset and history
  // unchanged.
  auto page = std::make_unique<Page>();
  if (!LoadPage(url, viewport_width_, fonts_, page.get(), error)) {
    return false;
  }
  current_url_ = page->final_url;
  page_ = std::move(page);
  scroll_y_ = 0;
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool Browser::Open(std::string_view url, std::string* error) {
  std::string parse_error;
  const std::optional<net::Url> parsed = net::ParseUrl(url, &parse_error);
  if (!parsed) {
    if (error != nullptr) {
      *error = "cannot parse URL '" + std::string(url) + "': " + parse_error;
    }
    return false;
  }
  if (!Load(*parsed, error)) {
    return false;
  }
  // A new load after going back discards the forward entries.
  if (!history_.empty()) {
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(history_index_) + 1,
                   history_.end());
  }
  history_.push_back(current_url_);
  history_index_ = history_.size() - 1;
  return true;
}

bool Browser::Navigate(std::string_view href, std::string* error) {
  if (!page_) {
    if (error != nullptr) {
      *error = "cannot follow a link without a current page";
    }
    return false;
  }
  std::string resolve_error;
  const std::optional<net::Url> resolved = net::ResolveUrl(current_url_, href, &resolve_error);
  if (!resolved) {
    if (error != nullptr) {
      *error = "cannot resolve link '" + std::string(href) + "': " + resolve_error;
    }
    return false;
  }
  if (SamePage(*resolved, current_url_)) {
    if (error != nullptr) {
      error->clear();
    }
    return false;
  }
  // Reserializing keeps the single history path: Open parses, loads and records.
  return Open(net::SerializeUrl(*resolved), error);
}

bool Browser::Back(std::string* error) {
  if (history_index_ == 0) {
    if (error != nullptr) {
      error->clear();
    }
    return false;
  }
  const std::size_t target = history_index_ - 1;
  if (!Load(history_[target], error)) {
    return false;
  }
  history_index_ = target;
  return true;
}

bool Browser::Forward(std::string* error) {
  if (history_index_ + 1 >= history_.size()) {
    if (error != nullptr) {
      error->clear();
    }
    return false;
  }
  const std::size_t target = history_index_ + 1;
  if (!Load(history_[target], error)) {
    return false;
  }
  history_index_ = target;
  return true;
}

bool Browser::CanGoBack() const { return history_index_ > 0; }

bool Browser::CanGoForward() const { return history_index_ + 1 < history_.size(); }

bool Browser::Click(int x, int y, std::string* error) {
  if (!page_) {
    if (error != nullptr) {
      error->clear();
    }
    return false;
  }
  // Window device px -> page space: add the scroll offset back.
  const float page_x = static_cast<float>(x);
  const float page_y = static_cast<float>(static_cast<long long>(y) + scroll_y_);
  const layout::BoxId hit = layout::HitTest(page_->boxes, page_x, page_y);
  if (hit == layout::kInvalidBox) {
    if (error != nullptr) {
      error->clear();
    }
    return false;
  }
  // The hit box may be anonymous or a text box: walk up the box tree to a box
  // with a DOM origin, then up the DOM to the nearest element with href.
  dom::NodeId node = dom::kInvalidNode;
  for (layout::BoxId box_id = hit; box_id != layout::kInvalidBox;) {
    const layout::Box& box = page_->boxes.Get(box_id);
    if (box.node != dom::kInvalidNode) {
      node = box.node;
      break;
    }
    box_id = box.parent;
  }
  const dom::NodeId link =
      node == dom::kInvalidNode ? dom::kInvalidNode : FindLinkElement(page_->document, node);
  if (link == dom::kInvalidNode) {
    if (error != nullptr) {
      error->clear();
    }
    return false;
  }
  return Navigate(FindAttribute(page_->document.Get(link), "href")->value, error);
}

bool Browser::ScrollBy(int delta_device_px) {
  if (!page_) {
    return false;
  }
  const int max_scroll = MaxScrollPx(page_->boxes, viewport_height_);
  const long long target = static_cast<long long>(scroll_y_) + delta_device_px;
  const int clamped = static_cast<int>(std::clamp(target, 0LL, static_cast<long long>(max_scroll)));
  if (clamped == scroll_y_) {
    return false;
  }
  scroll_y_ = clamped;
  return true;
}

bool Browser::ScrollToTop() {
  if (!page_ || scroll_y_ == 0) {
    return false;
  }
  scroll_y_ = 0;
  return true;
}

bool Browser::ScrollToBottom() {
  if (!page_) {
    return false;
  }
  const int target = MaxScrollPx(page_->boxes, viewport_height_);
  if (target == scroll_y_) {
    return false;
  }
  scroll_y_ = target;
  return true;
}

void Browser::SetViewport(int width, int height) {
  assert(width > 0 && height > 0);
  viewport_width_ = width;
  viewport_height_ = height;
  Relayout();
  ClampScroll();
}

bool Browser::has_page() const { return page_ != nullptr; }

const net::Url& Browser::current_url() const { return current_url_; }

std::string Browser::title() const {
  if (!page_) {
    return {};
  }
  std::string text = CollapseAsciiWhitespace(ExtractedTitle());
  if (!text.empty()) {
    return text;
  }
  return net::SerializeUrl(current_url_);
}

int Browser::scroll_y() const { return scroll_y_; }

int Browser::content_height() const { return page_ ? ContentHeightPx(page_->boxes) : 0; }

int Browser::viewport_width() const { return viewport_width_; }

int Browser::viewport_height() const { return viewport_height_; }

void Browser::Paint(render::Surface& surface) const {
  surface.Fill(kPageColor);
  if (!page_) {
    return;
  }
  const paint::Viewport viewport{surface.width(), surface.height(), scroll_y_};
  // The runs provider binds the session font selection and the page language; a
  // null selection yields the empty provider, i.e. the frozen bitmap text path.
  paint::FontRunsProvider font_runs;
  if (fonts_) {
    font_runs = [fonts = fonts_, language = page_->language](const style::ComputedStyle& style,
                                                             std::string_view text_utf8) {
      return fonts->Runs(text_utf8, style, language);
    };
  }
  const render::DisplayList list = paint::BuildDisplayList(
      page_->document, page_->styles, page_->boxes, viewport, page_->images, font_runs);
  render::DrawDisplayList(list, surface);
}

void Browser::Relayout() {
  if (!page_) {
    return;
  }
  // Re-layout only: the DOM, styles and decoded images are reused, so a resize
  // never refetches the page or its images. The session's font selection is
  // applied again.
  RelayoutPage(page_.get(), viewport_width_, fonts_);
}

void Browser::ClampScroll() {
  if (!page_) {
    scroll_y_ = 0;
    return;
  }
  scroll_y_ = std::clamp(scroll_y_, 0, MaxScrollPx(page_->boxes, viewport_height_));
}

std::string Browser::ExtractedTitle() const {
  if (!page_) {
    return {};
  }
  const dom::NodeId title_id = FindTitleElement(page_->document);
  if (title_id == dom::kInvalidNode) {
    return {};
  }
  return TextContent(page_->document, title_id);
}

}  // namespace toy::shell
