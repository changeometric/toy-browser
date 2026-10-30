// HTML tokenizer for the M3 subset: elements, attributes, comments, doctype,
// raw text (script/style) and RCDATA (title/textarea) content, plus the small
// entity set the rest of the module understands.

#include "tokenizer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toy::dom {
namespace {

bool IsAsciiAlpha(char character) {
  return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

bool IsAsciiDigit(char character) { return character >= '0' && character <= '9'; }

bool IsHexDigit(char character) {
  return IsAsciiDigit(character) || (character >= 'a' && character <= 'f') ||
         (character >= 'A' && character <= 'F');
}

bool IsAsciiWhitespace(char character) {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
         character == '\f';
}

// Characters that may appear in a tag name. The closing '>' terminates it.
bool IsTagNameChar(char character) {
  return !IsAsciiWhitespace(character) && character != '>' && character != '/' && character != '<';
}

char ToLowerAscii(char character) {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<char>(character - 'A' + 'a');
  }
  return character;
}

std::string ToLowerAscii(std::string_view text) {
  std::string result(text);
  for (char& character : result) {
    character = ToLowerAscii(character);
  }
  return result;
}

bool EqualsIgnoreCase(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (ToLowerAscii(left[index]) != ToLowerAscii(right[index])) {
      return false;
    }
  }
  return true;
}

std::string TrimAsciiWhitespace(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && IsAsciiWhitespace(text[begin])) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && IsAsciiWhitespace(text[end - 1])) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::string FirstTokenLowercase(std::string_view text) {
  std::size_t end = 0;
  while (end < text.size() && !IsAsciiWhitespace(text[end])) {
    ++end;
  }
  return ToLowerAscii(text.substr(0, end));
}

struct NamedEntity {
  std::string_view text;
  std::string_view replacement;
};

// The complete MVP entity set. Names are case-sensitive and the terminating
// semicolon is required; anything else stays literal text.
constexpr NamedEntity kNamedEntities[] = {
    {"&amp;", "&"},   {"&lt;", "<"},   {"&gt;", ">"},
    {"&quot;", "\""}, {"&apos;", "'"}, {"&nbsp;", "\xC2\xA0"},
};

bool IsValidCodepoint(std::uint32_t value) {
  return value != 0 && value <= 0x10FFFFu && !(value >= 0xD800u && value <= 0xDFFFu);
}

void AppendUtf8(std::uint32_t value, std::string* output) {
  if (value <= 0x7Fu) {
    output->push_back(static_cast<char>(value));
    return;
  }
  if (value <= 0x7FFu) {
    output->push_back(static_cast<char>(0xC0u | (value >> 6)));
    output->push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    return;
  }
  if (value <= 0xFFFFu) {
    output->push_back(static_cast<char>(0xE0u | (value >> 12)));
    output->push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3Fu)));
    output->push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    return;
  }
  output->push_back(static_cast<char>(0xF0u | (value >> 18)));
  output->push_back(static_cast<char>(0x80u | ((value >> 12) & 0x3Fu)));
  output->push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3Fu)));
  output->push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
}

std::uint32_t HexValue(char character) {
  if (character >= '0' && character <= '9') {
    return static_cast<std::uint32_t>(character - '0');
  }
  return static_cast<std::uint32_t>(ToLowerAscii(character) - 'a' + 10);
}

// Decodes the MVP entity subset. Unknown names and invalid numeric references
// are kept as literal text, one byte at a time.
std::string DecodeEntities(std::string_view raw) {
  std::string decoded;
  decoded.reserve(raw.size());
  std::size_t index = 0;
  while (index < raw.size()) {
    if (raw[index] != '&') {
      decoded.push_back(raw[index]);
      ++index;
      continue;
    }
    bool replaced = false;
    for (const NamedEntity& entity : kNamedEntities) {
      if (raw.compare(index, entity.text.size(), entity.text) == 0) {
        decoded.append(entity.replacement);
        index += entity.text.size();
        replaced = true;
        break;
      }
    }
    if (replaced) {
      continue;
    }
    if (index + 1 < raw.size() && raw[index + 1] == '#') {
      std::size_t cursor = index + 2;
      const bool hexadecimal = cursor < raw.size() && raw[cursor] == 'x';
      if (hexadecimal) {
        ++cursor;
      }
      const std::size_t digits_start = cursor;
      const std::uint32_t base = hexadecimal ? 16u : 10u;
      std::uint32_t value = 0;
      bool too_large = false;
      while (cursor < raw.size() &&
             (hexadecimal ? IsHexDigit(raw[cursor]) : IsAsciiDigit(raw[cursor]))) {
        const std::uint32_t digit =
            hexadecimal ? HexValue(raw[cursor]) : static_cast<std::uint32_t>(raw[cursor] - '0');
        if (!too_large) {
          if (value > (0x10FFFFu - digit) / base) {
            too_large = true;
          } else {
            value = value * base + digit;
          }
        }
        ++cursor;
      }
      if (!too_large && cursor > digits_start && cursor < raw.size() && raw[cursor] == ';' &&
          IsValidCodepoint(value)) {
        AppendUtf8(value, &decoded);
        index = cursor + 1;
        continue;
      }
    }
    decoded.push_back('&');
    ++index;
  }
  return decoded;
}

}  // namespace

Tokenizer::Tokenizer(std::string_view input) : input_(input) {}

bool Tokenizer::StartsComment() const {
  return std::string_view(input_.substr(position_, 4)) == "<!--";
}

bool Tokenizer::StartsDoctype() const {
  constexpr std::string_view kKeyword = "doctype";
  const std::size_t keyword_end = position_ + 2 + kKeyword.size();
  if (keyword_end > input_.size()) {
    return false;
  }
  if (input_[position_] != '<' || input_[position_ + 1] != '!') {
    return false;
  }
  if (!EqualsIgnoreCase(input_.substr(position_ + 2, kKeyword.size()), kKeyword)) {
    return false;
  }
  return keyword_end == input_.size() || input_[keyword_end] == '>' ||
         IsAsciiWhitespace(input_[keyword_end]);
}

bool Tokenizer::StartsEndTag() const {
  return position_ + 2 < input_.size() && input_[position_] == '<' &&
         input_[position_ + 1] == '/' && IsAsciiAlpha(input_[position_ + 2]);
}

bool Tokenizer::StartsStartTag() const {
  return position_ + 1 < input_.size() && input_[position_] == '<' &&
         IsAsciiAlpha(input_[position_ + 1]);
}

Token Tokenizer::FlushText() {
  Token token;
  token.type = TokenType::kText;
  token.data = DecodeEntities(pending_text_);
  pending_text_.clear();
  return token;
}

Token Tokenizer::ParseComment() {
  const std::size_t content_start = position_ + 4;
  const std::size_t close = input_.find("-->", content_start);
  Token token;
  token.type = TokenType::kComment;
  if (close == std::string_view::npos) {
    token.data = std::string(input_.substr(content_start));
    position_ = input_.size();
  } else {
    token.data = std::string(input_.substr(content_start, close - content_start));
    position_ = close + 3;
  }
  return token;
}

Token Tokenizer::ParseDoctype() {
  constexpr std::size_t kKeywordLength = 2 + 7;  // "<!" + "doctype"
  const std::size_t content_start = position_ + kKeywordLength;
  const std::size_t close = input_.find('>', content_start);
  std::size_t content_end = input_.size();
  if (close == std::string_view::npos) {
    position_ = input_.size();
  } else {
    content_end = close;
    position_ = close + 1;
  }
  Token token;
  token.type = TokenType::kDoctype;
  token.data = TrimAsciiWhitespace(input_.substr(content_start, content_end - content_start));
  token.name = FirstTokenLowercase(token.data);
  return token;
}

Token Tokenizer::ParseEndTag() {
  position_ += 2;  // '<' and '/'
  const std::size_t name_start = position_;
  while (position_ < input_.size() && IsTagNameChar(input_[position_])) {
    ++position_;
  }
  Token token;
  token.type = TokenType::kEndTag;
  token.name = ToLowerAscii(input_.substr(name_start, position_ - name_start));
  while (position_ < input_.size() && input_[position_] != '>') {
    ++position_;
  }
  if (position_ < input_.size()) {
    ++position_;
  }
  return token;
}

Token Tokenizer::ParseStartTag() {
  ++position_;  // '<'
  const std::size_t name_start = position_;
  while (position_ < input_.size() && IsTagNameChar(input_[position_])) {
    ++position_;
  }
  Token token;
  token.type = TokenType::kStartTag;
  token.name = ToLowerAscii(input_.substr(name_start, position_ - name_start));
  ParseAttributes(&token.attributes);
  if (token.name == "script" || token.name == "style" || token.name == "title" ||
      token.name == "textarea") {
    raw_text_tag_ = token.name;
  }
  return token;
}

std::string Tokenizer::ParseAttributeValue() {
  if (position_ < input_.size() && (input_[position_] == '"' || input_[position_] == '\'')) {
    const char quote = input_[position_];
    ++position_;
    const std::size_t value_start = position_;
    while (position_ < input_.size() && input_[position_] != quote) {
      ++position_;
    }
    const std::string value = DecodeEntities(input_.substr(value_start, position_ - value_start));
    if (position_ < input_.size()) {
      ++position_;  // closing quote
    }
    return value;
  }
  const std::size_t value_start = position_;
  while (position_ < input_.size()) {
    const char character = input_[position_];
    if (IsAsciiWhitespace(character) || character == '>' || character == '<') {
      break;
    }
    ++position_;
  }
  std::string_view raw = input_.substr(value_start, position_ - value_start);
  // A trailing '/' immediately before '>' is ignored, not part of the value.
  if (!raw.empty() && raw.back() == '/' && position_ < input_.size() && input_[position_] == '>') {
    raw.remove_suffix(1);
  }
  return DecodeEntities(raw);
}

void Tokenizer::ParseAttributes(std::vector<Attribute>* attributes) {
  for (;;) {
    while (position_ < input_.size() && IsAsciiWhitespace(input_[position_])) {
      ++position_;
    }
    if (position_ >= input_.size()) {
      return;
    }
    const char lookahead = input_[position_];
    if (lookahead == '>') {
      ++position_;
      return;
    }
    if (lookahead == '/') {
      if (position_ + 1 < input_.size() && input_[position_ + 1] == '>') {
        position_ += 2;
        return;
      }
      ++position_;
      continue;
    }
    if (lookahead == '<') {
      // Malformed markup; skip the character so the loop always advances.
      ++position_;
      continue;
    }
    const std::size_t name_start = position_;
    while (position_ < input_.size()) {
      const char character = input_[position_];
      if (IsAsciiWhitespace(character) || character == '=' || character == '>' ||
          character == '/' || character == '<') {
        break;
      }
      ++position_;
    }
    if (position_ == name_start) {
      // A stray '=' or similar; skip it so the loop always advances.
      ++position_;
      continue;
    }
    const std::string name = ToLowerAscii(input_.substr(name_start, position_ - name_start));
    while (position_ < input_.size() && IsAsciiWhitespace(input_[position_])) {
      ++position_;
    }
    std::string value;
    if (position_ < input_.size() && input_[position_] == '=') {
      ++position_;
      while (position_ < input_.size() && IsAsciiWhitespace(input_[position_])) {
        ++position_;
      }
      value = ParseAttributeValue();
    }
    bool duplicate = false;
    for (const Attribute& existing : *attributes) {
      if (existing.name == name) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      Attribute attribute;
      attribute.name = name;
      attribute.value = std::move(value);
      attributes->push_back(std::move(attribute));
    }
  }
}

Token Tokenizer::NextRawText() {
  const std::string tag = raw_text_tag_;
  const bool rcdata = tag == "title" || tag == "textarea";
  bool found = false;
  std::size_t end_tag_start = input_.size();
  std::size_t after_end_tag = input_.size();
  for (std::size_t index = position_; index + 2 + tag.size() <= input_.size(); ++index) {
    if (input_[index] != '<' || input_[index + 1] != '/') {
      continue;
    }
    if (!EqualsIgnoreCase(input_.substr(index + 2, tag.size()), tag)) {
      continue;
    }
    const std::size_t terminator_index = index + 2 + tag.size();
    if (terminator_index < input_.size()) {
      const char terminator = input_[terminator_index];
      if (terminator != '>' && terminator != '/' && !IsAsciiWhitespace(terminator)) {
        continue;
      }
    }
    const std::size_t close = input_.find('>', terminator_index);
    if (close == std::string_view::npos) {
      continue;
    }
    end_tag_start = index;
    after_end_tag = close + 1;
    found = true;
    break;
  }
  const std::string_view raw_content = input_.substr(position_, end_tag_start - position_);
  position_ = found ? after_end_tag : input_.size();
  raw_text_tag_.clear();
  std::string content = rcdata ? DecodeEntities(raw_content) : std::string(raw_content);
  if (!content.empty()) {
    if (found) {
      pending_end_tag_ = true;
      pending_end_tag_name_ = tag;
    }
    Token token;
    token.type = TokenType::kText;
    token.data = std::move(content);
    return token;
  }
  if (found) {
    Token token;
    token.type = TokenType::kEndTag;
    token.name = tag;
    return token;
  }
  return Token{};
}

Token Tokenizer::Next() {
  if (pending_end_tag_) {
    pending_end_tag_ = false;
    Token token;
    token.type = TokenType::kEndTag;
    token.name = pending_end_tag_name_;
    pending_end_tag_name_.clear();
    return token;
  }
  if (!raw_text_tag_.empty()) {
    return NextRawText();
  }
  for (;;) {
    if (position_ >= input_.size()) {
      if (!pending_text_.empty()) {
        return FlushText();
      }
      return Token{};
    }
    if (input_[position_] != '<') {
      pending_text_.push_back(input_[position_]);
      ++position_;
      continue;
    }
    if (StartsComment()) {
      if (!pending_text_.empty()) {
        return FlushText();
      }
      return ParseComment();
    }
    if (StartsDoctype()) {
      if (!pending_text_.empty()) {
        return FlushText();
      }
      return ParseDoctype();
    }
    if (StartsEndTag()) {
      if (!pending_text_.empty()) {
        return FlushText();
      }
      return ParseEndTag();
    }
    if (StartsStartTag()) {
      if (!pending_text_.empty()) {
        return FlushText();
      }
      return ParseStartTag();
    }
    // A '<' that begins none of the supported constructs is plain text.
    pending_text_.push_back('<');
    ++position_;
  }
}

}  // namespace toy::dom
