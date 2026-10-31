// Plain TCP connection to the HTTP/1.1 client code.

#include <poll.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "stream_internal.h"

namespace toy::net::internal {
namespace {

constexpr std::size_t kReadBufferBytes = 16 * 1024;

void SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

}  // namespace

bool SocketStream::WriteAll(std::string_view data, int timeout_ms, std::string* error) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t sent =
        ::send(socket_.get(), data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
    if (sent > 0) {
      offset += static_cast<std::size_t>(sent);
      continue;
    }
    if (sent < 0 && errno == EINTR) {
      continue;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      const int ready = WaitForEvents(socket_.get(), POLLOUT, timeout_ms, error);
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

bool SocketStream::ReadSome(char* buffer, std::size_t capacity, std::size_t* read, int timeout_ms,
                            std::string* error) {
  for (;;) {
    const ssize_t received = ::recv(socket_.get(), buffer, capacity, 0);
    if (received > 0) {
      *read = static_cast<std::size_t>(received);
      return true;
    }
    if (received == 0) {
      *read = 0;
      return true;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      const int ready = WaitForEvents(socket_.get(), POLLIN, timeout_ms, error);
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

bool ReadUntilEof(Stream& stream, int timeout_ms, std::size_t limit, std::string* data,
                  std::string* error) {
  char buffer[kReadBufferBytes];
  for (;;) {
    std::size_t read = 0;
    if (!stream.ReadSome(buffer, sizeof(buffer), &read, timeout_ms, error)) {
      return false;
    }
    if (read == 0) {
      return true;
    }
    data->append(buffer, read);
    if (data->size() > limit) {
      SetError(error, "HTTP response exceeds the size limit");
      return false;
    }
  }
}

std::unique_ptr<Stream> OpenTcpStream(const Url& url, const FetchOptions& options,
                                      std::string* error) {
  Socket socket;
  if (!ConnectTcp(url, options.connect_timeout_ms, &socket, error)) {
    return nullptr;
  }
  return std::make_unique<SocketStream>(std::move(socket));
}

}  // namespace toy::net::internal
