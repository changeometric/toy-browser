#pragma once

// In-process loopback TLS server for deterministic `https://` tests.
//
// It listens on 127.0.0.1 with an ephemeral port and serves HTTP/1.1 responses over TLS using the
// committed test-only certificate fixtures in `tests/fixtures/tls/`. Tests provide a handler that
// maps the request target to a response; nothing here touches the public network, a public CA or
// the machine's certificate store.
//
// Usage:
//   #include "support/tls_server.h"
//
//   toy_test::TlsServer server;
//   std::string error;
//   if (!server.Start(cert_path, key_path, handler, &error)) { /* report error */ }
//   const std::string url = server.Url("/index.html");
//   // ... fetch url with the transport under test ...
//   server.Stop();  // optional; the destructor stops it too
//
// The header is test infrastructure shared by the net and shell test executables, so it is
// header-only: every test that links OpenSSL can use it without adding a shared library target.
// Keep production code out of this file.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace toy_test {

// One canned HTTP/1.1 response. The server adds `Content-Length`, `Connection: close` and the
// status line, writes the body and closes the connection.
struct HttpResponse {
  int status = 200;
  // Extra response headers, written in order before Content-Length.
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};

// A TLS server thread that answers requests until stopped. Every accepted connection is served
// sequentially (the tests fetch one resource at a time), with a request-header read and a response
// write bounded by a socket timeout so a misbehaving client cannot hang the thread forever.
class TlsServer {
 public:
  // Returns the response for a request target such as "/index.html?x=1".
  using Handler = std::function<HttpResponse(std::string_view request_target)>;

  TlsServer() = default;
  ~TlsServer() { Stop(); }

  TlsServer(const TlsServer&) = delete;
  TlsServer& operator=(const TlsServer&) = delete;

  // Starts serving TLS on 127.0.0.1 with the given PEM certificate chain and private key.
  // Returns false with a readable *error when the files cannot be loaded or the listener cannot be
  // created; on failure the server is fully stopped.
  bool Start(const std::string& certificate_pem, const std::string& private_key_pem,
             Handler handler, std::string* error) {
    Stop();
    ClearError(error);
    ctx_ = SSL_CTX_new(TLS_server_method());
    if (ctx_ == nullptr) {
      SetError(error, "cannot create the TLS server context: " + SslErrorText());
      return false;
    }
    if (SSL_CTX_use_certificate_chain_file(ctx_, certificate_pem.c_str()) != 1) {
      SetError(error, "cannot load TLS certificate '" + certificate_pem + "': " + SslErrorText());
      Stop();
      return false;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx_, private_key_pem.c_str(), SSL_FILETYPE_PEM) != 1) {
      SetError(error, "cannot load TLS private key '" + private_key_pem + "': " + SslErrorText());
      Stop();
      return false;
    }
    if (SSL_CTX_check_private_key(ctx_) != 1) {
      SetError(error, "TLS certificate and private key do not match: " + SslErrorText());
      Stop();
      return false;
    }
    handler_ = std::move(handler);
    tls_ = true;
    if (!OpenListener(error)) {
      Stop();
      return false;
    }
    stop_.store(false);
    thread_ = std::thread([this] { AcceptLoop(); });
    return true;
  }

  // Starts accepting TCP connections but never completes the TLS handshake. Used to test the
  // client's TLS handshake timeout without sleeping in the test.
  bool StartStalled(std::string* error) {
    Stop();
    ClearError(error);
    handler_ = nullptr;
    tls_ = false;
    if (!OpenListener(error)) {
      Stop();
      return false;
    }
    stop_.store(false);
    thread_ = std::thread([this] { AcceptLoop(); });
    return true;
  }

  // Stops the server and joins its thread. Idempotent; also called by the destructor.
  void Stop() {
    stop_.store(true);
    if (thread_.joinable()) {
      thread_.join();
    }
    if (ctx_ != nullptr) {
      SSL_CTX_free(ctx_);
      ctx_ = nullptr;
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

  // "https://localhost:<port><path>" for an absolute request path.
  std::string Url(std::string_view path) const {
    return "https://localhost:" + std::to_string(port_) + std::string(path);
  }

 private:
  static constexpr std::size_t kMaxRequestBytes = 64 * 1024;
  static constexpr int kPollTimeoutMs = 20;
  static constexpr int kSocketTimeoutSeconds = 5;

  void AcceptLoop() {
    while (!stop_.load()) {
      pollfd entry{};
      entry.fd = listen_fd_;
      entry.events = POLLIN;
      const int ready = ::poll(&entry, 1, kPollTimeoutMs);
      if (ready <= 0 || (entry.revents & POLLIN) == 0) {
        continue;
      }
      const int fd = ::accept(listen_fd_, nullptr, nullptr);
      if (fd < 0) {
        continue;
      }
      if (!tls_) {
        // Hold the connection open without speaking TLS, then close on shutdown.
        while (!stop_.load()) {
          ::poll(nullptr, 0, kPollTimeoutMs);
        }
        ::close(fd);
        continue;
      }
      ServeConnection(fd);
    }
  }

  void ServeConnection(int fd) {
    timeval timeout{};
    timeout.tv_sec = kSocketTimeoutSeconds;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    SSL* ssl = SSL_new(ctx_);
    if (ssl == nullptr) {
      ::close(fd);
      return;
    }
    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) <= 0) {
      ERR_clear_error();
      SSL_free(ssl);
      ::close(fd);
      return;
    }

    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < kMaxRequestBytes) {
      const int received = SSL_read(ssl, buffer, static_cast<int>(sizeof(buffer)));
      if (received <= 0) {
        break;
      }
      request.append(buffer, static_cast<std::size_t>(received));
    }

    const HttpResponse response =
        handler_ != nullptr ? handler_(RequestTarget(request)) : HttpResponse{};
    const std::string raw = BuildResponse(response);
    std::size_t offset = 0;
    while (offset < raw.size()) {
      const std::size_t remaining = raw.size() - offset;
      const std::size_t chunk = remaining < 16384 ? remaining : 16384;
      const int written = SSL_write(ssl, raw.data() + offset, static_cast<int>(chunk));
      if (written <= 0) {
        break;
      }
      offset += static_cast<std::size_t>(written);
    }
    // Send close_notify once (best effort) so a well-behaved client sees a clean EOF; clients must
    // still treat an unclean EOF as end of stream, because real servers vary.
    SSL_shutdown(ssl);
    ERR_clear_error();
    SSL_free(ssl);
    ::close(fd);
  }

  bool OpenListener(std::string* error) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
      SetError(error, "cannot create the test server socket: " + SysErrorText());
      return false;
    }
    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      SetError(error, "cannot bind the test server socket: " + SysErrorText());
      return false;
    }
    socklen_t address_size = sizeof(address);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &address_size) != 0) {
      SetError(error, "cannot read the test server port: " + SysErrorText());
      return false;
    }
    port_ = ntohs(address.sin_port);
    if (::listen(listen_fd_, 16) != 0) {
      SetError(error, "cannot listen on the test server socket: " + SysErrorText());
      return false;
    }
    return true;
  }

  static std::string RequestTarget(const std::string& request) {
    const std::size_t method_end = request.find(' ');
    if (method_end == std::string::npos) {
      return "/";
    }
    const std::size_t target_end = request.find(' ', method_end + 1);
    if (target_end == std::string::npos) {
      return "/";
    }
    return request.substr(method_end + 1, target_end - method_end - 1);
  }

  static std::string BuildResponse(const HttpResponse& response) {
    std::string raw =
        "HTTP/1.1 " + std::to_string(response.status) + " " + StatusText(response.status) + "\r\n";
    for (const auto& [name, value] : response.headers) {
      raw += name + ": " + value + "\r\n";
    }
    raw += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    raw += "Connection: close\r\n\r\n";
    raw += response.body;
    return raw;
  }

  static const char* StatusText(int status) {
    switch (status) {
      case 200:
        return "OK";
      case 302:
        return "Found";
      case 404:
        return "Not Found";
      default:
        return "OK";
    }
  }

  static void ClearError(std::string* error) {
    if (error != nullptr) {
      error->clear();
    }
  }

  static void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
      *error = std::move(message);
    }
  }

  static std::string SslErrorText() {
    const unsigned long code = ERR_get_error();
    if (code == 0) {
      return "no OpenSSL error recorded";
    }
    char buffer[256];
    ERR_error_string_n(code, buffer, sizeof(buffer));
    ERR_clear_error();
    return std::string(buffer);
  }

  static std::string SysErrorText() { return std::string(std::strerror(errno)); }

  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  SSL_CTX* ctx_ = nullptr;
  bool tls_ = true;
  Handler handler_;
  std::atomic<bool> stop_{true};
  std::thread thread_;
};

}  // namespace toy_test
