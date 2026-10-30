// Blocking HTTP/1.1 GET client and file:// reader behind toy::net::Fetch.

#include "toy/net/http.h"

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "toy/net/url.h"
#include "url_internal.h"

namespace toy::net {
namespace {

constexpr const char* kUserAgent = "toybrowser/0.1";
// Read budget above the body limit, so a large-but-legal header block still fits.
constexpr std::size_t kMaxHeaderBytes = 64 * 1024;
constexpr std::size_t kReadBufferBytes = 16 * 1024;

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

class Socket {
 public:
  Socket() = default;
  explicit Socket(int fd) : fd_(fd) {}
  ~Socket() { Close(); }

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  int get() const { return fd_; }

  // Hands the descriptor to the caller, which then owns it.
  int Release() {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }

  void Close() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  int fd_ = -1;
};

// Waits for the socket to become ready. Returns 1 when ready, 0 on timeout and
// -1 on failure with `error` set.
int WaitForEvents(int fd, short events, int timeout_ms, std::string* error) {
  pollfd entry{};
  entry.fd = fd;
  entry.events = events;
  for (;;) {
    const int result = ::poll(&entry, 1, timeout_ms);
    if (result > 0) {
      if ((entry.revents & POLLNVAL) != 0) {
        SetError(error, "invalid socket while waiting for I/O");
        return -1;
      }
      return 1;
    }
    if (result == 0) {
      return 0;
    }
    if (errno == EINTR) {
      continue;
    }
    SetError(error, std::string("poll failed: ") + std::strerror(errno));
    return -1;
  }
}

std::string EndpointText(const Url& url) { return url.host + ":" + std::to_string(url.port); }

// Connects to url over TCP. Returns false with `error` (or the best per-address
// failure) when every address fails.
bool ConnectTcp(const Url& url, int timeout_ms, int* connected_fd, std::string* error) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  const std::string port_text = std::to_string(url.port);
  addrinfo* addresses = nullptr;
  const int lookup = ::getaddrinfo(url.host.c_str(), port_text.c_str(), &hints, &addresses);
  if (lookup != 0) {
    SetError(error, "cannot resolve host '" + url.host + "': " + ::gai_strerror(lookup));
    return false;
  }
  struct AddressList {
    addrinfo* head;
    ~AddressList() { ::freeaddrinfo(head); }
  } address_list{addresses};

  std::string last_error = "no usable address for host '" + url.host + "'";
  for (const addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
    const int fd = ::socket(address->ai_family, address->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK,
                            address->ai_protocol);
    if (fd < 0) {
      last_error = std::string("cannot create socket: ") + std::strerror(errno);
      continue;
    }
    Socket candidate(fd);
    const int result = ::connect(fd, address->ai_addr, address->ai_addrlen);
    if (result != 0 && errno != EINPROGRESS) {
      last_error = "cannot connect to " + EndpointText(url) + ": " + std::strerror(errno);
      continue;
    }
    if (result != 0) {
      std::string wait_error;
      const int ready = WaitForEvents(fd, POLLOUT, timeout_ms, &wait_error);
      if (ready <= 0) {
        last_error = ready == 0 ? "connect timed out to " + EndpointText(url) : wait_error;
        continue;
      }
      int socket_error = 0;
      socklen_t socket_error_size = sizeof(socket_error);
      if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) != 0) {
        last_error = std::string("cannot inspect socket state: ") + std::strerror(errno);
        continue;
      }
      if (socket_error != 0) {
        last_error = "cannot connect to " + EndpointText(url) + ": " + std::strerror(socket_error);
        continue;
      }
    }
    *connected_fd = candidate.Release();
    return true;
  }
  SetError(error, last_error);
  return false;
}

bool SendAll(int fd, std::string_view data, int timeout_ms, std::string* error) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t sent = ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
    if (sent > 0) {
      offset += static_cast<std::size_t>(sent);
      continue;
    }
    if (sent < 0 && errno == EINTR) {
      continue;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      const int ready = WaitForEvents(fd, POLLOUT, timeout_ms, error);
      if (ready == 0) {
        SetError(error, "send timed out");
      }
      if (ready != 1) {
        return false;
      }
      continue;
    }
    SetError(error, std::string("send failed: ") + std::strerror(errno));
    return false;
  }
  return true;
}

// Reads until the peer closes the connection, failing once `limit` bytes are exceeded.
bool ReadUntilEof(int fd, int timeout_ms, std::size_t limit, std::string* data,
                  std::string* error) {
  char buffer[kReadBufferBytes];
  for (;;) {
    const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
    if (received > 0) {
      data->append(buffer, static_cast<std::size_t>(received));
      if (data->size() > limit) {
        SetError(error, "HTTP response exceeds the size limit");
        return false;
      }
      continue;
    }
    if (received == 0) {
      return true;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      const int ready = WaitForEvents(fd, POLLIN, timeout_ms, error);
      if (ready == 0) {
        SetError(error, "read timed out");
      }
      if (ready != 1) {
        return false;
      }
      continue;
    }
    SetError(error, std::string("receive failed: ") + std::strerror(errno));
    return false;
  }
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

bool ParseResponse(std::string_view raw, std::size_t max_body_bytes, Response* response,
                   std::string* error) {
  constexpr std::size_t kNotFound = std::string_view::npos;
  const std::size_t header_end = raw.find("\r\n\r\n");
  if (header_end == kNotFound) {
    SetError(error, "malformed HTTP response: missing header terminator");
    return false;
  }
  const std::string_view head = raw.substr(0, header_end);
  const std::string_view raw_body = raw.substr(header_end + 4);

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

std::string BuildRequest(const Url& url) {
  std::string target = EncodePath(url.path.empty() ? std::string_view("/") : url.path);
  if (!url.query.empty()) {
    target += '?';
    target += EncodeQuery(url.query);
  }
  std::string host = url.host;
  if (url.port != 80) {
    host += ':';
    host += std::to_string(url.port);
  }
  std::string request;
  request += "GET " + target + " HTTP/1.1\r\n";
  request += "Host: " + host + "\r\n";
  request += "Connection: close\r\n";
  request += "User-Agent: ";
  request += kUserAgent;
  request += "\r\n\r\n";
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

bool LooksLikeAbsoluteUrl(std::string_view text) {
  constexpr std::size_t kNotFound = std::string_view::npos;
  const std::size_t colon = text.find(':');
  if (colon == kNotFound || colon == 0) {
    return false;
  }
  const std::size_t slash = text.find('/');
  if (slash != kNotFound && slash < colon) {
    return false;
  }
  const char first = text.front();
  if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))) {
    return false;
  }
  for (std::size_t index = 0; index < colon; ++index) {
    const char character = text[index];
    const bool valid = (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9') || character == '+' ||
                       character == '-' || character == '.';
    if (!valid) {
      return false;
    }
  }
  return true;
}

bool ResolveRedirect(const Url& base, const std::string& location, Url* next, std::string* error) {
  if (LooksLikeAbsoluteUrl(location)) {
    const std::optional<Url> parsed = ParseUrl(location, error);
    if (!parsed) {
      return false;
    }
    *next = *parsed;
    return true;
  }
  if (location.size() >= 2 && location[0] == '/' && location[1] == '/') {
    SetError(error, "unsupported protocol-relative redirect Location '" + location + "'");
    return false;
  }
  if (location.empty() || location.front() != '/') {
    SetError(error, "unsupported redirect Location '" + location +
                        "': only absolute and root-relative URLs are supported");
    return false;
  }
  std::string absolute = base.scheme + "://" + base.host;
  if (base.port != 80) {
    absolute += ':';
    absolute += std::to_string(base.port);
  }
  absolute += location;
  const std::optional<Url> parsed = ParseUrl(absolute, error);
  if (!parsed) {
    return false;
  }
  *next = *parsed;
  return true;
}

bool FetchHttpOnce(const Url& url, const FetchOptions& options, Response* out, std::string* error) {
  if (url.host.empty()) {
    SetError(error, "HTTP URL has no host");
    return false;
  }
  int connected_fd = -1;
  if (!ConnectTcp(url, options.connect_timeout_ms, &connected_fd, error)) {
    return false;
  }
  Socket socket(connected_fd);
  if (!SendAll(socket.get(), BuildRequest(url), options.read_timeout_ms, error)) {
    return false;
  }
  std::string raw;
  if (!ReadUntilEof(socket.get(), options.read_timeout_ms, ResponseReadLimit(options), &raw,
                    error)) {
    return false;
  }
  return ParseResponse(raw, options.max_body_bytes, out, error);
}

bool FetchFile(const Url& url, Response* out, std::string* error) {
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
    return FetchFile(url, out, error);
  }
  if (url.scheme != "http") {
    SetError(error, "unsupported URL scheme '" + url.scheme + "'");
    return false;
  }

  Url current = url;
  for (int redirects = 0;;) {
    Response response;
    if (!FetchHttpOnce(current, options, &response, error)) {
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
        Url next;
        if (!ResolveRedirect(current, location, &next, error)) {
          return false;
        }
        if (next.scheme != "http") {
          SetError(error, "redirect to unsupported URL scheme '" + next.scheme + "'");
          return false;
        }
        current = std::move(next);
        ++redirects;
        continue;
      }
    }
    *out = std::move(response);
    return true;
  }
}

}  // namespace toy::net
