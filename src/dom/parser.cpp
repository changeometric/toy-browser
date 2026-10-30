// Simplified HTML tree builder: turns tokenizer output into the arena DOM.
//
// The subset is deliberately small: a stack of open elements, a guaranteed
// html/head/body skeleton, and recovery instead of errors. There is no content
// model, no implied end tags other than at EOF, and no adoption agency.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tokenizer.h"
#include "toy/dom/node.h"

namespace toy::dom {
namespace {

constexpr NodeId kDocumentNode = 0;

bool IsVoidElement(std::string_view name) {
  static constexpr std::string_view kNames[] = {
      "area",  "base", "br",   "col",   "embed",  "hr",    "img",
      "input", "link", "meta", "param", "source", "track", "wbr",
  };
  for (std::string_view candidate : kNames) {
    if (candidate == name) {
      return true;
    }
  }
  return false;
}

bool IsWhitespaceOnly(std::string_view text) {
  for (const char character : text) {
    if (character != ' ' && character != '\t' && character != '\n' && character != '\r' &&
        character != '\f') {
      return false;
    }
  }
  return true;
}

class TreeBuilder {
 public:
  TreeBuilder() {
    nodes_.push_back(Node{});
    nodes_[kDocumentNode].type = NodeType::kDocument;
  }

  void Parse(std::string_view html) {
    Tokenizer tokenizer(html);
    for (Token token = tokenizer.Next(); token.type != TokenType::kEndOfFile;
         token = tokenizer.Next()) {
      Process(token);
    }
    Finish();
  }

  std::vector<Node> TakeNodes() { return std::move(nodes_); }

 private:
  NodeId AddNode(NodeType type, std::string name, std::string data,
                 const std::vector<Attribute>& attributes) {
    Node node;
    node.type = type;
    node.name = std::move(name);
    node.data = std::move(data);
    node.attributes = attributes;
    nodes_.push_back(std::move(node));
    return static_cast<NodeId>(nodes_.size() - 1);
  }

  void AppendChild(NodeId parent, NodeId child) {
    nodes_[child].parent = parent;
    nodes_[child].prev_sibling = nodes_[parent].last_child;
    nodes_[child].next_sibling = kInvalidNode;
    if (nodes_[parent].last_child != kInvalidNode) {
      nodes_[nodes_[parent].last_child].next_sibling = child;
    } else {
      nodes_[parent].first_child = child;
    }
    nodes_[parent].last_child = child;
  }

  void InsertBefore(NodeId parent, NodeId child, NodeId reference) {
    nodes_[child].parent = parent;
    nodes_[child].prev_sibling = nodes_[reference].prev_sibling;
    nodes_[child].next_sibling = reference;
    if (nodes_[reference].prev_sibling != kInvalidNode) {
      nodes_[nodes_[reference].prev_sibling].next_sibling = child;
    } else {
      nodes_[parent].first_child = child;
    }
    nodes_[reference].prev_sibling = child;
  }

  void InsertAfter(NodeId parent, NodeId child, NodeId reference) {
    nodes_[child].parent = parent;
    nodes_[child].prev_sibling = reference;
    nodes_[child].next_sibling = nodes_[reference].next_sibling;
    if (nodes_[reference].next_sibling != kInvalidNode) {
      nodes_[nodes_[reference].next_sibling].prev_sibling = child;
    } else {
      nodes_[parent].last_child = child;
    }
    nodes_[reference].next_sibling = child;
  }

  NodeId EnsureHtml() {
    if (html_id_ == kInvalidNode) {
      html_id_ = AddNode(NodeType::kElement, "html", "", {});
      AppendChild(kDocumentNode, html_id_);
    }
    return html_id_;
  }

  NodeId EnsureHead() {
    const NodeId html = EnsureHtml();
    if (head_id_ == kInvalidNode) {
      head_id_ = AddNode(NodeType::kElement, "head", "", {});
      // No body exists yet without a head, so appending keeps any comments
      // between <html> and <head> before the head element.
      AppendChild(html, head_id_);
    }
    return head_id_;
  }

  NodeId EnsureBody() {
    const NodeId html = EnsureHtml();
    EnsureHead();
    if (body_id_ == kInvalidNode) {
      body_id_ = AddNode(NodeType::kElement, "body", "", {});
      AppendChild(html, body_id_);
    }
    return body_id_;
  }

  // Inserts a doctype or comment that appeared before the html element. Such
  // nodes stay document children in source order, directly before html.
  void InsertPreHtmlNode(NodeId node) {
    if (html_id_ == kInvalidNode) {
      AppendChild(kDocumentNode, node);
      pre_html_tail_ = node;
      return;
    }
    if (pre_html_tail_ == kInvalidNode) {
      InsertBefore(kDocumentNode, node, html_id_);
    } else {
      InsertAfter(kDocumentNode, node, pre_html_tail_);
    }
    pre_html_tail_ = node;
  }

  bool HtmlStarted() const { return html_seen_ || skeleton_seen_ || content_started_; }

  void Process(const Token& token) {
    switch (token.type) {
      case TokenType::kDoctype:
        ProcessDoctype(token);
        break;
      case TokenType::kComment:
        ProcessComment(token);
        break;
      case TokenType::kText:
        ProcessText(token);
        break;
      case TokenType::kStartTag:
        ProcessStartTag(token);
        break;
      case TokenType::kEndTag:
        ProcessEndTag(token);
        break;
      case TokenType::kEndOfFile:
        break;
    }
  }

  void ProcessDoctype(const Token& token) {
    // A doctype is only meaningful before the html element; later ones are
    // dropped instead of polluting the body.
    if (HtmlStarted()) {
      return;
    }
    InsertPreHtmlNode(AddNode(NodeType::kDoctype, token.name, token.data, {}));
  }

  void ProcessComment(const Token& token) {
    const NodeId node = AddNode(NodeType::kComment, "", token.data, {});
    if (!open_elements_.empty()) {
      AppendChild(open_elements_.back(), node);
      return;
    }
    if (!HtmlStarted()) {
      InsertPreHtmlNode(node);
      return;
    }
    if (html_seen_) {
      // Reached only after </html>: late comments stay document children.
      AppendChild(kDocumentNode, node);
      return;
    }
    // The implicit body already started, so the comment belongs there.
    AppendChild(EnsureBody(), node);
  }

  void ProcessText(const Token& token) {
    if (token.data.empty()) {
      return;
    }
    if (open_elements_.empty()) {
      // Text before <html> (and after the document body closed) goes to body.
      // It does not end the pre-html phase, so a later doctype or comment
      // still becomes a document child before the html element.
      AppendChild(EnsureBody(), AddNode(NodeType::kText, "", token.data, {}));
      return;
    }
    NodeId parent = open_elements_.back();
    if (parent == html_id_) {
      if (!head_closed_ && IsWhitespaceOnly(token.data)) {
        // Formatting whitespace between <html> and <head> belongs to head, so
        // it does not start the implicit body early.
        AppendChild(EnsureHead(), AddNode(NodeType::kText, "", token.data, {}));
        return;
      }
      // Content directly inside html belongs to the implicit body.
      content_started_ = true;
      head_closed_ = true;
      parent = EnsureBody();
      open_elements_.push_back(parent);
    }
    AppendChild(parent, AddNode(NodeType::kText, "", token.data, {}));
  }

  void ProcessStartTag(const Token& token) {
    const std::string& name = token.name;
    if (name == "html") {
      ProcessHtmlTag(token);
      return;
    }
    if (name == "head") {
      ProcessHeadTag(token);
      return;
    }
    if (name == "body") {
      ProcessBodyTag(token);
      return;
    }
    const NodeId parent = InsertionParent();
    const NodeId element = AddNode(NodeType::kElement, name, "", token.attributes);
    AppendChild(parent, element);
    if (!IsVoidElement(name)) {
      open_elements_.push_back(element);
    }
  }

  // Where ordinary (non html/head/body) content is inserted. Content directly
  // inside html or at document level goes into the implicit body.
  NodeId InsertionParent() {
    if (open_elements_.empty()) {
      content_started_ = true;
      return EnsureBody();
    }
    if (open_elements_.back() == html_id_) {
      content_started_ = true;
      head_closed_ = true;
      const NodeId body = EnsureBody();
      open_elements_.push_back(body);
      return body;
    }
    return open_elements_.back();
  }

  void ProcessHtmlTag(const Token& token) {
    html_seen_ = true;
    if (html_id_ == kInvalidNode) {
      html_id_ = AddNode(NodeType::kElement, "html", "", token.attributes);
      AppendChild(kDocumentNode, html_id_);
    } else {
      nodes_[html_id_].attributes = token.attributes;
    }
    if (open_elements_.empty()) {
      open_elements_.push_back(html_id_);
    }
  }

  void ProcessHeadTag(const Token& token) {
    skeleton_seen_ = true;
    head_closed_ = false;
    const NodeId head = EnsureHead();
    nodes_[head].attributes = token.attributes;
    if (open_elements_.empty() || open_elements_.back() == html_id_) {
      open_elements_.push_back(head);
    }
  }

  void ProcessBodyTag(const Token& token) {
    skeleton_seen_ = true;
    head_closed_ = true;
    const NodeId body = EnsureBody();
    nodes_[body].attributes = token.attributes;
    if (open_elements_.empty() || open_elements_.back() == html_id_) {
      open_elements_.push_back(body);
    }
  }

  void ProcessEndTag(const Token& token) {
    if (IsVoidElement(token.name)) {
      return;  // Void elements never open, so their end tags are stray.
    }
    for (std::size_t index = open_elements_.size(); index-- > 0;) {
      if (nodes_[open_elements_[index]].name == token.name) {
        open_elements_.resize(index);
        if (token.name == "head") {
          head_closed_ = true;
        }
        return;
      }
    }
    // No matching open element: ignored.
  }

  void Finish() {
    EnsureHtml();
    EnsureHead();
    EnsureBody();
  }

  std::vector<Node> nodes_;
  std::vector<NodeId> open_elements_;
  NodeId html_id_ = kInvalidNode;
  NodeId head_id_ = kInvalidNode;
  NodeId body_id_ = kInvalidNode;
  NodeId pre_html_tail_ = kInvalidNode;
  bool html_seen_ = false;
  bool skeleton_seen_ = false;
  bool content_started_ = false;
  bool head_closed_ = false;
};

}  // namespace

Document ParseDocument(std::string_view html) {
  constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
  if (html.substr(0, kUtf8Bom.size()) == kUtf8Bom) {
    html.remove_prefix(kUtf8Bom.size());
  }
  TreeBuilder builder;
  builder.Parse(html);
  Document document;
  document.nodes_ = builder.TakeNodes();
  return document;
}

}  // namespace toy::dom
