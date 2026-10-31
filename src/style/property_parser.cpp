// Property value parsing and application for the M4 subset. Anything outside
// the subset is ignored: the computed style keeps whatever it had.

#include "property_parser.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "toy/css/stylesheet.h"
#include "toy/style/computed_style.h"

namespace toy::style::detail {
namespace {

bool IsAsciiDigit(char character) { return character >= '0' && character <= '9'; }

bool IsAsciiHexDigit(char character) {
  return IsAsciiDigit(character) || (character >= 'a' && character <= 'f') ||
         (character >= 'A' && character <= 'F');
}

bool IsAsciiWhitespace(char character) {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
         character == '\f';
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

std::string_view TrimAsciiWhitespace(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && IsAsciiWhitespace(text[begin])) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && IsAsciiWhitespace(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

// Splits on runs of ASCII whitespace, skipping leading/trailing whitespace.
std::vector<std::string_view> SplitWhitespace(std::string_view text) {
  std::vector<std::string_view> tokens;
  std::size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && IsAsciiWhitespace(text[index])) {
      ++index;
    }
    const std::size_t start = index;
    while (index < text.size() && !IsAsciiWhitespace(text[index])) {
      ++index;
    }
    if (index > start) {
      tokens.push_back(text.substr(start, index - start));
    }
  }
  return tokens;
}

std::uint32_t HexValue(char character) {
  if (character >= '0' && character <= '9') {
    return static_cast<std::uint32_t>(character - '0');
  }
  return static_cast<std::uint32_t>(ToLowerAscii(character) - 'a' + 10);
}

float ResolveLength(const Length& length, float em_base) {
  return length.in_em ? length.value * em_base : length.value;
}

bool ApplyLengthValue(std::string_view text, float em_base, float* target) {
  const std::optional<Length> length = ParseLength(text);
  if (!length) {
    return false;
  }
  *target = ResolveLength(*length, em_base);
  return true;
}

// Applies a 1-4 value margin/padding shorthand. Any unparsable value makes the
// whole declaration a no-op, as in CSS.
void ApplyBoxShorthand(std::string_view value, float em_base, float* top, float* right,
                       float* bottom, float* left) {
  const std::vector<std::string_view> tokens = SplitWhitespace(value);
  if (tokens.empty() || tokens.size() > 4) {
    return;
  }
  float resolved[4] = {0, 0, 0, 0};
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (!ApplyLengthValue(tokens[index], em_base, &resolved[index])) {
      return;
    }
  }
  switch (tokens.size()) {
    case 1:
      *top = resolved[0];
      *right = resolved[0];
      *bottom = resolved[0];
      *left = resolved[0];
      break;
    case 2:
      *top = resolved[0];
      *right = resolved[1];
      *bottom = resolved[0];
      *left = resolved[1];
      break;
    case 3:
      *top = resolved[0];
      *right = resolved[1];
      *bottom = resolved[2];
      *left = resolved[1];
      break;
    default:
      *top = resolved[0];
      *right = resolved[1];
      *bottom = resolved[2];
      *left = resolved[3];
      break;
  }
}

void ApplySizeValue(std::string_view value, float em_base, std::optional<float>* target) {
  if (ToLowerAscii(TrimAsciiWhitespace(value)) == "auto") {
    *target = std::nullopt;
    return;
  }
  float resolved = 0;
  if (ApplyLengthValue(value, em_base, &resolved)) {
    *target = resolved;
  }
}

void ApplyFontWeightValue(std::string_view value, ComputedStyle* style) {
  const std::string text = ToLowerAscii(TrimAsciiWhitespace(value));
  if (text == "normal") {
    style->font_weight = 400;
    return;
  }
  if (text == "bold") {
    style->font_weight = 700;
    return;
  }
  std::size_t index = 0;
  int weight = 0;
  bool any_digit = false;
  while (index < text.size() && IsAsciiDigit(text[index])) {
    weight = weight * 10 + (text[index] - '0');
    any_digit = true;
    ++index;
  }
  if (any_digit && index == text.size() && weight >= 100 && weight <= 900) {
    style->font_weight = weight;
  }
}

bool IsAsciiAlpha(char character) {
  return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

// Bytes of an unquoted font family name: ASCII letters/digits, '-', '_' and
// non-ASCII bytes (a UTF-8 encoded family name is kept as-is).
bool IsFontFamilyNameChar(char character) {
  return IsAsciiAlpha(character) || IsAsciiDigit(character) || character == '-' ||
         character == '_' || static_cast<unsigned char>(character) >= 0x80;
}

bool IsGenericFontFamily(std::string_view lower_name) {
  return lower_name == "serif" || lower_name == "sans-serif" || lower_name == "monospace" ||
         lower_name == "cursive" || lower_name == "fantasy" || lower_name == "system-ui";
}

// Parses one font-family value: a comma-separated list of family names. An item
// is either a quoted string (single or double quotes; a backslash escapes the
// next character) or an unquoted name made of IsFontFamilyNameChar bytes.
// Generic families are normalized to lowercase, other names keep their spelling.
// Any empty or invalid item invalidates the whole list, so the declaration is
// ignored as in CSS. The unsupported CSS-wide keywords (inherit, initial, unset)
// invalidate the declaration too; only a quoted "inherit" is an ordinary name.
std::optional<std::vector<std::string>> ParseFontFamilyValue(std::string_view value) {
  std::vector<std::string> families;
  std::size_t index = 0;
  while (true) {
    while (index < value.size() && IsAsciiWhitespace(value[index])) {
      ++index;
    }
    if (index >= value.size()) {
      return std::nullopt;  // Empty list or an empty item (including a trailing comma).
    }
    std::string family;
    if (value[index] == '"' || value[index] == '\'') {
      const char quote = value[index++];
      bool closed = false;
      while (index < value.size()) {
        const char character = value[index];
        if (character == '\\') {
          if (index + 1 >= value.size()) {
            return std::nullopt;  // A dangling escape is invalid.
          }
          family.push_back(value[index + 1]);
          index += 2;
          continue;
        }
        if (character == quote) {
          ++index;
          closed = true;
          break;
        }
        family.push_back(character);
        ++index;
      }
      if (!closed) {
        return std::nullopt;
      }
    } else {
      const std::size_t start = index;
      while (index < value.size() && IsFontFamilyNameChar(value[index])) {
        ++index;
      }
      if (index == start) {
        return std::nullopt;  // An item must start with a name byte or a quote.
      }
      const std::string_view name = value.substr(start, index - start);
      const std::string lower_name = ToLowerAscii(name);
      if (lower_name == "inherit" || lower_name == "initial" || lower_name == "unset") {
        return std::nullopt;
      }
      family = IsGenericFontFamily(lower_name) ? lower_name : std::string(name);
    }
    if (family.empty()) {
      return std::nullopt;  // An empty family name is invalid.
    }
    families.push_back(std::move(family));
    while (index < value.size() && IsAsciiWhitespace(value[index])) {
      ++index;
    }
    if (index >= value.size()) {
      return families;
    }
    if (value[index] != ',') {
      return std::nullopt;  // Garbage after an item (e.g. an unquoted space).
    }
    ++index;  // Consume the comma; the next item is parsed by the next iteration.
  }
}

void ApplyBackgroundColorValue(std::string_view value, ComputedStyle* style) {
  if (ToLowerAscii(TrimAsciiWhitespace(value)) == "transparent") {
    style->background_color = 0x00000000u;
    return;
  }
  const std::optional<std::uint32_t> color = ParseColor(value);
  if (color) {
    style->background_color = 0xFF000000u | *color;
  }
}

}  // namespace

std::optional<Length> ParseLength(std::string_view text) {
  const std::string_view trimmed = TrimAsciiWhitespace(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  std::size_t index = 0;
  bool negative = false;
  if (trimmed[index] == '-' || trimmed[index] == '+') {
    negative = trimmed[index] == '-';
    ++index;
  }
  double magnitude = 0.0;
  bool any_digit = false;
  while (index < trimmed.size() && IsAsciiDigit(trimmed[index])) {
    magnitude = magnitude * 10.0 + (trimmed[index] - '0');
    any_digit = true;
    ++index;
  }
  if (index < trimmed.size() && trimmed[index] == '.') {
    ++index;
    double scale = 0.1;
    while (index < trimmed.size() && IsAsciiDigit(trimmed[index])) {
      magnitude += (trimmed[index] - '0') * scale;
      scale *= 0.1;
      any_digit = true;
      ++index;
    }
  }
  if (!any_digit) {
    return std::nullopt;
  }
  const double value = negative ? -magnitude : magnitude;
  if (index == trimmed.size()) {
    // Unitless zero is the only allowed unitless length.
    if (magnitude != 0.0) {
      return std::nullopt;
    }
    return Length{0.0f, false};
  }
  const std::string unit = ToLowerAscii(trimmed.substr(index));
  if (unit == "px") {
    return Length{static_cast<float>(value), false};
  }
  if (unit == "em") {
    return Length{static_cast<float>(value), true};
  }
  return std::nullopt;
}

std::optional<std::uint32_t> ParseColor(std::string_view text) {
  const std::string value = ToLowerAscii(TrimAsciiWhitespace(text));
  if (value == "black") {
    return 0x000000u;
  }
  if (value == "white") {
    return 0xFFFFFFu;
  }
  if (value == "red") {
    return 0xFF0000u;
  }
  if (value == "green") {
    return 0x008000u;
  }
  if (value == "blue") {
    return 0x0000FFu;
  }
  if (value == "gray" || value == "grey") {
    return 0x808080u;
  }
  if (value.size() == 4 && value[0] == '#') {
    if (!IsAsciiHexDigit(value[1]) || !IsAsciiHexDigit(value[2]) || !IsAsciiHexDigit(value[3])) {
      return std::nullopt;
    }
    const std::uint32_t red = HexValue(value[1]);
    const std::uint32_t green = HexValue(value[2]);
    const std::uint32_t blue = HexValue(value[3]);
    return ((red * 17u) << 16) | ((green * 17u) << 8) | (blue * 17u);
  }
  if (value.size() == 7 && value[0] == '#') {
    std::uint32_t channels[3] = {0, 0, 0};
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const char high = value[1 + channel * 2];
      const char low = value[2 + channel * 2];
      if (!IsAsciiHexDigit(high) || !IsAsciiHexDigit(low)) {
        return std::nullopt;
      }
      channels[channel] = (HexValue(high) << 4) | HexValue(low);
    }
    return (channels[0] << 16) | (channels[1] << 8) | channels[2];
  }
  return std::nullopt;
}

void ApplyFontSizeDeclaration(const css::Declaration& declaration, float parent_font_size_px,
                              ComputedStyle* style) {
  if (declaration.property != "font-size") {
    return;
  }
  const std::optional<Length> length = ParseLength(declaration.value);
  if (!length) {
    return;
  }
  style->font_size_px = ResolveLength(*length, parent_font_size_px);
}

void ApplyDeclaration(const css::Declaration& declaration, ComputedStyle* style) {
  const std::string_view property = declaration.property;
  if (property == "display") {
    const std::string value = ToLowerAscii(TrimAsciiWhitespace(declaration.value));
    if (value == "block") {
      style->display = Display::kBlock;
    } else if (value == "inline") {
      style->display = Display::kInline;
    } else if (value == "none") {
      style->display = Display::kNone;
    }
    return;
  }
  if (property == "color") {
    const std::optional<std::uint32_t> color = ParseColor(declaration.value);
    if (color) {
      style->color = *color;
    }
    return;
  }
  if (property == "background-color") {
    ApplyBackgroundColorValue(declaration.value, style);
    return;
  }
  if (property == "font-weight") {
    ApplyFontWeightValue(declaration.value, style);
    return;
  }
  if (property == "font-style") {
    const std::string value = ToLowerAscii(TrimAsciiWhitespace(declaration.value));
    if (value == "normal") {
      style->font_style = FontStyle::kNormal;
    } else if (value == "italic") {
      style->font_style = FontStyle::kItalic;
    }
    return;
  }
  if (property == "font-family") {
    const std::optional<std::vector<std::string>> families =
        ParseFontFamilyValue(declaration.value);
    if (families) {
      style->font_family = *families;
    }
    return;
  }
  if (property == "margin") {
    ApplyBoxShorthand(declaration.value, style->font_size_px, &style->margin_top,
                      &style->margin_right, &style->margin_bottom, &style->margin_left);
    return;
  }
  if (property == "padding") {
    ApplyBoxShorthand(declaration.value, style->font_size_px, &style->padding_top,
                      &style->padding_right, &style->padding_bottom, &style->padding_left);
    return;
  }
  if (property == "margin-top") {
    ApplyLengthValue(declaration.value, style->font_size_px, &style->margin_top);
    return;
  }
  if (property == "margin-right") {
    ApplyLengthValue(declaration.value, style->font_size_px, &style->margin_right);
    return;
  }
  if (property == "margin-bottom") {
    ApplyLengthValue(declaration.value, style->font_size_px, &style->margin_bottom);
    return;
  }
  if (property == "margin-left") {
    ApplyLengthValue(declaration.value, style->font_size_px, &style->margin_left);
    return;
  }
  if (property == "padding-top") {
    ApplyLengthValue(declaration.value, style->font_size_px, &style->padding_top);
    return;
  }
  if (property == "padding-right") {
    ApplyLengthValue(declaration.value, style->font_size_px, &style->padding_right);
    return;
  }
  if (property == "padding-bottom") {
    ApplyLengthValue(declaration.value, style->font_size_px, &style->padding_bottom);
    return;
  }
  if (property == "padding-left") {
    ApplyLengthValue(declaration.value, style->font_size_px, &style->padding_left);
    return;
  }
  if (property == "width") {
    ApplySizeValue(declaration.value, style->font_size_px, &style->width);
    return;
  }
  if (property == "height") {
    ApplySizeValue(declaration.value, style->font_size_px, &style->height);
    return;
  }
  // Everything else is outside the M4 subset and ignored.
}

}  // namespace toy::style::detail
