#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "toy/dom/node.h"

namespace toy::dom {

// Token kinds produced by Tokenizer.
enum class TokenType { kDoctype, kStartTag, kEndTag, kText, kComment, kEndOfFile };

// One HTML token. Fields depend on the kind:
// - kDoctype: name = lowercase doctype name, data = raw trimmed content.
// - kStartTag / kEndTag: name = lowercase tag name. Attributes exist only for
//   start tags; their names are lowercase and their values entity-decoded.
// - kText: data = entity-decoded text (verbatim for raw text elements).
// - kComment: data = comment content, verbatim (no entity decoding).
struct Token {
  TokenType type = TokenType::kEndOfFile;
  std::string name;
  std::string data;
  std::vector<Attribute> attributes;
};

// A small, error-tolerant tokenizer for the M3 parsing subset. It never fails:
// malformed markup turns into text or comment tokens instead of errors.
// Raw text (script/style) and RCDATA (title/textarea) content is consumed
// immediately after their start tag, so the parser always sees a plain
// start tag / text / end tag sequence.
class Tokenizer {
 public:
  explicit Tokenizer(std::string_view input);

  // Returns the next token; kEndOfFile forever once the input is exhausted.
  Token Next();

 private:
  bool StartsComment() const;
  bool StartsDoctype() const;
  bool StartsEndTag() const;
  bool StartsStartTag() const;

  Token ParseComment();
  Token ParseDoctype();
  Token ParseEndTag();
  Token ParseStartTag();
  std::string ParseAttributeValue();
  void ParseAttributes(std::vector<Attribute>* attributes);
  Token NextRawText();
  Token FlushText();

  std::string_view input_;
  std::size_t position_ = 0;
  // Raw text characters seen so far but not yet emitted as a text token.
  std::string pending_text_;
  // Set when the last start tag switched the tokenizer into raw text or RCDATA
  // mode; holds the tag name that terminates the content.
  std::string raw_text_tag_;
  // Set when raw text content was emitted and its end tag is still owed.
  bool pending_end_tag_ = false;
  std::string pending_end_tag_name_;
};

}  // namespace toy::dom
