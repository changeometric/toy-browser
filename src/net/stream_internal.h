#pragma once

// Private byte-stream abstraction behind the shared HTTP/1.1 client code: the
// plain TCP transport and the TLS transport differ only in this interface, so
// body caps, header parsing, chunked decoding and timeouts stay in one place.

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "socket_internal.h"
#include "toy/net/http.h"
#include "toy/net/url.h"

namespace toy::net::internal {

// A connected bidirectional byte stream.
class Stream {
 public:
  virtual ~Stream() = default;
  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;

  // Writes all bytes, waiting at most `timeout_ms` for each poll.
  virtual bool WriteAll(std::string_view data, int timeout_ms, std::string* error) = 0;

  // Reads up to `capacity` bytes. On success sets `*read` to the byte count, with
  // 0 meaning end of stream. On failure returns false with `error` set.
  virtual bool ReadSome(char* buffer, std::size_t capacity, std::size_t* read, int timeout_ms,
                        std::string* error) = 0;

 protected:
  Stream() = default;
};

// Reads from `stream` until end of stream, appending to `data`. Fails once `limit`
// bytes are exceeded; on failure `data` may hold a partial response.
bool ReadUntilEof(Stream& stream, int timeout_ms, std::size_t limit, std::string* data,
                  std::string* error);

// Plain TCP stream over an already connected non-blocking socket.
class SocketStream final : public Stream {
 public:
  explicit SocketStream(Socket socket) : socket_(std::move(socket)) {}

  bool WriteAll(std::string_view data, int timeout_ms, std::string* error) override;
  bool ReadSome(char* buffer, std::size_t capacity, std::size_t* read, int timeout_ms,
                std::string* error) override;

 private:
  Socket socket_;
};

// Connects over TCP and returns a plain stream, or null with `error` set.
std::unique_ptr<Stream> OpenTcpStream(const Url& url, const FetchOptions& options,
                                      std::string* error);

}  // namespace toy::net::internal
