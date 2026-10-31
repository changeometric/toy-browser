#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace toy::net {

// A parsed absolute URL. Only the "http", "https" and "file" schemes are supported.
struct Url {
  // "http", "https" or "file", lowercase.
  std::string scheme;
  // Host for http and https URLs, lowercase. Always empty for file URLs.
  std::string host;
  // Port for http and https URLs: the explicit port, or the scheme's default (80 for
  // http, 443 for https) when none was written. Always 0 for file URLs.
  std::uint16_t port = 0;
  // Percent-decoded path. Always absolute (starts with '/') for parsed URLs.
  std::string path;
  // Percent-decoded query string, without the leading '?'.
  std::string query;
  // Percent-decoded fragment, without the leading '#'.
  std::string fragment;
};

// Parses an absolute http://, https:// or file:// URL. Relative URLs and other
// schemes are rejected; to resolve a relative reference against an absolute base
// URL, callers use ResolveUrl(base, reference) instead.
// On failure returns std::nullopt and, when `error` is not null, sets it to a
// human-readable message. On success `error` is cleared.
std::optional<Url> ParseUrl(std::string_view text, std::string* error);

// Resolves `reference` against the absolute URL `base` and returns the resulting
// absolute URL (RFC 3986 section 5.2, restricted to the http/https/file subset).
// `reference` may be an absolute URL (including http<->https switches), a
// scheme-relative URL ("//host/path", which inherits the base scheme), an absolute
// path ("/a/b"), a relative path (including "." and ".." segments, which keep the
// base scheme), a query-only reference ("?q"), a fragment-only reference ("#frag")
// or empty (which resolves to `base` without its fragment). Dot segments are
// removed from the result. A reference with an unsupported scheme (anything but
// http, https or file) is an error.
//
// The result is parsed with ParseUrl. File URLs in this MVP cannot carry a query
// or fragment, so those components are dropped from a file result; http and https
// results keep them.
//
// On failure returns std::nullopt and, when `error` is not null, sets it to a
// human-readable message. On success `error` is cleared.
std::optional<Url> ResolveUrl(const Url& base, std::string_view reference, std::string* error);

// Serializes a URL back to text, percent-encoding path, query and fragment and
// omitting the scheme's default port (80 for http, 443 for https). For every URL
// accepted by ParseUrl, ParseUrl(SerializeUrl(url)) reproduces the same fields.
std::string SerializeUrl(const Url& url);

}  // namespace toy::net
