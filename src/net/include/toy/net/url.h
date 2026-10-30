#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace toy::net {

// A parsed absolute URL. Only the "http" and "file" schemes are supported.
struct Url {
  // "http" or "file", lowercase.
  std::string scheme;
  // Host for http URLs, lowercase. Always empty for file URLs.
  std::string host;
  // HTTP port: the explicit port, or 80 when none was written. Always 0 for file URLs.
  std::uint16_t port = 0;
  // Percent-decoded path. Always absolute (starts with '/') for parsed URLs.
  std::string path;
  // Percent-decoded query string, without the leading '?'.
  std::string query;
  // Percent-decoded fragment, without the leading '#'.
  std::string fragment;
};

// Parses an absolute http:// or file:// URL. Relative URLs and other schemes are
// rejected because there is no base resolution yet ("no base resolution yet").
// On failure returns std::nullopt and, when `error` is not null, sets it to a
// human-readable message. On success `error` is cleared.
std::optional<Url> ParseUrl(std::string_view text, std::string* error);

// Serializes a URL back to text, percent-encoding path, query and fragment.
// For every URL accepted by ParseUrl, ParseUrl(SerializeUrl(url)) reproduces
// the same fields.
std::string SerializeUrl(const Url& url);

}  // namespace toy::net
