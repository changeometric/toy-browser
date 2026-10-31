// URL parsing and serialization for the supported http, https and file schemes.

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

// The schemes that follow the RFC 3986 authority/port/path rules shared by
// http and https; file URLs have their own rules.
bool IsHttpScheme(std::string_view scheme) { return scheme == "http" || scheme == "https"; }

std::uint16_t DefaultPort(std::string_view scheme) { return scheme == "https" ? 443 : 80; }

// "HTTP"/"HTTPS" for the scheme-specific error messages below.
std::string SchemeLabel(std::string_view scheme) {
  std::string label(scheme);
  for (char& character : label) {
    if (character >= 'a' && character <= 'z') {
      character = static_cast<char>(character - 'a' + 'A');
    }
  }
  return label;
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

// Splits an encoded URI reference into raw components (RFC 3986 section 4.2).
// Path, query and fragment stay percent-encoded; they are decoded only by the
// final ParseUrl call, exactly like a reference written in URL text.
struct ReferenceParts {
  bool has_scheme = false;
  std::string_view scheme;
  bool has_authority = false;
  std::string_view authority;
  std::string_view path;
  bool has_query = false;
  std::string_view query;
  bool has_fragment = false;
  std::string_view fragment;
  // A scheme was written without the "//" our http/https/file subset requires.
  bool unsupported_form = false;
};

ReferenceParts SplitReference(std::string_view text) {
  constexpr std::size_t kNotFound = std::string_view::npos;
  ReferenceParts parts;
  std::string_view before_fragment = text;
  const std::size_t hash = text.find('#');
  if (hash != kNotFound) {
    parts.has_fragment = true;
    parts.fragment = text.substr(hash + 1);
    before_fragment = text.substr(0, hash);
  }
  std::string_view hier_part = before_fragment;
  const std::size_t question = before_fragment.find('?');
  if (question != kNotFound) {
    parts.has_query = true;
    parts.query = before_fragment.substr(question + 1);
    hier_part = before_fragment.substr(0, question);
  }
  // A scheme is an initial letter followed by scheme characters and a ':'.
  std::size_t colon = kNotFound;
  if (!hier_part.empty() && IsAsciiAlpha(hier_part.front())) {
    std::size_t index = 1;
    while (index < hier_part.size() && IsSchemeChar(hier_part[index])) {
      ++index;
    }
    if (index < hier_part.size() && hier_part[index] == ':') {
      colon = index;
    }
  }
  if (colon != kNotFound) {
    parts.has_scheme = true;
    parts.scheme = hier_part.substr(0, colon);
    const std::string_view after_scheme = hier_part.substr(colon + 1);
    if (after_scheme.substr(0, 2) != "//") {
      parts.unsupported_form = true;
      return parts;
    }
    parts.has_authority = true;
    const std::string_view after_slashes = after_scheme.substr(2);
    const std::size_t slash = after_slashes.find('/');
    parts.authority = after_slashes.substr(0, slash);
    if (slash != kNotFound) {
      parts.path = after_slashes.substr(slash);
    }
    return parts;
  }
  if (hier_part.substr(0, 2) == "//") {
    parts.has_authority = true;
    const std::string_view after_slashes = hier_part.substr(2);
    const std::size_t slash = after_slashes.find('/');
    parts.authority = after_slashes.substr(0, slash);
    if (slash != kNotFound) {
      parts.path = after_slashes.substr(slash);
    }
    return parts;
  }
  parts.path = hier_part;
  return parts;
}

// Removes the last segment and its preceding '/' (if any) from `path`
// (RFC 3986 section 5.2.4 step 2C).
void RemoveLastPathSegment(std::string* path) {
  const std::size_t slash = path->rfind('/');
  if (slash == std::string::npos) {
    path->clear();
    return;
  }
  path->erase(slash);
}

// RFC 3986 section 5.2.4 on an encoded path. Leading ".." segments that would
// climb above the root are dropped, so dot segments never escape '/'.
std::string RemoveDotSegments(std::string_view encoded_path) {
  std::string input(encoded_path);
  std::string output;
  output.reserve(input.size());
  while (!input.empty()) {
    if (input.compare(0, 3, "../") == 0) {
      input.erase(0, 3);
    } else if (input.compare(0, 2, "./") == 0) {
      input.erase(0, 2);
    } else if (input.compare(0, 3, "/./") == 0) {
      input.replace(0, 3, "/");
    } else if (input == "/.") {
      input = "/";
    } else if (input.compare(0, 4, "/../") == 0) {
      input.replace(0, 4, "/");
      RemoveLastPathSegment(&output);
    } else if (input == "/..") {
      input = "/";
      RemoveLastPathSegment(&output);
    } else if (input == "." || input == "..") {
      input.clear();
    } else {
      // Move the first segment, including a leading '/', to the output buffer.
      const std::size_t search_from = input.front() == '/' ? 1 : 0;
      std::size_t segment_end = input.find('/', search_from);
      if (segment_end == std::string::npos) {
        segment_end = input.size();
      }
      output.append(input, 0, segment_end);
      input.erase(0, segment_end);
    }
  }
  return output;
}

// RFC 3986 section 5.2.3 on two encoded paths. `base_path` belongs to a URL
// with a defined authority, so it is either empty or starts with '/'.
std::string MergePaths(std::string_view base_path, std::string_view reference_path) {
  if (base_path.empty()) {
    return "/" + std::string(reference_path);
  }
  const std::size_t slash = base_path.rfind('/');
  if (slash == std::string_view::npos) {
    return std::string(reference_path);
  }
  return std::string(base_path.substr(0, slash + 1)) + std::string(reference_path);
}

// The authority of `base` as it appears in URL text ("host[:port]" for http and
// https, empty for file URLs, which carry no authority).
std::string BaseAuthority(const Url& base) {
  if (!IsHttpScheme(base.scheme)) {
    return std::string();
  }
  std::string authority = base.host;
  if (base.port != DefaultPort(base.scheme)) {
    authority += ':';
    authority += std::to_string(base.port);
  }
  return authority;
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
    SetError(error, "relative URL rejected: use ResolveUrl with an absolute base URL");
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
  if (!IsHttpScheme(url.scheme) && url.scheme != "file") {
    SetError(error, "unsupported URL scheme '" + url.scheme + "'");
    return std::nullopt;
  }
  if (text.substr(scheme_end + 1, 2) != "//") {
    SetError(error, "unsupported URL form: expected '" + url.scheme + "://'");
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

  if (IsHttpScheme(url.scheme)) {
    const std::string scheme_label = SchemeLabel(url.scheme);
    if (authority.empty()) {
      SetError(error, scheme_label + " URL requires a host");
      return std::nullopt;
    }
    const std::size_t port_separator = authority.find(':');
    const std::string_view host =
        port_separator == kNotFound ? authority : authority.substr(0, port_separator);
    if (host.empty()) {
      SetError(error, scheme_label + " URL requires a host");
      return std::nullopt;
    }
    url.host = ToLowerAscii(host);
    if (port_separator == kNotFound) {
      url.port = DefaultPort(url.scheme);
    } else {
      const std::string_view port_text = authority.substr(port_separator + 1);
      if (port_text.empty()) {
        SetError(error, scheme_label + " URL has an empty port");
        return std::nullopt;
      }
      std::uint32_t port = 0;
      for (const char character : port_text) {
        if (!IsAsciiDigit(character)) {
          SetError(error, "invalid " + scheme_label + " port '" + std::string(port_text) + "'");
          return std::nullopt;
        }
        port = port * 10 + static_cast<std::uint32_t>(character - '0');
        if (port > 65535) {
          break;
        }
      }
      if (port == 0 || port > 65535) {
        SetError(error, scheme_label + " port out of range '" + std::string(port_text) + "'");
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

std::optional<Url> ResolveUrl(const Url& base, std::string_view reference, std::string* error) {
  const ReferenceParts parts = SplitReference(reference);
  const std::string reference_scheme =
      parts.has_scheme ? ToLowerAscii(parts.scheme) : std::string();
  const std::string base_scheme = ToLowerAscii(base.scheme);
  const std::string& scheme = parts.has_scheme ? reference_scheme : base_scheme;
  if (!IsHttpScheme(scheme) && scheme != "file") {
    SetError(error, "unsupported URL scheme '" + scheme + "' in reference");
    return std::nullopt;
  }
  if (parts.has_scheme && parts.unsupported_form) {
    SetError(error, "unsupported URL form in reference: expected '" + scheme + "://'");
    return std::nullopt;
  }

  std::string authority;
  std::string path;
  std::string query;
  std::string fragment;
  if (parts.has_scheme || parts.has_authority) {
    // RFC 3986 section 5.2.2 cases 1 and 2: the reference supplies the
    // authority, its own path is used verbatim (minus dot segments) and the
    // query comes from the reference.
    authority = std::string(parts.authority);
    path = RemoveDotSegments(parts.path);
    if (parts.has_query) {
      query = std::string(parts.query);
    }
    if (parts.has_fragment) {
      fragment = std::string(parts.fragment);
    }
  } else if (parts.path.empty()) {
    // Case 3: an empty path keeps the base path and, unless the reference has a
    // query, the base query. This is also the empty and fragment-only form.
    authority = BaseAuthority(base);
    path = EncodePath(base.path);
    query = parts.has_query ? std::string(parts.query) : EncodeQuery(base.query);
    if (parts.has_fragment) {
      fragment = std::string(parts.fragment);
    }
  } else {
    // Cases 4 and 5: an absolute path replaces the base path; a relative path is
    // merged with the base path directory before dot segments are removed.
    authority = BaseAuthority(base);
    path = parts.path.front() == '/'
               ? RemoveDotSegments(parts.path)
               : RemoveDotSegments(MergePaths(EncodePath(base.path), parts.path));
    if (parts.has_query) {
      query = std::string(parts.query);
    }
    if (parts.has_fragment) {
      fragment = std::string(parts.fragment);
    }
  }

  // Rebuild URL text and let ParseUrl do the final validation and decoding. File
  // URLs cannot carry a query or a fragment in this MVP, so those components are
  // deliberately left out of the text; http and https keep both.
  std::string text = scheme;
  text += "://";
  text += authority;
  text += path;
  if (scheme != "file") {
    if (!query.empty()) {
      text += '?';
      text += query;
    }
    if (!fragment.empty()) {
      text += '#';
      text += fragment;
    }
  }
  return ParseUrl(text, error);
}

std::string SerializeUrl(const Url& url) {
  std::string text;
  text += url.scheme;
  text += "://";
  if (url.scheme != "file") {
    text += url.host;
    if (url.port != DefaultPort(url.scheme)) {
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
