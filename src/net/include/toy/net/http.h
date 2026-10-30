#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "toy/net/url.h"

namespace toy::net {

// The result of one fetch: final status, response headers, decoded body and the
// URL that produced the final response (after redirects).
struct Response {
  int status = 0;
  // Header names are lowercase; values are trimmed of surrounding whitespace.
  std::vector<std::pair<std::string, std::string>> headers;
  // Decoded body bytes (chunked transfer encoding is removed).
  std::string body;
  // URL of the final response. For file URLs this is the requested URL.
  Url final_url;

  // Returns the first value for a header, with a case-insensitive name lookup.
  // Returns an empty string when the header is absent.
  std::string HeaderValue(std::string_view name) const;
};

// Knobs for Fetch. Timeouts are in milliseconds.
struct FetchOptions {
  // Time allowed for the TCP connect.
  int connect_timeout_ms = 5000;
  // Time allowed for each socket read (also used when sending the request).
  int read_timeout_ms = 10000;
  // Number of redirects to follow before failing.
  int max_redirects = 5;
  // Upper bound on the decoded body size, for both HTTP and file bodies;
  // larger content is an error.
  std::size_t max_body_bytes = 16u * 1024 * 1024;
};

// Fetches an http or file URL and stores the result in `out`.
// - http: blocking HTTP/1.1 GET over TCP, one request per connection, following
//   301/302/303/307/308 redirects (absolute or root-relative Location only).
// - file: reads the file at the decoded absolute path; the response status is 200.
//   Files larger than FetchOptions::max_body_bytes are rejected before reading.
// Returns true on success and false on failure. On failure `error` (when not null)
// is a human-readable message and `out` is left untouched; on success `error` is
// cleared. Ordinary I/O and parsing failures never escape as exceptions.
bool Fetch(const Url& url, const FetchOptions& options, Response* out, std::string* error);

}  // namespace toy::net
