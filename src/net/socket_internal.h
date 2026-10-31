#pragma once

// Private TCP socket helpers shared by the plain and TLS transports.

#include <unistd.h>

#include <string>

#include "toy/net/url.h"

namespace toy::net::internal {

// Owns one connected (non-blocking) socket descriptor.
class Socket {
 public:
  Socket() = default;
  explicit Socket(int fd) : fd_(fd) {}
  ~Socket() { Close(); }

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket(Socket&& other) noexcept : fd_(other.Release()) {}
  Socket& operator=(Socket&& other) noexcept {
    if (this != &other) {
      Close();
      fd_ = other.Release();
    }
    return *this;
  }

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
int WaitForEvents(int fd, short events, int timeout_ms, std::string* error);

// Connects to `url` over TCP (the URL supplies the host and port). Returns false
// with `error` (or the best per-address failure) when every address fails.
bool ConnectTcp(const Url& url, int timeout_ms, Socket* connected, std::string* error);

}  // namespace toy::net::internal
