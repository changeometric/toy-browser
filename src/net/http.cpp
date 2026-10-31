// Blocking HTTP/1.1 GET client over plain TCP or TLS, and file:// reader, behind
// toy::net::Fetch.

#include "toy/net/http.h"

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "stream_internal.h"
#include "tls_internal.h"
#include "toy/net/url.h"
#include "url_internal.h"

namespace toy::net {
namespace {

constexpr const char* kUserAgent = "toybrowser/0.1";
// Read budget above the body limit, so a large-but-legal header block still fits.
constexpr std::size_t kMaxHeaderBytes = 64 * 1024;

void SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

bool IsAsciiDigit(char character) { return character >= '0' && character <= '9'; }

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

bool IsAsciiSpace(char character) { return character == ' ' || character == '\t'; }

std::string TrimAsciiSpace(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && IsAsciiSpace(text[begin])) {
    ++begin;
  }
  while (end > begin && IsAsciiSpace(text[end - 1])) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

// Returns the value of the first header with this lowercase name, or an empty
// view when the header is absent.
std::string_view FindHeader(const std::vector<std::pair<std::string, std::string>>& headers,
                            std::string_view name) {
  for (const auto& [header_name, header_value] : headers) {
    if (EqualsIgnoreCase(header_name, name)) {
      return header_value;
    }
  }
  return {};
}

int HexDigitValue(char character) {
  if (IsAsciiDigit(character)) {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

bool ParseContentLength(std::string_view text, std::size_t* length, std::string* error) {
  constexpr std::size_t kMaxSize = std::numeric_limits<std::size_t>::max();
  if (text.empty()) {
    SetError(error, "empty Content-Length header");
    return false;
  }
  std::size_t value = 0;
  for (const char character : text) {
    if (!IsAsciiDigit(character)) {
      SetError(error, "invalid Content-Length '" + std::string(text) + "'");
      return false;
    }
    const auto digit = static_cast<std::size_t>(character - '0');
    if (value > (kMaxSize - digit) / 10) {
      SetError(error, "Content-Length is too large");
      return false;
    }
    value = value * 10 + digit;
  }
  *length = value;
  return true;
}

bool DecodeChunkedBody(std::string_view raw, std::size_t max_body_bytes, std::string* body,
                       std::string* error) {
  constexpr std::size_t kMaxSize = std::numeric_limits<std::size_t>::max();
  constexpr std::size_t kNotFound = std::string_view::npos;
  std::size_t position = 0;
  for (;;) {
    const std::size_t size_line_end = raw.find("\r\n", position);
    if (size_line_end == kNotFound) {
      SetError(error, "malformed chunked body: missing chunk size line");
      return false;
    }
    std::string_view size_line = raw.substr(position, size_line_end - position);
    const std::size_t extension = size_line.find(';');
    if (extension != kNotFound) {
      size_line = size_line.substr(0, extension);
    }
    if (size_line.empty()) {
      SetError(error, "malformed chunked body: empty chunk size");
      return false;
    }
    std::size_t chunk_size = 0;
    for (const char character : size_line) {
      const int digit = HexDigitValue(character);
      if (digit < 0) {
        SetError(error,
                 "malformed chunked body: invalid chunk size '" + std::string(size_line) + "'");
        return false;
      }
      const auto value = static_cast<std::size_t>(digit);
      if (chunk_size > (kMaxSize - value) / 16) {
        SetError(error, "malformed chunked body: chunk size is too large");
        return false;
      }
      chunk_size = chunk_size * 16 + value;
    }
    position = size_line_end + 2;
    if (chunk_size == 0) {
      // Consume optional trailers up to the final empty line.
      for (;;) {
        const std::size_t trailer_end = raw.find("\r\n", position);
        if (trailer_end == kNotFound) {
          SetError(error, "malformed chunked body: missing terminating line");
          return false;
        }
        if (trailer_end == position) {
          return true;
        }
        position = trailer_end + 2;
      }
    }
    if (raw.size() - position < chunk_size) {
      SetError(error, "truncated chunked body");
      return false;
    }
    if (chunk_size > max_body_bytes - body->size()) {
      SetError(error, "HTTP response body exceeds the size limit");
      return false;
    }
    body->append(raw.data() + position, chunk_size);
    position += chunk_size;
    if (raw.size() - position < 2 || raw.substr(position, 2) != "\r\n") {
      SetError(error, "malformed chunked body: missing chunk terminator");
      return false;
    }
    position += 2;
  }
}

// Parses the status line and the header lines of a response head (everything before
// the terminating blank line). Shared by the one-shot and the keep-alive paths.
bool ParseResponseHead(std::string_view head, Response* response, std::string* error) {
  constexpr std::size_t kNotFound = std::string_view::npos;
  const std::size_t status_line_end = head.find("\r\n");
  const std::string_view status_line =
      status_line_end == kNotFound ? head : head.substr(0, status_line_end);
  const std::string_view header_lines =
      status_line_end == kNotFound ? std::string_view() : head.substr(status_line_end + 2);
  if (!status_line.starts_with("HTTP/")) {
    SetError(error, "malformed HTTP status line");
    return false;
  }
  const std::size_t status_space = status_line.find(' ');
  if (status_space == kNotFound) {
    SetError(error, "malformed HTTP status line");
    return false;
  }
  const std::string_view after_protocol = status_line.substr(status_space + 1);
  const std::size_t code_end = after_protocol.find(' ');
  const std::string_view code_text = after_protocol.substr(0, code_end);
  if (code_text.size() != 3) {
    SetError(error, "malformed HTTP status code");
    return false;
  }
  int status = 0;
  for (const char character : code_text) {
    if (!IsAsciiDigit(character)) {
      SetError(error, "malformed HTTP status code");
      return false;
    }
    status = status * 10 + (character - '0');
  }
  if (status < 100 || status > 599) {
    SetError(error, "HTTP status code out of range");
    return false;
  }
  response->status = status;
  response->headers.clear();

  std::size_t position = 0;
  while (position < header_lines.size()) {
    std::size_t line_end = header_lines.find("\r\n", position);
    if (line_end == kNotFound) {
      line_end = header_lines.size();
    }
    const std::string_view line = header_lines.substr(position, line_end - position);
    position = line_end + 2;
    if (line.empty()) {
      continue;
    }
    const std::size_t colon = line.find(':');
    if (colon == kNotFound || colon == 0) {
      SetError(error, "malformed HTTP header line");
      return false;
    }
    response->headers.emplace_back(ToLowerAscii(TrimAsciiSpace(line.substr(0, colon))),
                                   TrimAsciiSpace(line.substr(colon + 1)));
  }
  return true;
}

bool ParseResponse(std::string_view raw, std::size_t max_body_bytes, Response* response,
                   std::string* error) {
  constexpr std::size_t kNotFound = std::string_view::npos;
  const std::size_t header_end = raw.find("\r\n\r\n");
  if (header_end == kNotFound) {
    SetError(error, "malformed HTTP response: missing header terminator");
    return false;
  }
  if (!ParseResponseHead(raw.substr(0, header_end), response, error)) {
    return false;
  }
  const std::string_view raw_body = raw.substr(header_end + 4);

  const std::string_view transfer_encoding = FindHeader(response->headers, "transfer-encoding");
  const std::string_view content_length_text = FindHeader(response->headers, "content-length");
  std::string body;
  if (!transfer_encoding.empty()) {
    if (!EqualsIgnoreCase(transfer_encoding, "chunked")) {
      SetError(error, "unsupported Transfer-Encoding '" + std::string(transfer_encoding) + "'");
      return false;
    }
    if (!DecodeChunkedBody(raw_body, max_body_bytes, &body, error)) {
      return false;
    }
  } else if (!content_length_text.empty()) {
    std::size_t length = 0;
    if (!ParseContentLength(content_length_text, &length, error)) {
      return false;
    }
    if (length > max_body_bytes) {
      SetError(error, "HTTP response body exceeds the size limit");
      return false;
    }
    if (raw_body.size() < length) {
      SetError(error, "truncated HTTP body");
      return false;
    }
    body.assign(raw_body.substr(0, length));
  } else {
    body.assign(raw_body);
  }
  if (body.size() > max_body_bytes) {
    SetError(error, "HTTP response body exceeds the size limit");
    return false;
  }
  response->body = std::move(body);
  return true;
}

std::uint16_t DefaultPort(const Url& url) { return url.scheme == "https" ? 443 : 80; }

// The origin-form request target: encoded path plus query.
std::string RequestTarget(const Url& url) {
  std::string target = EncodePath(url.path.empty() ? std::string_view("/") : url.path);
  if (!url.query.empty()) {
    target += '?';
    target += EncodeQuery(url.query);
  }
  return target;
}

// The Host header value: the host plus a non-default port.
std::string HostHeader(const Url& url) {
  std::string host = url.host;
  if (url.port != DefaultPort(url)) {
    host += ':';
    host += std::to_string(url.port);
  }
  return host;
}

// The one-shot Fetch request: one request per connection, no compression.
std::string BuildRequest(const Url& url) {
  std::string request;
  request += "GET " + RequestTarget(url) + " HTTP/1.1\r\n";
  request += "Host: " + HostHeader(url) + "\r\n";
  request += "Connection: close\r\n";
  request += "User-Agent: ";
  request += kUserAgent;
  request += "\r\n\r\n";
  return request;
}

// The reusable Client request: HTTP/1.1 keep-alive with gzip negotiation.
std::string BuildClientRequest(const Url& url) {
  std::string request;
  request += "GET " + RequestTarget(url) + " HTTP/1.1\r\n";
  request += "Host: " + HostHeader(url) + "\r\n";
  request += "Connection: keep-alive\r\n";
  request += "User-Agent: ";
  request += kUserAgent;
  request += "\r\n";
  request += "Accept-Encoding: gzip\r\n\r\n";
  return request;
}

std::size_t ResponseReadLimit(const FetchOptions& options) {
  constexpr std::size_t kMaxSize = std::numeric_limits<std::size_t>::max();
  if (options.max_body_bytes > kMaxSize - kMaxHeaderBytes) {
    return kMaxSize;
  }
  return options.max_body_bytes + kMaxHeaderBytes;
}

bool IsRedirectStatus(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// Opens the byte stream for one request: plain TCP for http, verified TLS for https.
std::unique_ptr<internal::Stream> OpenStream(const Url& url, const FetchOptions& options,
                                             std::string* error) {
  if (url.scheme == "https") {
    return internal::OpenTlsStream(url, options, error);
  }
  return internal::OpenTcpStream(url, options, error);
}

// One request/response exchange. The HTTP/1.1 code above is shared by http and
// https; only the byte stream differs.
bool FetchOnce(const Url& url, const FetchOptions& options, Response* out, std::string* error) {
  if (url.host.empty()) {
    SetError(error, (url.scheme == "https" ? "HTTPS" : "HTTP") + std::string(" URL has no host"));
    return false;
  }
  const std::unique_ptr<internal::Stream> stream = OpenStream(url, options, error);
  if (!stream) {
    return false;
  }
  if (!stream->WriteAll(BuildRequest(url), options.read_timeout_ms, error)) {
    return false;
  }
  std::string raw;
  if (!internal::ReadUntilEof(*stream, options.read_timeout_ms, ResponseReadLimit(options), &raw,
                              error)) {
    return false;
  }
  return ParseResponse(raw, options.max_body_bytes, out, error);
}

bool FetchFile(const Url& url, const FetchOptions& options, Response* out, std::string* error) {
  const std::string& path = url.path;
  if (path.empty() || path.front() != '/') {
    SetError(error, "file URL path must be absolute");
    return false;
  }
  std::error_code status_error;
  const std::filesystem::file_status status = std::filesystem::status(path, status_error);
  // Some standard libraries report a missing path as an ENOENT error code and
  // others as a not_found status; both cases are the same "not found" failure.
  if (status_error) {
    if (status_error == std::errc::no_such_file_or_directory) {
      SetError(error, "file not found: " + path);
    } else {
      SetError(error, "cannot access file '" + path + "': " + status_error.message());
    }
    return false;
  }
  if (!std::filesystem::exists(status)) {
    SetError(error, "file not found: " + path);
    return false;
  }
  if (std::filesystem::is_directory(status)) {
    SetError(error, "cannot read a directory: " + path);
    return false;
  }
  if (!std::filesystem::is_regular_file(status)) {
    SetError(error, "not a regular file: " + path);
    return false;
  }
  std::error_code size_error;
  const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    SetError(error, "cannot read the size of file '" + path + "': " + size_error.message());
    return false;
  }
  if (file_size > static_cast<std::uintmax_t>(options.max_body_bytes)) {
    SetError(error, "file is larger than the " + std::to_string(options.max_body_bytes) +
                        "-byte limit: " + path);
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    SetError(error, "cannot open file: " + path);
    return false;
  }
  std::string body{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  if (file.bad()) {
    SetError(error, "error while reading file: " + path);
    return false;
  }
  Response response;
  response.status = 200;
  response.body = std::move(body);
  response.final_url = url;
  *out = std::move(response);
  return true;
}

// --- Reusable keep-alive client helpers (M13) --------------------------------

// True when a comma-separated header value carries `token` (case-insensitively).
bool HeaderHasToken(std::string_view value, std::string_view token) {
  std::size_t position = 0;
  while (position <= value.size()) {
    const std::size_t comma = value.find(',', position);
    const std::size_t end = comma == std::string_view::npos ? value.size() : comma;
    if (EqualsIgnoreCase(TrimAsciiSpace(value.substr(position, end - position)), token)) {
      return true;
    }
    if (comma == std::string_view::npos) {
      return false;
    }
    position = comma + 1;
  }
  return false;
}

// The Client never retries a timeout, so the retry logic has to recognise it; the
// transports report them all with the "timed out" wording.
bool IsTimeoutError(const std::string& error) {
  return error.find("timed out") != std::string::npos;
}

// Incremental response reader for the Client path. Bytes read ahead are kept in
// `pending_`; any bytes after a complete response frame stay there for the next
// request on the same pooled connection instead of being discarded.
class ResponseReader {
 public:
  ResponseReader(internal::Stream* stream, int timeout_ms, std::string pending)
      : stream_(stream), timeout_ms_(timeout_ms), pending_(std::move(pending)) {
    received_bytes_ = !pending_.empty();
  }

  // True once any response byte has arrived from the server, for which the client
  // must never retry the request.
  bool received_bytes() const { return received_bytes_; }

  // Returns the bytes read beyond the current response frame (idle-pool leftovers).
  std::string TakePending() { return std::move(pending_); }

  // Reads the response head through the blank line. On success `head` excludes the
  // terminating blank line and is bounded by `limit` bytes.
  bool ReadHead(std::string* head, std::size_t limit, std::string* error) {
    for (;;) {
      const std::size_t end = pending_.find("\r\n\r\n");
      if (end != std::string::npos) {
        *head = pending_.substr(0, end);
        pending_.erase(0, end + 4);
        return true;
      }
      if (pending_.size() > limit) {
        SetError(error, "HTTP response headers exceed the size limit");
        return false;
      }
      if (!Fill(error)) {
        if (!error->empty()) {
          return false;
        }
        SetError(error, pending_.empty()
                            ? "connection closed before the response headers were received"
                            : "malformed HTTP response: missing header terminator");
        return false;
      }
    }
  }

  // Reads one CRLF-terminated line (chunk size line or trailer), excluding the CRLF.
  bool ReadLine(std::string* line, std::size_t limit, std::string_view eof_error,
                std::string* error) {
    for (;;) {
      const std::size_t end = pending_.find("\r\n");
      if (end != std::string::npos) {
        *line = pending_.substr(0, end);
        pending_.erase(0, end + 2);
        return true;
      }
      if (pending_.size() > limit) {
        SetError(error, "malformed chunked body: chunk line is too long");
        return false;
      }
      if (!Fill(error)) {
        if (!error->empty()) {
          return false;
        }
        SetError(error, std::string(eof_error));
        return false;
      }
    }
  }

  // Appends exactly `count` body bytes.
  bool ReadExactly(std::size_t count, std::string* out, std::string_view eof_error,
                   std::string* error) {
    std::size_t appended = 0;
    if (!pending_.empty()) {
      const std::size_t take = std::min(count, pending_.size());
      out->append(pending_.data(), take);
      pending_.erase(0, take);
      appended = take;
    }
    char buffer[16 * 1024];
    while (appended < count) {
      const std::size_t want = std::min(count - appended, sizeof(buffer));
      std::size_t read = 0;
      if (!stream_->ReadSome(buffer, want, &read, timeout_ms_, error)) {
        return false;
      }
      if (read == 0) {
        SetError(error, std::string(eof_error));
        return false;
      }
      received_bytes_ = true;
      out->append(buffer, read);
      appended += read;
    }
    return true;
  }

  // Appends body bytes until end of stream (only for non-persistent responses).
  bool ReadUntilEof(std::size_t limit, std::string* out, std::string* error) {
    out->append(pending_);
    pending_.clear();
    if (out->size() > limit) {
      SetError(error, "HTTP response body exceeds the size limit");
      return false;
    }
    char buffer[16 * 1024];
    for (;;) {
      std::size_t read = 0;
      if (!stream_->ReadSome(buffer, sizeof(buffer), &read, timeout_ms_, error)) {
        return false;
      }
      if (read == 0) {
        return true;
      }
      received_bytes_ = true;
      out->append(buffer, read);
      if (out->size() > limit) {
        SetError(error, "HTTP response body exceeds the size limit");
        return false;
      }
    }
  }

 private:
  // Reads more bytes into the pending buffer. Returns false at end of stream or on
  // error; `error` is set only for a real transport failure (EOF is not an error).
  bool Fill(std::string* error) {
    char buffer[16 * 1024];
    std::size_t read = 0;
    if (!stream_->ReadSome(buffer, sizeof(buffer), &read, timeout_ms_, error)) {
      return false;
    }
    if (read == 0) {
      return false;
    }
    received_bytes_ = true;
    pending_.append(buffer, read);
    return true;
  }

  internal::Stream* stream_ = nullptr;
  int timeout_ms_ = 0;
  std::string pending_;
  bool received_bytes_ = false;
};

// Reads and decodes a chunked body incrementally, including any trailer lines.
bool ReadChunkedBody(ResponseReader* reader, std::size_t max_body_bytes, std::string* body,
                     std::string* error) {
  constexpr std::size_t kMaxSize = std::numeric_limits<std::size_t>::max();
  for (;;) {
    std::string size_line;
    if (!reader->ReadLine(&size_line, kMaxHeaderBytes,
                          "malformed chunked body: missing chunk size line", error)) {
      return false;
    }
    const std::size_t extension = size_line.find(';');
    if (extension != std::string::npos) {
      size_line.resize(extension);
    }
    if (size_line.empty()) {
      SetError(error, "malformed chunked body: empty chunk size");
      return false;
    }
    std::size_t chunk_size = 0;
    for (const char character : size_line) {
      const int digit = HexDigitValue(character);
      if (digit < 0) {
        SetError(error, "malformed chunked body: invalid chunk size '" + size_line + "'");
        return false;
      }
      const auto value = static_cast<std::size_t>(digit);
      if (chunk_size > (kMaxSize - value) / 16) {
        SetError(error, "malformed chunked body: chunk size is too large");
        return false;
      }
      chunk_size = chunk_size * 16 + value;
    }
    if (chunk_size == 0) {
      // Consume trailer lines up to the final empty line; trailers are not exposed.
      for (;;) {
        std::string trailer;
        if (!reader->ReadLine(&trailer, kMaxHeaderBytes,
                              "malformed chunked body: missing terminating line", error)) {
          return false;
        }
        if (trailer.empty()) {
          return true;
        }
      }
    }
    if (chunk_size > max_body_bytes - body->size()) {
      SetError(error, "HTTP response body exceeds the size limit");
      return false;
    }
    if (!reader->ReadExactly(chunk_size, body, "truncated chunked body", error)) {
      return false;
    }
    std::string terminator;
    if (!reader->ReadExactly(2, &terminator, "truncated chunked body", error)) {
      return false;
    }
    if (terminator != "\r\n") {
      SetError(error, "malformed chunked body: missing chunk terminator");
      return false;
    }
  }
}

// Inflates one gzip member, bounded by `max_body_bytes` so a compression bomb cannot
// allocate an unbounded body.
bool InflateGzip(std::string_view raw, std::size_t max_body_bytes, std::string* body,
                 std::string* error) {
  z_stream state{};
  if (inflateInit2(&state, 16 + MAX_WBITS) != Z_OK) {
    SetError(error, "cannot decode gzip response body: cannot initialize zlib");
    return false;
  }
  struct InflateGuard {
    z_stream* state;
    ~InflateGuard() { inflateEnd(state); }
  } guard{&state};

  constexpr std::size_t kInputChunk = 1u << 20;
  std::size_t offset = 0;
  char output[16 * 1024];
  for (;;) {
    if (state.avail_in == 0 && offset < raw.size()) {
      const std::size_t chunk = std::min(raw.size() - offset, kInputChunk);
      state.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.data() + offset));
      state.avail_in = static_cast<uInt>(chunk);
      offset += chunk;
    }
    state.next_out = reinterpret_cast<Bytef*>(output);
    state.avail_out = static_cast<uInt>(sizeof(output));
    const int result = inflate(&state, Z_NO_FLUSH);
    const std::size_t produced = sizeof(output) - state.avail_out;
    if (produced != 0) {
      if (produced > max_body_bytes - body->size()) {
        SetError(error, "HTTP response body exceeds the size limit");
        return false;
      }
      body->append(output, produced);
    }
    if (result == Z_STREAM_END) {
      return true;
    }
    if (result != Z_OK && result != Z_BUF_ERROR) {
      const std::string detail =
          state.msg != nullptr ? std::string(state.msg) : std::string("invalid gzip data");
      SetError(error, "cannot decode gzip response body: " + detail);
      return false;
    }
    if (state.avail_in == 0 && offset == raw.size()) {
      SetError(error, "cannot decode gzip response body: truncated gzip stream");
      return false;
    }
  }
}

// Applies the response's Content-Encoding: gzip is inflated, identity passes through
// unchanged and every other coding is an ordinary error. Headers stay as received.
bool DecodeResponseBody(const std::vector<std::pair<std::string, std::string>>& headers,
                        std::size_t max_body_bytes, std::string raw_body, std::string* body,
                        std::string* error) {
  const std::string coding = TrimAsciiSpace(FindHeader(headers, "content-encoding"));
  if (coding.empty() || EqualsIgnoreCase(coding, "identity")) {
    *body = std::move(raw_body);
    return true;
  }
  if (EqualsIgnoreCase(coding, "gzip")) {
    if (raw_body.empty()) {
      body->clear();
      return true;
    }
    return InflateGzip(raw_body, max_body_bytes, body, error);
  }
  SetError(error, "unsupported Content-Encoding '" + coding + "'");
  return false;
}

// One request/response exchange over `stream`. `pending` carries leftover bytes from
// the previous response on a pooled connection. On success `*persistent` reports
// whether the connection may be pooled, `*pending_out` holds unconsumed bytes and
// `*received_bytes` reports whether any response byte arrived (which forbids retry).
bool PerformExchange(internal::Stream& stream, std::string pending, const Url& url,
                     const FetchOptions& options, Response* response, bool* persistent,
                     std::string* pending_out, bool* received_bytes, std::string* error) {
  ResponseReader reader(&stream, options.read_timeout_ms, std::move(pending));
  *received_bytes = reader.received_bytes();
  if (!stream.WriteAll(BuildClientRequest(url), options.read_timeout_ms, error)) {
    return false;
  }
  std::string head;
  if (!reader.ReadHead(&head, kMaxHeaderBytes, error)) {
    *received_bytes = reader.received_bytes();
    return false;
  }
  *received_bytes = reader.received_bytes();
  if (!ParseResponseHead(head, response, error)) {
    return false;
  }

  const std::string_view connection = FindHeader(response->headers, "connection");
  bool exchange_allows_persistence = false;
  if (head.starts_with("HTTP/1.1")) {
    exchange_allows_persistence = !HeaderHasToken(connection, "close");
  } else if (head.starts_with("HTTP/1.0")) {
    exchange_allows_persistence = HeaderHasToken(connection, "keep-alive");
  }

  const std::string_view transfer_encoding = FindHeader(response->headers, "transfer-encoding");
  const std::string_view content_length_text = FindHeader(response->headers, "content-length");
  std::string body;
  bool delimited = false;
  if (!transfer_encoding.empty()) {
    if (!EqualsIgnoreCase(transfer_encoding, "chunked")) {
      SetError(error, "unsupported Transfer-Encoding '" + std::string(transfer_encoding) + "'");
      return false;
    }
    if (!ReadChunkedBody(&reader, options.max_body_bytes, &body, error)) {
      return false;
    }
    delimited = true;
  } else if (!content_length_text.empty()) {
    std::size_t length = 0;
    if (!ParseContentLength(content_length_text, &length, error)) {
      return false;
    }
    if (length > options.max_body_bytes) {
      SetError(error, "HTTP response body exceeds the size limit");
      return false;
    }
    if (!reader.ReadExactly(length, &body, "truncated HTTP body", error)) {
      return false;
    }
    delimited = true;
  } else {
    if (exchange_allows_persistence) {
      SetError(error, "HTTP response has no length delimiter for a persistent connection");
      return false;
    }
    if (!reader.ReadUntilEof(options.max_body_bytes, &body, error)) {
      return false;
    }
  }
  if (!DecodeResponseBody(response->headers, options.max_body_bytes, std::move(body),
                          &response->body, error)) {
    return false;
  }
  *persistent = delimited && exchange_allows_persistence;
  *pending_out = reader.TakePending();
  return true;
}

}  // namespace

std::string Response::HeaderValue(std::string_view name) const {
  const std::string_view value = FindHeader(headers, name);
  return std::string(value);
}

bool Fetch(const Url& url, const FetchOptions& options, Response* out, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (out == nullptr) {
    SetError(error, "Fetch requires a non-null response");
    return false;
  }
  if (url.scheme == "file") {
    return FetchFile(url, options, out, error);
  }
  if (url.scheme != "http" && url.scheme != "https") {
    SetError(error, "unsupported URL scheme '" + url.scheme + "'");
    return false;
  }

  Url current = url;
  for (int redirects = 0;;) {
    Response response;
    if (!FetchOnce(current, options, &response, error)) {
      return false;
    }
    response.final_url = current;
    if (IsRedirectStatus(response.status)) {
      const std::string location = response.HeaderValue("location");
      if (!location.empty()) {
        if (redirects >= options.max_redirects) {
          SetError(error, "too many redirects while fetching " + SerializeUrl(url));
          return false;
        }
        const std::optional<Url> next = ResolveUrl(current, location, error);
        if (!next) {
          return false;
        }
        if (next->scheme != "http" && next->scheme != "https") {
          SetError(error, "redirect to unsupported URL scheme '" + next->scheme + "'");
          return false;
        }
        current = *next;
        ++redirects;
        continue;
      }
    }
    *out = std::move(response);
    return true;
  }
}

// A reusable, single-threaded HTTP/1.1 client. The pimpl keeps one idle connection
// per origin; requests are serial, so a connection is either in the pool or in use.
struct Client::Impl {
  struct IdleConnection {
    std::string origin;
    std::unique_ptr<internal::Stream> stream;
    // Bytes read past the previous response frame, kept for the next request.
    std::string pending;
  };

  std::vector<IdleConnection> idle;

  static std::string OriginKey(const Url& url) {
    return url.scheme + "://" + url.host + ":" + std::to_string(url.port);
  }

  // Removes and returns the pooled connection for this origin, if any.
  std::unique_ptr<IdleConnection> TakeIdle(const Url& url) {
    const std::string key = OriginKey(url);
    for (auto entry = idle.begin(); entry != idle.end(); ++entry) {
      if (entry->origin == key) {
        auto connection = std::make_unique<IdleConnection>(std::move(*entry));
        idle.erase(entry);
        return connection;
      }
    }
    return nullptr;
  }

  // Pools a connection that finished a persistent exchange (at most one per origin).
  void PutIdle(const Url& url, std::unique_ptr<internal::Stream> stream, std::string pending) {
    const std::string key = OriginKey(url);
    for (auto entry = idle.begin(); entry != idle.end(); ++entry) {
      if (entry->origin == key) {
        idle.erase(entry);
        break;
      }
    }
    idle.push_back(IdleConnection{key, std::move(stream), std::move(pending)});
  }

  // Performs one request/response exchange, reusing a pooled connection when one
  // exists and retrying once on a fresh connection when a stale pooled connection
  // fails before any response byte arrives. Timeouts and fresh connections are
  // never retried.
  bool FetchHttpOnce(const Url& url, const FetchOptions& options, Response* out,
                     std::string* error) {
    if (url.host.empty()) {
      SetError(error, (url.scheme == "https" ? "HTTPS" : "HTTP") + std::string(" URL has no host"));
      return false;
    }
    if (std::unique_ptr<IdleConnection> pooled = TakeIdle(url); pooled != nullptr) {
      Response response;
      bool persistent = false;
      bool received_bytes = false;
      std::string pending_out;
      std::string attempt_error;
      if (PerformExchange(*pooled->stream, std::move(pooled->pending), url, options, &response,
                          &persistent, &pending_out, &received_bytes, &attempt_error)) {
        if (persistent) {
          PutIdle(url, std::move(pooled->stream), std::move(pending_out));
        }
        *out = std::move(response);
        return true;
      }
      pooled->stream.reset();
      if (received_bytes || IsTimeoutError(attempt_error)) {
        SetError(error, std::move(attempt_error));
        return false;
      }
      // The stale idle connection failed before answering: retry once, fresh.
    }

    std::unique_ptr<internal::Stream> stream = OpenStream(url, options, error);
    if (!stream) {
      return false;
    }
    Response response;
    bool persistent = false;
    bool received_bytes = false;
    std::string pending_out;
    if (!PerformExchange(*stream, std::string(), url, options, &response, &persistent, &pending_out,
                         &received_bytes, error)) {
      return false;
    }
    if (persistent) {
      PutIdle(url, std::move(stream), std::move(pending_out));
    }
    *out = std::move(response);
    return true;
  }
};

Client::Client() : impl_(std::make_unique<Impl>()) {}

Client::~Client() = default;

Client::Client(Client&&) noexcept = default;

Client& Client::operator=(Client&&) noexcept = default;

void Client::CloseIdleConnections() {
  if (impl_ != nullptr) {
    impl_->idle.clear();
  }
}

bool Client::Fetch(const Url& url, const FetchOptions& options, Response* out, std::string* error) {
  std::string ignored_error;
  std::string* error_out = error != nullptr ? error : &ignored_error;
  error_out->clear();
  if (out == nullptr) {
    SetError(error_out, "Client::Fetch requires a non-null response");
    return false;
  }
  if (impl_ == nullptr) {
    SetError(error_out, "Client::Fetch was called on a moved-from client");
    return false;
  }
  if (url.scheme == "file") {
    return FetchFile(url, options, out, error_out);
  }
  if (url.scheme != "http" && url.scheme != "https") {
    SetError(error_out, "unsupported URL scheme '" + url.scheme + "'");
    return false;
  }

  Url current = url;
  for (int redirects = 0;;) {
    Response response;
    if (!impl_->FetchHttpOnce(current, options, &response, error_out)) {
      return false;
    }
    response.final_url = current;
    if (IsRedirectStatus(response.status)) {
      const std::string location = response.HeaderValue("location");
      if (!location.empty()) {
        if (redirects >= options.max_redirects) {
          SetError(error_out, "too many redirects while fetching " + SerializeUrl(url));
          return false;
        }
        const std::optional<Url> next = ResolveUrl(current, location, error_out);
        if (!next) {
          return false;
        }
        if (next->scheme != "http" && next->scheme != "https") {
          SetError(error_out, "redirect to unsupported URL scheme '" + next->scheme + "'");
          return false;
        }
        current = *next;
        ++redirects;
        continue;
      }
    }
    *out = std::move(response);
    return true;
  }
}

}  // namespace toy::net
