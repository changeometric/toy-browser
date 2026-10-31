#pragma once

// In-process loopback HTTP server for deterministic `http://` tests.
//
// It listens on 127.0.0.1 with an ephemeral port and serves requests; tests
// provide a handler that maps the raw request to a raw response. The server
// records every request, counts accepted connections, and keeps a connection open
// for further requests unless the response says `Connection: close` (the
// `RawHttpResponse` helper always does) or the test enables
// `SetForceCloseAfterResponse` — so it can exercise HTTP/1.1 keep-alive clients
// (M13) without the public network.
//
// Usage:
//   #include "support/http_server.h"
//
//   toy_test::HttpServer server;
//   std::string error;
//   if (!server.Start([](std::string_view request) {
//         return toy_test::RawHttpResponse(200, "text/css", "p { color: #ff0000 }");
//       }, &error)) { /* report error */ }
//   const std::string url = server.Url("/index.html");
//   // ... fetch `url` with the code under test ...
//   server.Stop();  // optional; the destructor stops it too
//
// The header is header-only shared test infrastructure: every test executable can
// link the existing `toy_test_support` interface target and use it. Keep
// production code out of this file.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace toy_test {

// A complete raw HTTP/1.1 response with Content-Length and Connection: close.
// The body is sent verbatim (no escaping, no compression).
inline std::string RawHttpResponse(int status, std::string_view content_type,
                                   std::string_view body) {
  std::string response = "HTTP/1.1 " + std::to_string(status) + " ";
  switch (status) {
    case 200:
      response += "OK";
      break;
    case 302:
      response += "Found";
      break;
    case 404:
      response += "Not Found";
      break;
    default:
      response += "Status";
      break;
  }
  response += "\r\nContent-Type: ";
  response.append(content_type);
  response += "\r\nContent-Length: " + std::to_string(body.size());
  response += "\r\nConnection: close\r\n\r\n";
  response.append(body);
  return response;
}

// A loopback HTTP/1.1 server thread. Connections are served sequentially with
// request-read and response-write socket timeouts, so a misbehaving client
// cannot hang the server thread forever.
class HttpServer {
 public:
  // Returns the complete raw response for one raw request.
  using Handler = std::function<std::string(std::string_view request)>;

  HttpServer() = default;
  ~HttpServer() { Stop(); }

  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  // Starts accepting on 127.0.0.1 with an ephemeral port. Returns false with a
  // readable *error when the listener cannot be created; on failure the server
  // is fully stopped.
  bool Start(Handler handler, std::string* error) {
    Stop();
    ClearError(error);
    if (!OpenListener(error)) {
      Stop();
      return false;
    }
    handler_ = std::move(handler);
    stop_.store(false);
    thread_ = std::thread([this] { AcceptLoop(); });
    return true;
  }

  // Stops the server and joins its thread. Idempotent; also called by the
  // destructor.
  void Stop() {
    stop_.store(true);
    if (thread_.joinable()) {
      thread_.join();
    }
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
    handler_ = nullptr;
    port_ = 0;
  }

  // True while the listener is open.
  bool started() const { return listen_fd_ >= 0; }

  // The ephemeral port the server listens on, or 0 when stopped.
  std::uint16_t port() const { return port_; }

  // "http://127.0.0.1:<port><path>" for an absolute request path.
  std::string Url(std::string_view path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + std::string(path);
  }

  // Number of requests received so far (headers complete).
  int request_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(requests_.size());
  }

  // Snapshot of the raw requests received so far, in arrival order.
  std::vector<std::string> requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
  }

  // Number of TCP connections accepted so far. A keep-alive client must keep this
  // at 1 while reusing one connection across several requests.
  int connection_count() const { return connections_.load(); }

  // When enabled, the server closes each connection right after writing its next
  // response without announcing it with `Connection: close` (used to simulate a
  // server that drops an idle keep-alive connection).
  void SetForceCloseAfterResponse(bool enabled) {
    force_close_after_response_.store(enabled);
  }

 private:
  static constexpr std::size_t kMaxRequestBytes = 64 * 1024;
  static constexpr int kPollTimeoutMs = 20;
  static constexpr int kSocketTimeoutSeconds = 5;

  static void ClearError(std::string* error) {
    if (error != nullptr) {
      error->clear();
    }
  }

  static void SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
      *error = message;
    }
  }

  bool OpenListener(std::string* error) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      SetError(error, std::string("cannot create the test HTTP listener: ") + std::strerror(errno));
      return false;
    }
    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      SetError(error, std::string("cannot bind the test HTTP listener: ") + std::strerror(errno));
      return false;
    }
    socklen_t address_size = sizeof(address);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &address_size) != 0) {
      SetError(error, std::string("cannot read the test HTTP port: ") + std::strerror(errno));
      return false;
    }
    port_ = ntohs(address.sin_port);
    if (::listen(listen_fd_, 8) != 0) {
      SetError(error, std::string("cannot listen on the test HTTP port: ") + std::strerror(errno));
      return false;
    }
    return true;
  }

  void AcceptLoop() {
    while (!stop_.load()) {
      pollfd entry{};
      entry.fd = listen_fd_;
      entry.events = POLLIN;
      const int ready = ::poll(&entry, 1, kPollTimeoutMs);
      if (ready <= 0) {
        continue;
      }
      const int client = ::accept(listen_fd_, nullptr, nullptr);
      if (client < 0) {
        continue;
      }
      connections_.fetch_add(1);
      Serve(client);
    }
  }

  // Serves requests on one accepted connection until the client closes it, the
  // response asks to close, the test forces a close, or the socket times out.
  void Serve(int client) {
    timeval socket_timeout{};
    socket_timeout.tv_sec = kSocketTimeoutSeconds;
    socket_timeout.tv_usec = 0;
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &socket_timeout, sizeof(socket_timeout));
    ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout, sizeof(socket_timeout));

    while (!stop_.load()) {
      std::string request;
      char buffer[4096];
      bool complete = false;
      while (request.size() < kMaxRequestBytes && !stop_.load()) {
        const ssize_t received = ::recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
          break;
        }
        request.append(buffer, static_cast<std::size_t>(received));
        if (request.find("\r\n\r\n") != std::string::npos) {
          complete = true;
          break;
        }
      }
      if (!complete || !handler_) {
        break;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(request);
      }
      const std::string response = handler_(request);
      if (response.empty()) {
        break;
      }
      std::size_t written = 0;
      while (written < response.size()) {
        const ssize_t sent =
            ::send(client, response.data() + written, response.size() - written, MSG_NOSIGNAL);
        if (sent <= 0) {
          break;
        }
        written += static_cast<std::size_t>(sent);
      }
      if (written < response.size() || force_close_after_response_.load() ||
          WantsClose(response)) {
        break;
      }
    }
    ::close(client);
  }

  // True when a raw response carries a `Connection: close` header. The comparison
  // is case-insensitive and deliberately simple: test handlers emit small,
  // controlled responses.
  static bool WantsClose(const std::string& response) {
    std::string lowered = response;
    for (char& character : lowered) {
      if (character >= 'A' && character <= 'Z') {
        character = static_cast<char>(character - 'A' + 'a');
      }
    }
    return lowered.find("\nconnection: close") != std::string::npos;
  }

  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  std::atomic<bool> stop_{false};
  std::atomic<bool> force_close_after_response_{false};
  std::atomic<int> connections_{0};
  Handler handler_;
  std::thread thread_;
  mutable std::mutex mutex_;
  std::vector<std::string> requests_;
};

}  // namespace toy_test
