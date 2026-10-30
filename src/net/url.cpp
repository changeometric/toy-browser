// URL parsing and serialization for the supported http and file schemes.

#include "toy/net/url.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "url_internal.h"

namespace toy::net {
namespace {

constexpr const char* kNoBaseResolution = "no base resolution yet";

void SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

bool IsAsciiAlpha(char character) {
  return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

bool IsAsciiDigit(char character) { return character >= '0' && character <= '9'; }

bool IsSchemeChar(char character) {
  return IsAsciiAlpha(character) || IsAsciiDigit(character) || character == '+' ||
         character == '-' || character == '.';
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

bool IsHexDigit(char character) {
  return IsAsciiDigit(character) || (character >= 'a' && character <= 'f') ||
         (character >= 'A' && character <= 'F');
}

int HexValue(char character) {
  if (IsAsciiDigit(character)) {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  return character - 'A' + 10;
}

bool PercentDecode(std::string_view encoded, std::string* decoded) {
  decoded->clear();
  decoded->reserve(encoded.size());
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    if (encoded[index] != '%') {
      decoded->push_back(encoded[index]);
      continue;
    }
    if (index + 2 >= encoded.size() || !IsHexDigit(encoded[index + 1]) ||
        !IsHexDigit(encoded[index + 2])) {
      return false;
    }
    decoded->push_back(
        static_cast<char>(HexValue(encoded[index + 1]) * 16 + HexValue(encoded[index + 2])));
    index += 2;
  }
  return true;
}

bool IsUnreserved(char character) {
  return IsAsciiAlpha(character) || IsAsciiDigit(character) || character == '-' ||
         character == '.' || character == '_' || character == '~';
}

bool IsSubDelimiter(char character) {
  switch (character) {
    case '!':
    case '$':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '*':
    case '+':
    case ',':
    case ';':
    case '=':
      return true;
    default:
      return false;
  }
}

bool IsPathChar(char character) {
  return IsUnreserved(character) || IsSubDelimiter(character) || character == ':' ||
         character == '@' || character == '/';
}

bool IsQueryChar(char character) { return IsPathChar(character) || character == '?'; }

std::string Encode(std::string_view decoded, bool query) {
  constexpr char kHexDigits[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(decoded.size());
  for (const char character : decoded) {
    if (query ? IsQueryChar(character) : IsPathChar(character)) {
      encoded.push_back(character);
      continue;
    }
    const auto byte = static_cast<unsigned char>(character);
    encoded.push_back('%');
    encoded.push_back(kHexDigits[byte >> 4]);
    encoded.push_back(kHexDigits[byte & 0x0F]);
  }
  return encoded;
}

// Splits "path?query#fragment" into three still-encoded components.
void SplitTarget(std::string_view target, std::string_view* path, std::string_view* query,
                 std::string_view* fragment) {
  constexpr std::size_t kNotFound = std::string_view::npos;
  const std::size_t hash = target.find('#');
  const std::string_view before_hash = target.substr(0, hash == kNotFound ? target.size() : hash);
  if (hash != kNotFound) {
    *fragment = target.substr(hash + 1);
  }
  const std::size_t question = before_hash.find('?');
  *path = before_hash.substr(0, question == kNotFound ? before_hash.size() : question);
  if (question != kNotFound) {
    *query = before_hash.substr(question + 1);
  }
}

bool DecodeComponents(std::string_view encoded_path, std::string_view encoded_query,
                      std::string_view encoded_fragment, Url* url, std::string* error) {
  if (!PercentDecode(encoded_path, &url->path)) {
    SetError(error, "invalid percent escape in URL path");
    return false;
  }
  if (!PercentDecode(encoded_query, &url->query)) {
    SetError(error, "invalid percent escape in URL query");
    return false;
  }
  if (!PercentDecode(encoded_fragment, &url->fragment)) {
    SetError(error, "invalid percent escape in URL fragment");
    return false;
  }
  return true;
}

}  // namespace

std::string EncodePath(std::string_view decoded) { return Encode(decoded, false); }

std::string EncodeQuery(std::string_view decoded) { return Encode(decoded, true); }

std::optional<Url> ParseUrl(std::string_view text, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }

  constexpr std::size_t kNotFound = std::string_view::npos;
  const std::size_t scheme_end = text.find(':');
  if (scheme_end == kNotFound) {
    SetError(error, std::string("relative URL rejected: ") + kNoBaseResolution);
    return std::nullopt;
  }
  const std::string_view scheme_text = text.substr(0, scheme_end);
  if (scheme_text.empty() || !IsAsciiAlpha(scheme_text.front())) {
    SetError(error, "invalid URL: malformed scheme");
    return std::nullopt;
  }
  for (const char character : scheme_text) {
    if (!IsSchemeChar(character)) {
      SetError(error, "invalid URL: malformed scheme");
      return std::nullopt;
    }
  }

  Url url;
  url.scheme = ToLowerAscii(scheme_text);
  if (url.scheme != "http" && url.scheme != "file") {
    SetError(error, "unsupported URL scheme '" + url.scheme + "': " + kNoBaseResolution);
    return std::nullopt;
  }
  if (text.substr(scheme_end + 1, 2) != "//") {
    SetError(error, "unsupported URL form: expected '" + url.scheme + "://', " + kNoBaseResolution);
    return std::nullopt;
  }

  const std::string_view rest = text.substr(scheme_end + 3);
  const std::size_t authority_end = rest.find_first_of("/?#");
  const std::string_view authority =
      authority_end == kNotFound ? rest : rest.substr(0, authority_end);
  const std::string_view target =
      authority_end == kNotFound ? std::string_view() : rest.substr(authority_end);

  std::string_view encoded_path;
  std::string_view encoded_query;
  std::string_view encoded_fragment;
  SplitTarget(target, &encoded_path, &encoded_query, &encoded_fragment);

  if (url.scheme == "http") {
    if (authority.empty()) {
      SetError(error, "HTTP URL requires a host");
      return std::nullopt;
    }
    const std::size_t port_separator = authority.find(':');
    const std::string_view host =
        port_separator == kNotFound ? authority : authority.substr(0, port_separator);
    if (host.empty()) {
      SetError(error, "HTTP URL requires a host");
      return std::nullopt;
    }
    url.host = ToLowerAscii(host);
    if (port_separator == kNotFound) {
      url.port = 80;
    } else {
      const std::string_view port_text = authority.substr(port_separator + 1);
      if (port_text.empty()) {
        SetError(error, "HTTP URL has an empty port");
        return std::nullopt;
      }
      std::uint32_t port = 0;
      for (const char character : port_text) {
        if (!IsAsciiDigit(character)) {
          SetError(error, "invalid HTTP port '" + std::string(port_text) + "'");
          return std::nullopt;
        }
        port = port * 10 + static_cast<std::uint32_t>(character - '0');
        if (port > 65535) {
          break;
        }
      }
      if (port == 0 || port > 65535) {
        SetError(error, "HTTP port out of range '" + std::string(port_text) + "'");
        return std::nullopt;
      }
      url.port = static_cast<std::uint16_t>(port);
    }
    if (encoded_path.empty()) {
      encoded_path = "/";
    }
  } else {
    // file:// URLs carry no authority (or the literal localhost), port or query.
    if (!authority.empty() && !EqualsIgnoreCase(authority, "localhost")) {
      SetError(error, "file URL host must be empty or 'localhost'");
      return std::nullopt;
    }
    if (!encoded_query.empty() || !encoded_fragment.empty()) {
      SetError(error, "file URL must not have a query or fragment");
      return std::nullopt;
    }
    url.host.clear();
    url.port = 0;
  }

  if (!DecodeComponents(encoded_path, encoded_query, encoded_fragment, &url, error)) {
    return std::nullopt;
  }
  if (url.scheme == "file" && (url.path.empty() || url.path.front() != '/')) {
    SetError(error, "file URL path must be absolute");
    return std::nullopt;
  }
  return url;
}

std::string SerializeUrl(const Url& url) {
  std::string text;
  text += url.scheme;
  text += "://";
  if (url.scheme != "file") {
    text += url.host;
    if (url.port != 80) {
      text += ':';
      text += std::to_string(url.port);
    }
  }
  text += EncodePath(url.path);
  if (!url.query.empty()) {
    text += '?';
    text += EncodeQuery(url.query);
  }
  if (!url.fragment.empty()) {
    text += '#';
    text += EncodeQuery(url.fragment);
  }
  return text;
}

}  // namespace toy::net
