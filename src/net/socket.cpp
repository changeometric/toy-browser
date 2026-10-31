// TCP socket helpers shared by the plain and TLS transports.

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include "socket_internal.h"

namespace toy::net::internal {
namespace {

void SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

std::string EndpointText(const Url& url) { return url.host + ":" + std::to_string(url.port); }

}  // namespace

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

bool ConnectTcp(const Url& url, int timeout_ms, Socket* connected, std::string* error) {
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
    *connected = std::move(candidate);
    return true;
  }
  SetError(error, last_error);
  return false;
}

}  // namespace toy::net::internal
