#pragma once

#include <cstddef>
#include <memory>
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
  // Time allowed for each socket read (also used when sending the request and for
  // the TLS handshake).
  int read_timeout_ms = 10000;
  // Number of redirects to follow before failing.
  int max_redirects = 5;
  // Upper bound on the decoded body size, for HTTP, HTTPS and file bodies;
  // larger content is an error.
  std::size_t max_body_bytes = 16u * 1024 * 1024;
};

// A reusable, single-threaded HTTP/1.1 client with persistent connections and
// transparent gzip decoding (M13).
//
// Fetch() below is the one-shot convenience API (one request per connection,
// `Connection: close`, no compression negotiation). A Client instead keeps at most
// one idle connection per origin (scheme, host, port) and reuses it for later
// requests to that origin, so a page, its stylesheets and its images no longer pay
// a TCP connect — or TLS handshake — per resource. Requests are serial: no
// pipelining, no concurrent callers and no cross-origin reuse.
//
// Shared with Fetch(): blocking operation, the same FetchOptions (connect/read
// timeouts, decoded-body cap, redirect limit), the same redirect rules
// (301/302/303/307/308, Location resolved with ResolveUrl, only http/https
// targets), the same file:// handling, and the same Response fields (decoded body,
// lowercase headers, final URL after redirects).
//
// Specific to Client:
// - requests are HTTP/1.1 keep-alive and advertise `Accept-Encoding: gzip`;
// - a response with `Content-Encoding: gzip` is inflated transparently with the
//   approved zlib before it is stored in Response::body; `identity` is accepted;
//   any other coding is an ordinary error. Both the raw encoded body and the
//   decoded body are bounded by FetchOptions::max_body_bytes; headers are kept as
//   received, so callers can still observe Content-Encoding;
// - a connection is returned to the idle pool only when the response body was
//   fully framed and the exchange allows persistence: HTTP/1.1 without
//   `Connection: close`, or HTTP/1.0 with `Connection: keep-alive`, with the body
//   delimited by Content-Length or chunked encoding. A persistent response with no
//   length delimiter is an error (the connection is then closed);
// - if a connection reused from the pool fails before any response byte arrives,
//   the client closes it and retries the request once on a fresh connection.
//   Timeouts and fresh-connection failures are never retried, and nothing is
//   retried after response bytes have arrived;
// - redirects use the same pool: same-origin hops reuse the connection,
//   cross-origin hops keep one idle connection for their own origin;
// - CloseIdleConnections() drops every pooled connection; the destructor does the
//   same.
//
// On failure *out is left untouched and *error (when not null) is a human-readable
// message in the same style as Fetch(); on success *error is cleared. Ordinary I/O
// and parsing failures never escape as exceptions.
class Client {
 public:
  Client();
  ~Client();

  Client(Client&&) noexcept;
  Client& operator=(Client&&) noexcept;
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  // Fetches http, https or file. See the class comment for the keep-alive, gzip,
  // persistence and retry rules.
  bool Fetch(const Url& url, const FetchOptions& options, Response* out, std::string* error);

  // Closes all idle pooled connections. Has no effect on later requests beyond
  // paying for a new connection.
  void CloseIdleConnections();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Fetches an http, https or file URL and stores the result in `out`.
// - http: blocking HTTP/1.1 GET over TCP; https: the same request/response code over
//   a verified TLS connection (SNI and hostname checks, system trust store, no
//   insecure mode). One request per connection, following 301/302/303/307/308
//   redirects resolved with net::ResolveUrl (relative and protocol-relative
//   Location supported; targets may switch between http and https).
// - file: reads the file at the decoded absolute path; the response status is 200.
//   Files larger than FetchOptions::max_body_bytes are rejected before reading.
// Returns true on success and false on failure. On failure `error` (when not null)
// is a human-readable message and `out` is left untouched; on success `error` is
// cleared. Ordinary I/O and parsing failures never escape as exceptions.
bool Fetch(const Url& url, const FetchOptions& options, Response* out, std::string* error);

}  // namespace toy::net
