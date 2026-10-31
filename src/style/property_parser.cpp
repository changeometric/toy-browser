// Property value parsing and application. Anything outside the supported
// subset is ignored: the computed style keeps whatever it had.

#include "property_parser.h"

#include <cmath>
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

bool IsAsciiAlpha(char character) {
  return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
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

// Splits on top-level ASCII whitespace, treating a `func(...)` run (parentheses
// at any nesting depth, including quoted strings) as one token. Used by the
// background shorthand, where `url(...)` and `rgb(...)` must not be split.
std::vector<std::string_view> SplitTopLevelTokens(std::string_view text) {
  std::vector<std::string_view> tokens;
  std::size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && IsAsciiWhitespace(text[index])) {
      ++index;
    }
    if (index >= text.size()) {
      break;
    }
    const std::size_t start = index;
    int depth = 0;
    char quote = 0;
    while (index < text.size()) {
      const char character = text[index];
      if (quote != 0) {
        if (character == '\\' && index + 1 < text.size()) {
          index += 2;
          continue;
        }
        if (character == quote) {
          quote = 0;
        }
        ++index;
        continue;
      }
      if (character == '"' || character == '\'') {
        quote = character;
        ++index;
        continue;
      }
      if (character == '(') {
        ++depth;
      } else if (character == ')') {
        if (depth > 0) {
          --depth;
        }
      } else if (depth == 0 && IsAsciiWhitespace(character)) {
        break;
      }
      ++index;
    }
    tokens.push_back(text.substr(start, index - start));
  }
  return tokens;
}

// Parses an optional sign, digits and an optional fractional part at `*index`.
// On success writes the value and advances `*index` past the number.
bool ParseNumberAt(std::string_view text, std::size_t* index, float* value) {
  std::size_t position = *index;
  if (position >= text.size()) {
    return false;
  }
  bool negative = false;
  if (text[position] == '-' || text[position] == '+') {
    negative = text[position] == '-';
    ++position;
  }
  double magnitude = 0.0;
  bool any_digit = false;
  while (position < text.size() && IsAsciiDigit(text[position])) {
    magnitude = magnitude * 10.0 + (text[position] - '0');
    any_digit = true;
    ++position;
  }
  if (position < text.size() && text[position] == '.') {
    ++position;
    double scale = 0.1;
    while (position < text.size() && IsAsciiDigit(text[position])) {
      magnitude += (text[position] - '0') * scale;
      scale *= 0.1;
      any_digit = true;
      ++position;
    }
  }
  if (!any_digit) {
    return false;
  }
  *value = static_cast<float>(negative ? -magnitude : magnitude);
  *index = position;
  return true;
}

std::uint32_t HexValue(char character) {
  if (character >= '0' && character <= '9') {
    return static_cast<std::uint32_t>(character - '0');
  }
  return static_cast<std::uint32_t>(ToLowerAscii(character) - 'a' + 10);
}

// Rounds a 0..255 channel and clamps it into range.
std::uint32_t ClampByte(double value) {
  if (!(value > 0.0)) {  // Also handles NaN.
    return 0;
  }
  if (value >= 255.0) {
    return 255;
  }
  return static_cast<std::uint32_t>(std::lround(value));
}

// A color component token: a number, optionally followed by a trailing '%'.
struct ColorComponent {
  float value = 0.0f;
  bool percent = false;
};

std::optional<ColorComponent> ParseColorComponent(std::string_view token) {
  const std::string_view trimmed = TrimAsciiWhitespace(token);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  std::size_t index = 0;
  ColorComponent component;
  if (!ParseNumberAt(trimmed, &index, &component.value)) {
    return std::nullopt;
  }
  if (index < trimmed.size() && trimmed[index] == '%') {
    component.percent = true;
    ++index;
  }
  if (index != trimmed.size()) {
    return std::nullopt;
  }
  return component;
}

// rgb()/hsl() channel: a 0..255 byte, from an integer/float or a percentage.
std::optional<std::uint32_t> ParseChannelByte(std::string_view token) {
  const std::optional<ColorComponent> component = ParseColorComponent(token);
  if (!component) {
    return std::nullopt;
  }
  const double value = component->percent
                           ? static_cast<double>(component->value) * 255.0 / 100.0
                           : static_cast<double>(component->value);
  return ClampByte(value);
}

// Alpha: 0..1 number, 0..255 number, or a percentage. Legacy comma-syntax
// alpha is a 0..1 number; the 0..255 form is accepted as documented.
std::optional<std::uint32_t> ParseAlphaByte(std::string_view token) {
  const std::optional<ColorComponent> component = ParseColorComponent(token);
  if (!component) {
    return std::nullopt;
  }
  double alpha = 0.0;
  if (component->percent) {
    alpha = static_cast<double>(component->value) / 100.0;
  } else if (component->value <= 1.0f) {
    alpha = static_cast<double>(component->value);
  } else {
    alpha = static_cast<double>(component->value) / 255.0;
  }
  if (!(alpha > 0.0)) {
    return 0;
  }
  if (alpha >= 1.0) {
    return 255;
  }
  return static_cast<std::uint32_t>(std::lround(alpha * 255.0));
}

// Hue in degrees: a plain number or a number with a "deg" suffix.
std::optional<float> ParseHue(std::string_view token) {
  std::string text = ToLowerAscii(TrimAsciiWhitespace(token));
  if (text.size() > 3 && text.compare(text.size() - 3, 3, "deg") == 0) {
    text.resize(text.size() - 3);
  }
  std::size_t index = 0;
  float hue = 0.0f;
  if (!ParseNumberAt(text, &index, &hue) || index != text.size()) {
    return std::nullopt;
  }
  return hue;
}

// Saturation/lightness: a percentage, or a plain number interpreted as a
// percentage (tolerant legacy-syntax handling). Clamped to 0..100.
std::optional<double> ParsePercentComponent(std::string_view token) {
  const std::optional<ColorComponent> component = ParseColorComponent(token);
  if (!component) {
    return std::nullopt;
  }
  double value = static_cast<double>(component->value);
  if (!(value > 0.0)) {
    return 0.0;
  }
  if (value > 100.0) {
    return 100.0;
  }
  return value;
}

// Splits the inside of a color function on commas, trimming each argument.
bool SplitCommaArguments(std::string_view inner, std::vector<std::string_view>* arguments) {
  std::size_t start = 0;
  while (true) {
    const std::size_t comma = inner.find(',', start);
    const std::string_view part =
        TrimAsciiWhitespace(inner.substr(start, comma == std::string_view::npos
                                                    ? std::string_view::npos
                                                    : comma - start));
    if (part.empty()) {
      return false;
    }
    arguments->push_back(part);
    if (comma == std::string_view::npos) {
      return true;
    }
    start = comma + 1;
  }
}

// If `value` is exactly `<name>(<inner>)`, writes <inner>. `value` must already
// be trimmed and lowercased.
bool MatchColorFunction(std::string_view value, std::string_view name, std::string_view* inner) {
  const std::size_t prefix = name.size() + 1;
  if (value.size() <= prefix || value.back() != ')' || value.compare(0, name.size(), name) != 0 ||
      value[name.size()] != '(') {
    return false;
  }
  *inner = value.substr(prefix, value.size() - prefix - 1);
  return true;
}

std::optional<std::uint32_t> ParseRgbFunction(std::string_view value) {
  std::string_view inner;
  if (!MatchColorFunction(value, "rgb", &inner) && !MatchColorFunction(value, "rgba", &inner)) {
    return std::nullopt;
  }
  std::vector<std::string_view> arguments;
  if (!SplitCommaArguments(inner, &arguments)) {
    return std::nullopt;
  }
  if (arguments.size() != 3 && arguments.size() != 4) {
    return std::nullopt;
  }
  std::uint32_t channels[3] = {0, 0, 0};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    const std::optional<std::uint32_t> byte = ParseChannelByte(arguments[channel]);
    if (!byte) {
      return std::nullopt;
    }
    channels[channel] = *byte;
  }
  std::uint32_t alpha = 255;
  if (arguments.size() == 4) {
    const std::optional<std::uint32_t> byte = ParseAlphaByte(arguments[3]);
    if (!byte) {
      return std::nullopt;
    }
    alpha = *byte;
  }
  return (alpha << 24) | (channels[0] << 16) | (channels[1] << 8) | channels[2];
}

std::optional<std::uint32_t> ParseHslFunction(std::string_view value) {
  std::string_view inner;
  if (!MatchColorFunction(value, "hsl", &inner) && !MatchColorFunction(value, "hsla", &inner)) {
    return std::nullopt;
  }
  std::vector<std::string_view> arguments;
  if (!SplitCommaArguments(inner, &arguments)) {
    return std::nullopt;
  }
  if (arguments.size() != 3 && arguments.size() != 4) {
    return std::nullopt;
  }
  const std::optional<float> hue = ParseHue(arguments[0]);
  const std::optional<double> saturation = ParsePercentComponent(arguments[1]);
  const std::optional<double> lightness = ParsePercentComponent(arguments[2]);
  if (!hue || !saturation || !lightness) {
    return std::nullopt;
  }
  std::uint32_t alpha = 255;
  if (arguments.size() == 4) {
    const std::optional<std::uint32_t> byte = ParseAlphaByte(arguments[3]);
    if (!byte) {
      return std::nullopt;
    }
    alpha = *byte;
  }
  // Standard HSL -> RGB conversion (CSS Color 3).
  double hue_degrees = std::fmod(static_cast<double>(*hue), 360.0);
  if (hue_degrees < 0.0) {
    hue_degrees += 360.0;
  }
  const double s = *saturation / 100.0;
  const double l = *lightness / 100.0;
  const double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
  const double h_prime = hue_degrees / 60.0;
  const double x = c * (1.0 - std::fabs(std::fmod(h_prime, 2.0) - 1.0));
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  const int sector = static_cast<int>(h_prime) % 6;
  switch (sector) {
    case 0:
      red = c;
      green = x;
      break;
    case 1:
      red = x;
      green = c;
      break;
    case 2:
      green = c;
      blue = x;
      break;
    case 3:
      green = x;
      blue = c;
      break;
    case 4:
      red = x;
      blue = c;
      break;
    default:
      red = c;
      blue = x;
      break;
  }
  const double m = l - c / 2.0;
  const std::uint32_t red_byte = ClampByte((red + m) * 255.0);
  const std::uint32_t green_byte = ClampByte((green + m) * 255.0);
  const std::uint32_t blue_byte = ClampByte((blue + m) * 255.0);
  return (alpha << 24) | (red_byte << 16) | (green_byte << 8) | blue_byte;
}

struct NamedColor {
  std::string_view name;
  std::uint32_t rgb;
};

// The CSS Color Level 4 named-color set (the 148 keywords, excluding the
// `transparent` keyword which is handled separately) and the legacy `grey`
// spellings, which are part of the same set.
constexpr NamedColor kNamedColors[] = {
    {"aliceblue", 0xF0F8FFu},      {"antiquewhite", 0xFAEBD7u},   {"aqua", 0x00FFFFu},
    {"aquamarine", 0x7FFFD4u},     {"azure", 0xF0FFFFu},          {"beige", 0xF5F5DCu},
    {"bisque", 0xFFE4C4u},         {"black", 0x000000u},          {"blanchedalmond", 0xFFEBCDu},
    {"blue", 0x0000FFu},           {"blueviolet", 0x8A2BE2u},     {"brown", 0xA52A2Au},
    {"burlywood", 0xDEB887u},      {"cadetblue", 0x5F9EA0u},      {"chartreuse", 0x7FFF00u},
    {"chocolate", 0xD2691Eu},      {"coral", 0xFF7F50u},          {"cornflowerblue", 0x6495EDu},
    {"cornsilk", 0xFFF8DCu},       {"crimson", 0xDC143Cu},        {"cyan", 0x00FFFFu},
    {"darkblue", 0x00008Bu},       {"darkcyan", 0x008B8Bu},       {"darkgoldenrod", 0xB8860Bu},
    {"darkgray", 0xA9A9A9u},       {"darkgreen", 0x006400u},      {"darkgrey", 0xA9A9A9u},
    {"darkkhaki", 0xBDB76Bu},      {"darkmagenta", 0x8B008Bu},    {"darkolivegreen", 0x556B2Fu},
    {"darkorange", 0xFF8C00u},     {"darkorchid", 0x9932CCu},     {"darkred", 0x8B0000u},
    {"darksalmon", 0xE9967Au},     {"darkseagreen", 0x8FBC8Fu},   {"darkslateblue", 0x483D8Bu},
    {"darkslategray", 0x2F4F4Fu},  {"darkslategrey", 0x2F4F4Fu},  {"darkturquoise", 0x00CED1u},
    {"darkviolet", 0x9400D3u},     {"deeppink", 0xFF1493u},       {"deepskyblue", 0x00BFFFu},
    {"dimgray", 0x696969u},        {"dimgrey", 0x696969u},        {"dodgerblue", 0x1E90FFu},
    {"firebrick", 0xB22222u},      {"floralwhite", 0xFFFAF0u},    {"forestgreen", 0x228B22u},
    {"fuchsia", 0xFF00FFu},        {"gainsboro", 0xDCDCDCu},      {"ghostwhite", 0xF8F8FFu},
    {"gold", 0xFFD700u},           {"goldenrod", 0xDAA520u},      {"gray", 0x808080u},
    {"green", 0x008000u},          {"greenyellow", 0xADFF2Fu},    {"grey", 0x808080u},
    {"honeydew", 0xF0FFF0u},       {"hotpink", 0xFF69B4u},        {"indianred", 0xCD5C5Cu},
    {"indigo", 0x4B0082u},         {"ivory", 0xFFFFF0u},          {"khaki", 0xF0E68Cu},
    {"lavender", 0xE6E6FAu},       {"lavenderblush", 0xFFF0F5u},  {"lawngreen", 0x7CFC00u},
    {"lemonchiffon", 0xFFFACDu},   {"lightblue", 0xADD8E6u},      {"lightcoral", 0xF08080u},
    {"lightcyan", 0xE0FFFFu},      {"lightgoldenrodyellow", 0xFAFAD2u},
    {"lightgray", 0xD3D3D3u},      {"lightgreen", 0x90EE90u},     {"lightgrey", 0xD3D3D3u},
    {"lightpink", 0xFFB6C1u},      {"lightsalmon", 0xFFA07Au},    {"lightseagreen", 0x20B2AAu},
    {"lightskyblue", 0x87CEFAu},   {"lightslategray", 0x778899u}, {"lightslategrey", 0x778899u},
    {"lightsteelblue", 0xB0C4DEu}, {"lightyellow", 0xFFFFE0u},    {"lime", 0x00FF00u},
    {"limegreen", 0x32CD32u},      {"linen", 0xFAF0E6u},          {"magenta", 0xFF00FFu},
    {"maroon", 0x800000u},         {"mediumaquamarine", 0x66CDAAu},
    {"mediumblue", 0x0000CDu},     {"mediumorchid", 0xBA55D3u},   {"mediumpurple", 0x9370DBu},
    {"mediumseagreen", 0x3CB371u}, {"mediumslateblue", 0x7B68EEu},
    {"mediumspringgreen", 0x00FA9Au}, {"mediumturquoise", 0x48D1CCu},
    {"mediumvioletred", 0xC71585u}, {"midnightblue", 0x191970u},  {"mintcream", 0xF5FFFAu},
    {"mistyrose", 0xFFE4E1u},      {"moccasin", 0xFFE4B5u},       {"navajowhite", 0xFFDEADu},
    {"navy", 0x000080u},           {"oldlace", 0xFDF5E6u},        {"olive", 0x808000u},
    {"olivedrab", 0x6B8E23u},      {"orange", 0xFFA500u},         {"orangered", 0xFF4500u},
    {"orchid", 0xDA70D6u},         {"palegoldenrod", 0xEEE8AAu},  {"palegreen", 0x98FB98u},
    {"paleturquoise", 0xAFEEEEu},  {"palevioletred", 0xDB7093u},  {"papayawhip", 0xFFEFD5u},
    {"peachpuff", 0xFFDAB9u},      {"peru", 0xCD853Fu},           {"pink", 0xFFC0CBu},
    {"plum", 0xDDA0DDu},           {"powderblue", 0xB0E0E6u},     {"purple", 0x800080u},
    {"rebeccapurple", 0x663399u},  {"red", 0xFF0000u},            {"rosybrown", 0xBC8F8Fu},
    {"royalblue", 0x4169E1u},      {"saddlebrown", 0x8B4513u},    {"salmon", 0xFA8072u},
    {"sandybrown", 0xF4A460u},     {"seagreen", 0x2E8B57u},       {"seashell", 0xFFF5EEu},
    {"sienna", 0xA0522Du},         {"silver", 0xC0C0C0u},         {"skyblue", 0x87CEEBu},
    {"slateblue", 0x6A5ACDu},      {"slategray", 0x708090u},      {"slategrey", 0x708090u},
    {"snow", 0xFFFAFAu},           {"springgreen", 0x00FF7Fu},    {"steelblue", 0x4682B4u},
    {"tan", 0xD2B48Cu},            {"teal", 0x008080u},           {"thistle", 0xD8BFD8u},
    {"tomato", 0xFF6347u},         {"turquoise", 0x40E0D0u},      {"violet", 0xEE82EEu},
    {"wheat", 0xF5DEB3u},          {"white", 0xFFFFFFu},          {"whitesmoke", 0xF5F5F5u},
    {"yellow", 0xFFFF00u},         {"yellowgreen", 0x9ACD32u},
};

std::optional<std::uint32_t> ParseHexColor(std::string_view value) {
  const std::size_t digits = value.size() - 1;
  if (digits != 3 && digits != 4 && digits != 6 && digits != 8) {
    return std::nullopt;
  }
  for (std::size_t index = 1; index < value.size(); ++index) {
    if (!IsAsciiHexDigit(value[index])) {
      return std::nullopt;
    }
  }
  std::uint32_t channels[4] = {0, 0, 0, 255};
  if (digits == 3 || digits == 4) {
    for (std::size_t channel = 0; channel < digits; ++channel) {
      channels[channel] = HexValue(value[1 + channel]) * 17;
    }
  } else {
    const std::size_t channel_count = digits / 2;
    for (std::size_t channel = 0; channel < channel_count; ++channel) {
      channels[channel] =
          (HexValue(value[1 + channel * 2]) << 4) | HexValue(value[2 + channel * 2]);
    }
  }
  return (channels[3] << 24) | (channels[0] << 16) | (channels[1] << 8) | channels[2];
}

// ---- Font families (M10; unchanged) ---------------------------------------

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

// ---- Value application helpers --------------------------------------------

// A computed length is either px or an unresolved percentage; detail::Length
// has already resolved em/rem against the relevant font size.
style::Length ToComputedLength(const ParsedLength& length, float em_base_px,
                               float rem_base_px) {
  style::Length result;
  switch (length.unit) {
    case ParsedLength::Unit::kPx:
      result.value = length.value;
      result.unit = style::Length::Unit::kPx;
      break;
    case ParsedLength::Unit::kEm:
      result.value = length.value * em_base_px;
      result.unit = style::Length::Unit::kPx;
      break;
    case ParsedLength::Unit::kRem:
      result.value = length.value * rem_base_px;
      result.unit = style::Length::Unit::kPx;
      break;
    case ParsedLength::Unit::kPercent:
      result.value = length.value;
      result.unit = style::Length::Unit::kPercent;
      break;
  }
  return result;
}

bool ApplyLengthValue(std::string_view text, float em_base_px, float rem_base_px,
                      style::Length* target) {
  const std::optional<ParsedLength> length = ParseLength(text);
  if (!length) {
    return false;
  }
  *target = ToComputedLength(*length, em_base_px, rem_base_px);
  return true;
}

bool ApplyAutoLengthValue(std::string_view text, float em_base_px, float rem_base_px,
                          AutoLength* target) {
  if (ToLowerAscii(TrimAsciiWhitespace(text)) == "auto") {
    target->is_auto = true;
    target->length = style::Length{};
    return true;
  }
  style::Length length;
  if (!ApplyLengthValue(text, em_base_px, rem_base_px, &length)) {
    return false;
  }
  target->is_auto = false;
  target->length = length;
  return true;
}

// width/height: `auto` means std::nullopt, otherwise a Length (percent kept).
bool ApplySizeValue(std::string_view text, float em_base_px, float rem_base_px,
                    std::optional<style::Length>* target) {
  if (ToLowerAscii(TrimAsciiWhitespace(text)) == "auto") {
    *target = std::nullopt;
    return true;
  }
  style::Length length;
  if (!ApplyLengthValue(text, em_base_px, rem_base_px, &length)) {
    return false;
  }
  *target = length;
  return true;
}

// min-*/max-*: `none` removes the constraint, otherwise a Length (percent kept).
bool ApplyConstraintValue(std::string_view text, float em_base_px, float rem_base_px,
                          std::optional<style::Length>* target) {
  if (ToLowerAscii(TrimAsciiWhitespace(text)) == "none") {
    *target = std::nullopt;
    return true;
  }
  style::Length length;
  if (!ApplyLengthValue(text, em_base_px, rem_base_px, &length)) {
    return false;
  }
  *target = length;
  return true;
}

// The side (0=top, 1=right, 2=bottom, 3=left) whose value a 1..4 token box
// shorthand uses.
std::size_t BoxShorthandSide(std::size_t token_count, std::size_t side) {
  switch (token_count) {
    case 1:
      return 0;
    case 2:
      return side % 2;
    case 3:
      return side == 3 ? 1 : side;
    default:
      return side;
  }
}

void ApplyPaddingShorthand(std::string_view value, float em_base_px, float rem_base_px,
                           ComputedStyle* computed) {
  const std::vector<std::string_view> tokens = SplitWhitespace(value);
  if (tokens.empty() || tokens.size() > 4) {
    return;
  }
  style::Length resolved[4];
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    style::Length length;
    if (!ApplyLengthValue(tokens[index], em_base_px, rem_base_px, &length)) {
      return;
    }
    resolved[index] = length;
  }
  style::Length* sides[4] = {&computed->padding_top, &computed->padding_right,
                             &computed->padding_bottom, &computed->padding_left};
  for (std::size_t side = 0; side < 4; ++side) {
    *sides[side] = resolved[BoxShorthandSide(tokens.size(), side)];
  }
}

void ApplyMarginShorthand(std::string_view value, float em_base_px, float rem_base_px,
                          ComputedStyle* computed) {
  const std::vector<std::string_view> tokens = SplitWhitespace(value);
  if (tokens.empty() || tokens.size() > 4) {
    return;
  }
  AutoLength resolved[4];
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (!ApplyAutoLengthValue(tokens[index], em_base_px, rem_base_px, &resolved[index])) {
      return;
    }
  }
  AutoLength* sides[4] = {&computed->margin_top, &computed->margin_right,
                          &computed->margin_bottom, &computed->margin_left};
  for (std::size_t side = 0; side < 4; ++side) {
    *sides[side] = resolved[BoxShorthandSide(tokens.size(), side)];
  }
}

// The logical block/inline shorthands: one value sets both ends, two values are
// start then end.
void ApplyLogicalMarginShorthand(std::string_view value, float em_base_px, float rem_base_px,
                                 AutoLength* start, AutoLength* end) {
  const std::vector<std::string_view> tokens = SplitWhitespace(value);
  if (tokens.empty() || tokens.size() > 2) {
    return;
  }
  AutoLength resolved[2];
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (!ApplyAutoLengthValue(tokens[index], em_base_px, rem_base_px, &resolved[index])) {
      return;
    }
  }
  *start = resolved[0];
  *end = tokens.size() == 2 ? resolved[1] : resolved[0];
}

void ApplyLogicalPaddingShorthand(std::string_view value, float em_base_px, float rem_base_px,
                                  style::Length* start, style::Length* end) {
  const std::vector<std::string_view> tokens = SplitWhitespace(value);
  if (tokens.empty() || tokens.size() > 2) {
    return;
  }
  style::Length resolved[2];
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (!ApplyLengthValue(tokens[index], em_base_px, rem_base_px, &resolved[index])) {
      return;
    }
  }
  *start = resolved[0];
  *end = tokens.size() == 2 ? resolved[1] : resolved[0];
}

void ApplyLineHeightValue(std::string_view value, float own_font_size_px,
                          ComputedStyle* style) {
  const std::string_view trimmed = TrimAsciiWhitespace(value);
  if (ToLowerAscii(trimmed) == "normal") {
    style->line_height = LineHeight{LineHeight::Kind::kNormal, 0.0f};
    return;
  }
  // A unitless number is a font-size multiplier (the `0` also matches here).
  if (const std::optional<float> number = ParseNumber(trimmed)) {
    if (*number < 0.0f) {
      return;
    }
    style->line_height = LineHeight{LineHeight::Kind::kNumber, *number};
    return;
  }
  const std::optional<ParsedLength> length = ParseLength(trimmed);
  if (!length || length->value < 0.0f) {
    return;
  }
  switch (length->unit) {
    case ParsedLength::Unit::kPx:
      style->line_height = LineHeight{LineHeight::Kind::kPx, length->value};
      break;
    case ParsedLength::Unit::kEm:
      style->line_height = LineHeight{LineHeight::Kind::kPx,
                                      length->value * own_font_size_px};
      break;
    case ParsedLength::Unit::kPercent:
      style->line_height = LineHeight{LineHeight::Kind::kNumber, length->value / 100.0f};
      break;
    case ParsedLength::Unit::kRem:
      // Not part of the M14a line-height contract; treat as an unknown value.
      break;
  }
}

void ApplyBackgroundColorValue(std::string_view value, ComputedStyle* style) {
  const std::optional<std::uint32_t> color = ParseColor(value);
  if (color) {
    style->background_color = *color;
  }
}

// The `background` shorthand: only the color component is supported. Tokens
// such as `url(...)`, positions and repeat values are ignored; the first token
// that parses as a color sets background-color. A value with no color component
// leaves the background unchanged (documented simplification).
void ApplyBackgroundShorthandValue(std::string_view value, ComputedStyle* style) {
  for (const std::string_view token : SplitTopLevelTokens(value)) {
    const std::optional<std::uint32_t> color = ParseColor(token);
    if (color) {
      style->background_color = *color;
      return;
    }
  }
}

// `none`, `underline`, `line-through` and `overline` are the decoration line
// values; other shorthand components (style, color) are ignored. Only the
// underline line is represented in M14a. A `none` combined with another
// decoration is invalid CSS and ignored; a value with no decoration-line token
// at all is ignored too.
void ApplyTextDecorationValue(std::string_view value, ComputedStyle* style) {
  const std::vector<std::string_view> tokens = SplitWhitespace(value);
  if (tokens.empty()) {
    return;
  }
  bool has_none = false;
  bool has_underline = false;
  bool has_other_decoration = false;
  bool has_decoration_line = false;
  for (const std::string_view token : tokens) {
    const std::string lower = ToLowerAscii(token);
    if (lower == "none") {
      has_none = true;
      has_decoration_line = true;
    } else if (lower == "underline") {
      has_underline = true;
      has_decoration_line = true;
    } else if (lower == "overline" || lower == "line-through" || lower == "blink") {
      has_other_decoration = true;
      has_decoration_line = true;
    }
    // Any other token is an unsupported shorthand component (style or color)
    // and does not affect the decoration line.
  }
  if (!has_decoration_line) {
    return;  // No decoration line at all: leave the inherited value alone.
  }
  if (has_none && (has_underline || has_other_decoration)) {
    return;  // `none` cannot be combined with a decoration.
  }
  style->text_decoration_underline = has_underline;
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

}  // namespace

std::optional<ParsedLength> ParseLength(std::string_view text) {
  const std::string_view trimmed = TrimAsciiWhitespace(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  std::size_t index = 0;
  float value = 0.0f;
  if (!ParseNumberAt(trimmed, &index, &value)) {
    return std::nullopt;
  }
  if (index == trimmed.size()) {
    // Unitless zero is the only allowed unitless length.
    if (value != 0.0f) {
      return std::nullopt;
    }
    return ParsedLength{0.0f, ParsedLength::Unit::kPx};
  }
  const std::string unit = ToLowerAscii(trimmed.substr(index));
  if (unit == "px") {
    return ParsedLength{value, ParsedLength::Unit::kPx};
  }
  if (unit == "em") {
    return ParsedLength{value, ParsedLength::Unit::kEm};
  }
  if (unit == "rem") {
    return ParsedLength{value, ParsedLength::Unit::kRem};
  }
  if (unit == "%") {
    return ParsedLength{value, ParsedLength::Unit::kPercent};
  }
  return std::nullopt;
}

std::optional<float> ParseNumber(std::string_view text) {
  const std::string_view trimmed = TrimAsciiWhitespace(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  std::size_t index = 0;
  float value = 0.0f;
  if (!ParseNumberAt(trimmed, &index, &value) || index != trimmed.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::uint32_t> ParseColor(std::string_view text) {
  const std::string value = ToLowerAscii(TrimAsciiWhitespace(text));
  if (value.empty()) {
    return std::nullopt;
  }
  if (value == "transparent") {
    return 0x00000000u;
  }
  if (value[0] == '#') {
    return ParseHexColor(value);
  }
  if (value.find('(') != std::string::npos) {
    if (const std::optional<std::uint32_t> color = ParseRgbFunction(value)) {
      return color;
    }
    return ParseHslFunction(value);
  }
  for (const NamedColor& named : kNamedColors) {
    if (named.name == value) {
      return 0xFF000000u | named.rgb;
    }
  }
  return std::nullopt;
}

void ApplyFontSizeDeclaration(const css::Declaration& declaration, float parent_font_size_px,
                              float root_font_size_px, ComputedStyle* style) {
  if (declaration.property != "font-size") {
    return;
  }
  const std::optional<ParsedLength> length = ParseLength(declaration.value);
  if (!length || length->value < 0.0f) {
    return;
  }
  float font_size_px = 0.0f;
  switch (length->unit) {
    case ParsedLength::Unit::kPx:
      font_size_px = length->value;
      break;
    case ParsedLength::Unit::kEm:
      font_size_px = length->value * parent_font_size_px;
      break;
    case ParsedLength::Unit::kRem:
      font_size_px = length->value * root_font_size_px;
      break;
    case ParsedLength::Unit::kPercent:
      font_size_px = length->value / 100.0f * parent_font_size_px;
      break;
  }
  style->font_size_px = font_size_px;
}

void ApplyDeclaration(const css::Declaration& declaration, float root_font_size_px,
                      ComputedStyle* style) {
  const std::string_view property = declaration.property;
  if (property == "font-size") {
    return;  // Applied by the font-size pass.
  }
  const float em_base_px = style->font_size_px;
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
  if (property == "visibility") {
    const std::string value = ToLowerAscii(TrimAsciiWhitespace(declaration.value));
    if (value == "visible") {
      style->visibility = Visibility::kVisible;
    } else if (value == "hidden") {
      style->visibility = Visibility::kHidden;
    }
    return;
  }
  if (property == "color") {
    const std::optional<std::uint32_t> color = ParseColor(declaration.value);
    if (color) {
      // Text is always opaque: the alpha channel is dropped.
      style->color = *color & 0xFFFFFFu;
    }
    return;
  }
  if (property == "background-color") {
    ApplyBackgroundColorValue(declaration.value, style);
    return;
  }
  if (property == "background") {
    ApplyBackgroundShorthandValue(declaration.value, style);
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
  if (property == "text-align") {
    const std::string value = ToLowerAscii(TrimAsciiWhitespace(declaration.value));
    if (value == "left" || value == "start") {
      style->text_align = TextAlign::kLeft;
    } else if (value == "right" || value == "end") {
      style->text_align = TextAlign::kRight;
    } else if (value == "center") {
      style->text_align = TextAlign::kCenter;
    } else if (value == "justify") {
      style->text_align = TextAlign::kJustify;
    }
    return;
  }
  if (property == "text-decoration") {
    ApplyTextDecorationValue(declaration.value, style);
    return;
  }
  if (property == "line-height") {
    ApplyLineHeightValue(declaration.value, em_base_px, style);
    return;
  }
  if (property == "margin") {
    ApplyMarginShorthand(declaration.value, em_base_px, root_font_size_px, style);
    return;
  }
  if (property == "margin-block") {
    ApplyLogicalMarginShorthand(declaration.value, em_base_px, root_font_size_px,
                                &style->margin_top, &style->margin_bottom);
    return;
  }
  if (property == "margin-inline") {
    ApplyLogicalMarginShorthand(declaration.value, em_base_px, root_font_size_px,
                                &style->margin_left, &style->margin_right);
    return;
  }
  if (property == "padding") {
    ApplyPaddingShorthand(declaration.value, em_base_px, root_font_size_px, style);
    return;
  }
  if (property == "padding-block") {
    ApplyLogicalPaddingShorthand(declaration.value, em_base_px, root_font_size_px,
                                 &style->padding_top, &style->padding_bottom);
    return;
  }
  if (property == "padding-inline") {
    ApplyLogicalPaddingShorthand(declaration.value, em_base_px, root_font_size_px,
                                 &style->padding_left, &style->padding_right);
    return;
  }
  // Logical longhands are mapped to their physical horizontal-tb equivalent.
  if (property == "margin-top" || property == "margin-block-start") {
    ApplyAutoLengthValue(declaration.value, em_base_px, root_font_size_px, &style->margin_top);
    return;
  }
  if (property == "margin-right" || property == "margin-inline-end") {
    ApplyAutoLengthValue(declaration.value, em_base_px, root_font_size_px, &style->margin_right);
    return;
  }
  if (property == "margin-bottom" || property == "margin-block-end") {
    ApplyAutoLengthValue(declaration.value, em_base_px, root_font_size_px, &style->margin_bottom);
    return;
  }
  if (property == "margin-left" || property == "margin-inline-start") {
    ApplyAutoLengthValue(declaration.value, em_base_px, root_font_size_px, &style->margin_left);
    return;
  }
  if (property == "padding-top" || property == "padding-block-start") {
    ApplyLengthValue(declaration.value, em_base_px, root_font_size_px, &style->padding_top);
    return;
  }
  if (property == "padding-right" || property == "padding-inline-end") {
    ApplyLengthValue(declaration.value, em_base_px, root_font_size_px, &style->padding_right);
    return;
  }
  if (property == "padding-bottom" || property == "padding-block-end") {
    ApplyLengthValue(declaration.value, em_base_px, root_font_size_px, &style->padding_bottom);
    return;
  }
  if (property == "padding-left" || property == "padding-inline-start") {
    ApplyLengthValue(declaration.value, em_base_px, root_font_size_px, &style->padding_left);
    return;
  }
  if (property == "width" || property == "inline-size") {
    ApplySizeValue(declaration.value, em_base_px, root_font_size_px, &style->width);
    return;
  }
  if (property == "height" || property == "block-size") {
    ApplySizeValue(declaration.value, em_base_px, root_font_size_px, &style->height);
    return;
  }
  if (property == "min-width" || property == "min-inline-size") {
    ApplyConstraintValue(declaration.value, em_base_px, root_font_size_px, &style->min_width);
    return;
  }
  if (property == "min-height" || property == "min-block-size") {
    ApplyConstraintValue(declaration.value, em_base_px, root_font_size_px, &style->min_height);
    return;
  }
  if (property == "max-width" || property == "max-inline-size") {
    ApplyConstraintValue(declaration.value, em_base_px, root_font_size_px, &style->max_width);
    return;
  }
  if (property == "max-height" || property == "max-block-size") {
    ApplyConstraintValue(declaration.value, em_base_px, root_font_size_px, &style->max_height);
    return;
  }
  // Everything else is outside the supported subset and ignored.
}

}  // namespace toy::style::detail
