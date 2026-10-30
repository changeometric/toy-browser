// CSS scanner and parser for the M4 subset: style rules, declarations and
// simple selectors. Parsing is error-tolerant and never fails on bad input.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "toy/css/stylesheet.h"

namespace toy::css {
namespace {

constexpr std::size_t kNpos = std::string_view::npos;

bool IsWhitespace(char character) {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
         character == '\f';
}

bool IsAsciiAlpha(char character) {
  return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

bool IsAsciiDigit(char character) { return character >= '0' && character <= '9'; }

bool IsIdentStart(char character) {
  return IsAsciiAlpha(character) || character == '_' || character == '-' ||
         static_cast<unsigned char>(character) >= 0x80;
}

bool IsIdentChar(char character) { return IsIdentStart(character) || IsAsciiDigit(character); }

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

std::string_view Trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && IsWhitespace(text[begin])) {
    ++begin;
  }
  while (end > begin && IsWhitespace(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

// Trims the text and collapses every internal whitespace run to one space.
std::string CollapseWhitespace(std::string_view text) {
  std::string result;
  bool pending_space = false;
  for (const char character : text) {
    if (IsWhitespace(character)) {
      pending_space = true;
      continue;
    }
    if (pending_space && !result.empty()) {
      result.push_back(' ');
    }
    pending_space = false;
    result.push_back(character);
  }
  return result;
}

std::size_t SkipWhitespace(std::string_view text, std::size_t position) {
  while (position < text.size() && IsWhitespace(text[position])) {
    ++position;
  }
  return position;
}

// Returns the position just after the quoted string that starts at `position`,
// or text.size() when the string is unterminated.
std::size_t SkipString(std::string_view text, std::size_t position) {
  const char quote = text[position];
  ++position;
  while (position < text.size()) {
    if (text[position] == '\\' && position + 1 < text.size()) {
      position += 2;
      continue;
    }
    if (text[position] == quote) {
      return position + 1;
    }
    ++position;
  }
  return text.size();
}

// Copies `text` with every /* ... */ comment replaced by one space. Quoted
// strings are copied verbatim so comment markers inside them survive, and an
// unterminated comment runs to the end of the input.
std::string RemoveComments(std::string_view text) {
  std::string cleaned;
  cleaned.reserve(text.size());
  std::size_t position = 0;
  while (position < text.size()) {
    const char character = text[position];
    if (character == '"' || character == '\'') {
      const std::size_t string_end = SkipString(text, position);
      cleaned.append(text.substr(position, string_end - position));
      position = string_end;
      continue;
    }
    if (character == '/' && position + 1 < text.size() && text[position + 1] == '*') {
      position += 2;
      while (position + 1 < text.size() && !(text[position] == '*' && text[position + 1] == '/')) {
        ++position;
      }
      if (position + 1 < text.size()) {
        position += 2;
      } else {
        position = text.size();
      }
      cleaned.push_back(' ');
      continue;
    }
    cleaned.push_back(character);
    ++position;
  }
  return cleaned;
}

// Splits on top-level `separator` characters, ignoring separators inside
// quoted strings, parentheses, brackets and braces.
std::vector<std::string_view> SplitTopLevel(std::string_view text, char separator) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  int parentheses = 0;
  int brackets = 0;
  int braces = 0;
  for (std::size_t position = 0; position < text.size(); ++position) {
    const char character = text[position];
    if (character == '"' || character == '\'') {
      position = SkipString(text, position) - 1;
      continue;
    }
    if (character == '(') {
      ++parentheses;
    } else if (character == ')') {
      if (parentheses > 0) {
        --parentheses;
      }
    } else if (character == '[') {
      ++brackets;
    } else if (character == ']') {
      if (brackets > 0) {
        --brackets;
      }
    } else if (character == '{') {
      ++braces;
    } else if (character == '}') {
      if (braces > 0) {
        --braces;
      }
    } else if (character == separator && parentheses == 0 && brackets == 0 && braces == 0) {
      parts.push_back(text.substr(start, position - start));
      start = position + 1;
    }
  }
  parts.push_back(text.substr(start));
  return parts;
}

// Finds the first top-level '{' or '}' at or after `start`. Sets `is_block_start`
// to true for '{' and false for '}', and returns its position or kNpos.
std::size_t FindNextBlockDelimiter(std::string_view text, std::size_t start, bool* is_block_start) {
  int parentheses = 0;
  int brackets = 0;
  for (std::size_t position = start; position < text.size(); ++position) {
    const char character = text[position];
    if (character == '"' || character == '\'') {
      position = SkipString(text, position) - 1;
      continue;
    }
    if (character == '(') {
      ++parentheses;
      continue;
    }
    if (character == ')') {
      if (parentheses > 0) {
        --parentheses;
      }
      continue;
    }
    if (character == '[') {
      ++brackets;
      continue;
    }
    if (character == ']') {
      if (brackets > 0) {
        --brackets;
      }
      continue;
    }
    if (parentheses != 0 || brackets != 0) {
      continue;
    }
    if (character == '{') {
      *is_block_start = true;
      return position;
    }
    if (character == '}') {
      *is_block_start = false;
      return position;
    }
  }
  return kNpos;
}

// Returns the position just after the '}' that closes the block opened at
// `open_position`, or text.size() when the block is unterminated.
std::size_t SkipBalancedBlock(std::string_view text, std::size_t open_position, bool* closed) {
  std::size_t depth = 0;
  std::size_t position = open_position;
  while (position < text.size()) {
    const char character = text[position];
    if (character == '"' || character == '\'') {
      position = SkipString(text, position);
      continue;
    }
    if (character == '{') {
      ++depth;
      ++position;
      continue;
    }
    if (character == '}') {
      --depth;
      ++position;
      if (depth == 0) {
        *closed = true;
        return position;
      }
      continue;
    }
    ++position;
  }
  *closed = false;
  return text.size();
}

// Skips an at-rule that starts at `position` ('@'): everything up to its
// terminating ';' or the end of its balanced block.
std::size_t SkipAtRule(std::string_view text, std::size_t position) {
  ++position;
  while (position < text.size() && !IsWhitespace(text[position]) && text[position] != ';' &&
         text[position] != '{' && text[position] != '}') {
    ++position;
  }
  int parentheses = 0;
  int brackets = 0;
  while (position < text.size()) {
    const char character = text[position];
    if (character == '"' || character == '\'') {
      position = SkipString(text, position);
      continue;
    }
    if (character == '(') {
      ++parentheses;
    } else if (character == ')') {
      if (parentheses > 0) {
        --parentheses;
      }
    } else if (character == '[') {
      ++brackets;
    } else if (character == ']') {
      if (brackets > 0) {
        --brackets;
      }
    } else if (parentheses == 0 && brackets == 0) {
      if (character == ';') {
        return position + 1;
      }
      if (character == '{') {
        bool closed = false;
        return SkipBalancedBlock(text, position, &closed);
      }
      if (character == '}') {
        return position;
      }
    }
    ++position;
  }
  return text.size();
}

// Finds the ':' that separates a declaration property from its value, ignoring
// colons inside quoted strings, parentheses and brackets.
std::size_t FindDeclarationColon(std::string_view text) {
  int parentheses = 0;
  int brackets = 0;
  for (std::size_t position = 0; position < text.size(); ++position) {
    const char character = text[position];
    if (character == '"' || character == '\'') {
      position = SkipString(text, position) - 1;
      continue;
    }
    if (character == '(') {
      ++parentheses;
      continue;
    }
    if (character == ')') {
      if (parentheses > 0) {
        --parentheses;
      }
      continue;
    }
    if (character == '[') {
      ++brackets;
      continue;
    }
    if (character == ']') {
      if (brackets > 0) {
        --brackets;
      }
      continue;
    }
    if (character == ':' && parentheses == 0 && brackets == 0) {
      return position;
    }
  }
  return kNpos;
}

std::vector<Declaration> ParseDeclarationText(std::string_view text) {
  std::vector<Declaration> declarations;
  for (const std::string_view raw_chunk : SplitTopLevel(text, ';')) {
    const std::string_view chunk = Trim(raw_chunk);
    if (chunk.empty()) {
      continue;
    }
    const std::size_t colon = FindDeclarationColon(chunk);
    if (colon == kNpos) {
      continue;
    }
    const std::string_view property_text = Trim(chunk.substr(0, colon));
    if (property_text.empty()) {
      continue;
    }
    std::string value = CollapseWhitespace(chunk.substr(colon + 1));
    bool important = false;
    const std::size_t bang = value.rfind('!');
    if (bang != std::string::npos) {
      const std::string_view suffix = Trim(std::string_view(value).substr(bang + 1));
      if (EqualsIgnoreCase(suffix, "important")) {
        important = true;
        value = CollapseWhitespace(std::string_view(value).substr(0, bang));
      }
    }
    Declaration declaration;
    declaration.property = ToLowerAscii(property_text);
    declaration.value = std::move(value);
    declaration.important = important;
    declarations.push_back(std::move(declaration));
  }
  return declarations;
}

std::string ReadIdent(std::string_view text, std::size_t* position) {
  const std::size_t start = *position;
  ++*position;
  while (*position < text.size() && IsIdentChar(text[*position])) {
    ++*position;
  }
  return std::string(text.substr(start, *position - start));
}

// Parses one compound selector ("div.a#b", "*", ".a") starting at *position.
bool ParseCompoundSelector(std::string_view text, std::size_t* position,
                           CompoundSelector* compound) {
  std::size_t current = *position;
  CompoundSelector result;
  if (text[current] == '*') {
    result.parts.push_back(SimpleSelector{SimpleKind::kUniversal, std::string()});
    ++current;
  } else if (IsIdentStart(text[current])) {
    std::string name = ReadIdent(text, &current);
    result.parts.push_back(SimpleSelector{SimpleKind::kType, std::move(name)});
  }
  while (current < text.size() && (text[current] == '.' || text[current] == '#')) {
    const bool is_class = text[current] == '.';
    ++current;
    if (current >= text.size() || !IsIdentStart(text[current])) {
      return false;
    }
    std::string name = ReadIdent(text, &current);
    result.parts.push_back(
        SimpleSelector{is_class ? SimpleKind::kClass : SimpleKind::kId, std::move(name)});
  }
  if (result.parts.empty()) {
    return false;
  }
  // Only whitespace, '>' and ',' may follow a compound selector.
  if (current < text.size() && !IsWhitespace(text[current]) && text[current] != '>' &&
      text[current] != ',') {
    return false;
  }
  *position = current;
  *compound = std::move(result);
  return true;
}

bool ParseComplexSelector(std::string_view text, ComplexSelector* selector) {
  std::size_t position = SkipWhitespace(text, 0);
  if (position >= text.size()) {
    return false;
  }
  ComplexSelector result;
  bool has_pending_combinator = false;
  Combinator pending_combinator = Combinator::kDescendant;
  while (position < text.size()) {
    CompoundSelector compound;
    if (!ParseCompoundSelector(text, &position, &compound)) {
      return false;
    }
    if (!result.compounds.empty()) {
      if (!has_pending_combinator) {
        return false;
      }
      result.combinators.push_back(pending_combinator);
    }
    result.compounds.push_back(std::move(compound));

    const std::size_t after_compound = position;
    position = SkipWhitespace(text, position);
    const bool had_space = position > after_compound;
    if (position >= text.size()) {
      break;
    }
    if (text[position] == '>') {
      ++position;
      position = SkipWhitespace(text, position);
      if (position >= text.size() || text[position] == '>' || text[position] == ',') {
        return false;
      }
      pending_combinator = Combinator::kChild;
      has_pending_combinator = true;
      continue;
    }
    if (!had_space) {
      return false;
    }
    pending_combinator = Combinator::kDescendant;
    has_pending_combinator = true;
  }
  if (result.compounds.empty()) {
    return false;
  }
  *selector = std::move(result);
  return true;
}

Selector ParseSelectorText(std::string_view text) {
  Selector selector;
  for (const std::string_view raw_part : SplitTopLevel(text, ',')) {
    const std::string_view part = Trim(raw_part);
    if (part.empty()) {
      continue;
    }
    ComplexSelector complex;
    if (ParseComplexSelector(part, &complex)) {
      selector.alternatives.push_back(std::move(complex));
    }
  }
  return selector;
}

}  // namespace

std::vector<Declaration> ParseDeclarationList(std::string_view text) {
  const std::string cleaned = RemoveComments(text);
  return ParseDeclarationText(cleaned);
}

StyleSheet ParseStyleSheet(std::string_view css) {
  const std::string cleaned = RemoveComments(css);
  const std::string_view source(cleaned);
  StyleSheet sheet;
  std::size_t position = 0;
  while (position < source.size()) {
    position = SkipWhitespace(source, position);
    if (position >= source.size()) {
      break;
    }
    if (source[position] == '@') {
      position = SkipAtRule(source, position);
      continue;
    }
    if (source[position] == '}' || source[position] == ';') {
      ++position;
      continue;
    }
    bool is_block_start = false;
    const std::size_t delimiter = FindNextBlockDelimiter(source, position, &is_block_start);
    if (delimiter == kNpos) {
      break;
    }
    if (!is_block_start) {
      // Stray closing brace before any block: drop the text and move on.
      position = delimiter + 1;
      continue;
    }
    const std::string_view prelude = source.substr(position, delimiter - position);
    bool closed = false;
    const std::size_t block_end = SkipBalancedBlock(source, delimiter, &closed);
    const std::size_t content_begin = delimiter + 1;
    std::size_t content_end = closed ? block_end - 1 : block_end;
    if (content_end < content_begin) {
      content_end = content_begin;
    }
    const std::string_view block = source.substr(content_begin, content_end - content_begin);

    Rule rule;
    rule.selector = ParseSelectorText(prelude);
    if (!rule.selector.alternatives.empty()) {
      rule.declarations = ParseDeclarationText(block);
      sheet.rules.push_back(std::move(rule));
    }
    position = block_end;
  }
  return sheet;
}

}  // namespace toy::css
