// Tests for toy::net::Fetch over TLS against in-process loopback servers.
//
// Every test uses the committed test-only certificates in tests/fixtures/tls/
// and the loopback servers below; nothing here touches the public network, a
// public CA or the machine's certificate store. Trust is set by CTest via
// SSL_CERT_FILE, exactly like a normal system trust-store override.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "support/check.h"
#include "support/tls_server.h"
#include "toy/net/http.h"
#include "toy/net/url.h"

namespace {

using toy::net::Fetch;
using toy::net::FetchOptions;
using toy::net::ParseUrl;
using toy::net::Response;
using toy::net::Url;

std::string TlsFixture(const char* name) {
  return std::string(TOYBROWSER_TLS_FIXTURES) + "/" + name;
}

bool MakeUrl(const std::string& text, Url* url) {
  std::string error;
  const std::optional<Url> parsed = ParseUrl(text, &error);
  if (!parsed) {
    std::fprintf(stderr, "tls test: cannot build URL '%s': %s\n", text.c_str(), error.c_str());
    toy_test::Check(false, "the test URL parses");
    return false;
  }
  *url = *parsed;
  return true;
}

// Starts a loopback TLS server with the committed localhost leaf certificate.
bool StartLocalhostTlsServer(toy_test::TlsServer* server, toy_test::TlsServer::Handler handler) {
  std::string error;
  if (!server->Start(TlsFixture("localhost_cert.pem"), TlsFixture("localhost_key.pem"),
                     std::move(handler), &error)) {
    std::fprintf(stderr, "tls test: cannot start the TLS server: %s\n", error.c_str());
    toy_test::Check(false, "the TLS test server starts");
    return false;
  }
  return true;
}

toy_test::HttpResponse TextResponse(std::string body) {
  toy_test::HttpResponse response;
  response.status = 200;
  response.body = std::move(body);
  return response;
}

toy_test::HttpResponse RedirectResponse(std::string location) {
  toy_test::HttpResponse response;
  response.status = 302;
  response.headers.emplace_back("Location", std::move(location));
  return response;
}

// Minimal plain-HTTP loopback server. The TLS tests only need it for the
// https->http redirect case, so it keeps the request handling small.
class PlainHttpServer {
 public:
  using Handler = std::function<std::string(std::string_view request_target)>;

  PlainHttpServer() { OpenListener(); }
  ~PlainHttpServer() { Stop(); }

  PlainHttpServer(const PlainHttpServer&) = delete;
  PlainHttpServer& operator=(const PlainHttpServer&) = delete;

  bool ok() const { return listen_fd_ >= 0; }

  std::uint16_t port() const { return port_; }

  std::string Url(std::string_view path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + std::string(path);
  }

  void Start(Handler handler) {
    handler_ = std::move(handler);
    thread_ = std::thread([this] { Run(); });
  }

  void Stop() {
    stop_.store(true);
    if (thread_.joinable()) {
      thread_.join();
    }
    CloseListener();
  }

 private:
  void CloseListener() {
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
  }

  void OpenListener() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
      return;
    }
    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      CloseListener();
      return;
    }
    socklen_t address_size = sizeof(address);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &address_size) != 0) {
      CloseListener();
      return;
    }
    port_ = ntohs(address.sin_port);
    if (::listen(listen_fd_, 8) != 0) {
      CloseListener();
    }
  }

  void Run() {
    while (!stop_.load()) {
      pollfd entry{};
      entry.fd = listen_fd_;
      entry.events = POLLIN;
      const int ready = ::poll(&entry, 1, 20);
      if (ready <= 0) {
        continue;
      }
      const int client = ::accept(listen_fd_, nullptr, nullptr);
      if (client < 0) {
        continue;
      }
      Serve(client);
      ::close(client);
    }
  }

  void Serve(int client) {
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 64 * 1024) {
      const ssize_t received = ::recv(client, buffer, sizeof(buffer), 0);
      if (received <= 0) {
        return;
      }
      request.append(buffer, static_cast<std::size_t>(received));
    }
    const std::string response = handler_ != nullptr ? handler_(RequestTarget(request)) : "";
    std::size_t offset = 0;
    while (offset < response.size()) {
      const ssize_t sent =
          ::send(client, response.data() + offset, response.size() - offset, MSG_NOSIGNAL);
      if (sent <= 0) {
        return;
      }
      offset += static_cast<std::size_t>(sent);
    }
  }

  static std::string_view RequestTarget(const std::string& request) {
    const std::size_t method_end = request.find(' ');
    if (method_end == std::string::npos) {
      return "/";
    }
    const std::size_t target_end = request.find(' ', method_end + 1);
    if (target_end == std::string::npos) {
      return "/";
    }
    return std::string_view(request).substr(method_end + 1, target_end - method_end - 1);
  }

  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  Handler handler_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

std::string PlainResponse(std::string_view body) {
  return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" +
         std::string(body);
}

void TestTlsGet() {
  toy_test::TlsServer server;
  if (!StartLocalhostTlsServer(&server, [](std::string_view target) {
        toy_test::Check(target == "/hello?x=1", "the TLS request target reaches the server");
        toy_test::HttpResponse response = TextResponse("hello over tls");
        response.headers.emplace_back("Content-Type", "text/plain");
        response.headers.emplace_back("X-Test", "yes");
        return response;
      })) {
    return;
  }
  Url url;
  if (!MakeUrl(server.Url("/hello?x=1"), &url)) {
    return;
  }
  Response response;
  std::string error;
  toy_test::Check(Fetch(url, FetchOptions{}, &response, &error), "an https GET succeeds");
  toy_test::Check(error.empty(), "a successful https fetch clears the error");
  toy_test::Check(response.status == 200, "the https status is 200");
  toy_test::Check(response.body == "hello over tls", "the https body is read");
  toy_test::Check(response.HeaderValue("content-TYPE") == "text/plain",
                  "https response headers keep the case-insensitive lookup");
  toy_test::Check(response.HeaderValue("x-test") == "yes", "custom https headers are exposed");
  toy_test::Check(response.final_url.scheme == "https", "final_url keeps the https scheme");
  toy_test::Check(response.final_url.host == "localhost", "final_url keeps the host");
  toy_test::Check(response.final_url.port == server.port(), "final_url keeps the port");
  toy_test::Check(response.final_url.path == "/hello", "final_url keeps the request path");
  toy_test::Check(response.final_url.query == "x=1", "final_url keeps the request query");
}

void TestUntrustedCertificate() {
  toy_test::TlsServer server;
  std::string server_error;
  toy_test::Check(
      server.Start(
          TlsFixture("untrusted_cert.pem"), TlsFixture("untrusted_key.pem"),
          [](std::string_view) { return TextResponse("should not be read"); }, &server_error),
      "the untrusted-certificate test server starts");
  if (!server.started()) {
    std::fprintf(stderr, "tls test: cannot start the untrusted server: %s\n", server_error.c_str());
    return;
  }
  Url url;
  if (!MakeUrl(server.Url("/"), &url)) {
    return;
  }
  Response response;
  std::string error;
  toy_test::Check(!Fetch(url, FetchOptions{}, &response, &error),
                  "a certificate that does not chain to the trust store fails the fetch");
  toy_test::Check(error.find("certificate") != std::string::npos,
                  "the certificate failure error mentions the certificate");
}

void TestHostnameMismatch() {
  toy_test::TlsServer server;
  if (!StartLocalhostTlsServer(&server, [](std::string_view) { return TextResponse("no"); })) {
    return;
  }
  // The leaf certificate names DNS:localhost, so connecting by IP must fail.
  Url url;
  if (!MakeUrl("https://127.0.0.1:" + std::to_string(server.port()) + "/", &url)) {
    return;
  }
  Response response;
  std::string error;
  toy_test::Check(!Fetch(url, FetchOptions{}, &response, &error),
                  "a certificate that does not name the requested host fails the fetch");
  toy_test::Check(error.find("hostname") != std::string::npos,
                  "the hostname mismatch error mentions the hostname");
}

void TestHandshakeTimeout() {
  toy_test::TlsServer server;
  std::string server_error;
  toy_test::Check(server.StartStalled(&server_error), "the stalled handshake server starts");
  if (!server.started()) {
    std::fprintf(stderr, "tls test: cannot start the stalled server: %s\n", server_error.c_str());
    return;
  }
  Url url;
  if (!MakeUrl(server.Url("/slow"), &url)) {
    return;
  }
  FetchOptions options;
  options.connect_timeout_ms = 1000;
  options.read_timeout_ms = 200;
  Response response;
  std::string error;
  toy_test::Check(!Fetch(url, options, &response, &error),
                  "a server that never completes the TLS handshake hits the handshake timeout");
  toy_test::Check(error.find("timed out") != std::string::npos,
                  "the handshake timeout error mentions timing out");
}

void TestHttpsToHttpsRedirect() {
  toy_test::TlsServer server;
  if (!StartLocalhostTlsServer(&server, [](std::string_view target) {
        if (target == "/start") {
          return RedirectResponse("/final");
        }
        if (target == "/final") {
          return TextResponse("redirected over tls");
        }
        toy_test::HttpResponse response;
        response.status = 404;
        return response;
      })) {
    return;
  }
  Url url;
  if (!MakeUrl(server.Url("/start"), &url)) {
    return;
  }
  Response response;
  std::string error;
  toy_test::Check(Fetch(url, FetchOptions{}, &response, &error),
                  "an https->https redirect is followed");
  toy_test::Check(response.body == "redirected over tls", "the redirect target body is returned");
  toy_test::Check(response.final_url.path == "/final", "final_url is updated after the redirect");
  toy_test::Check(response.final_url.scheme == "https", "the redirect keeps the https scheme");
}

void TestHttpToHttpsRedirect() {
  toy_test::TlsServer tls_server;
  if (!StartLocalhostTlsServer(&tls_server, [](std::string_view target) {
        toy_test::Check(target == "/final", "the redirected request reaches the TLS server");
        return TextResponse("upgraded to tls");
      })) {
    return;
  }
  const std::string https_url = tls_server.Url("/final");

  PlainHttpServer plain_server;
  toy_test::Check(plain_server.ok(), "the plain redirect server listens");
  if (!plain_server.ok()) {
    return;
  }
  plain_server.Start([https_url](std::string_view) {
    return "HTTP/1.1 302 Found\r\nLocation: " + https_url + "\r\nContent-Length: 0\r\n\r\n";
  });
  Url url;
  if (!MakeUrl(plain_server.Url("/start"), &url)) {
    return;
  }
  Response response;
  std::string error;
  toy_test::Check(Fetch(url, FetchOptions{}, &response, &error),
                  "an http->https redirect is followed");
  toy_test::Check(response.body == "upgraded to tls", "the https target body is returned");
  toy_test::Check(response.final_url.scheme == "https", "final_url switches to https");
  toy_test::Check(response.final_url.port == tls_server.port(), "final_url keeps the TLS port");
}

void TestHttpsToHttpRedirect() {
  PlainHttpServer plain_server;
  toy_test::Check(plain_server.ok(), "the plain redirect server listens");
  if (!plain_server.ok()) {
    return;
  }
  plain_server.Start([](std::string_view target) {
    toy_test::Check(target == "/final", "the redirected request reaches the plain server");
    return PlainResponse("downgraded to plain");
  });
  const std::string plain_url = plain_server.Url("/final");

  toy_test::TlsServer tls_server;
  if (!StartLocalhostTlsServer(
          &tls_server, [plain_url](std::string_view) { return RedirectResponse(plain_url); })) {
    return;
  }
  Url url;
  if (!MakeUrl(tls_server.Url("/start"), &url)) {
    return;
  }
  Response response;
  std::string error;
  toy_test::Check(Fetch(url, FetchOptions{}, &response, &error),
                  "an https->http redirect is followed");
  toy_test::Check(response.body == "downgraded to plain", "the plain target body is returned");
  toy_test::Check(response.final_url.scheme == "http", "final_url switches to http");
  toy_test::Check(response.final_url.port == plain_server.port(),
                  "final_url keeps the plain server port");
}

void TestBodyLimit() {
  toy_test::TlsServer server;
  if (!StartLocalhostTlsServer(
          &server, [](std::string_view) { return TextResponse(std::string(100, 'x')); })) {
    return;
  }
  Url url;
  if (!MakeUrl(server.Url("/large"), &url)) {
    return;
  }
  FetchOptions options;
  options.max_body_bytes = 16;
  Response response;
  std::string error;
  toy_test::Check(!Fetch(url, options, &response, &error),
                  "the body cap applies to https responses too");
  toy_test::Check(error.find("size limit") != std::string::npos,
                  "the oversize https body error mentions the size limit");
}

}  // namespace

int main() {
  TestTlsGet();
  TestUntrustedCertificate();
  TestHostnameMismatch();
  TestHandshakeTimeout();
  TestHttpsToHttpsRedirect();
  TestHttpToHttpsRedirect();
  TestHttpsToHttpRedirect();
  TestBodyLimit();
  return toy_test::Finish("tls test");
}
