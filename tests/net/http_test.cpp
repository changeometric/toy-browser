// Tests for toy::net::Fetch over HTTP/1.1 against in-process loopback servers.

#include "toy/net/http.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "toy/net/url.h"

namespace {

std::atomic<int> g_failures{0};

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "http test failed: %s\n", description);
    g_failures.fetch_add(1);
  }
}

// Minimal loopback HTTP server: it accepts connections until destroyed, reads
// one request per connection and writes the canned response returned by the
// handler. An empty response means "hold the connection open silently", which
// is used to exercise the client read timeout without sleeping in the test.
class LoopbackServer {
 public:
  using Handler = std::function<std::string(const std::string& request)>;

  LoopbackServer() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
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

  ~LoopbackServer() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
    CloseListener();
  }

  LoopbackServer(const LoopbackServer&) = delete;
  LoopbackServer& operator=(const LoopbackServer&) = delete;

  bool ok() const { return listen_fd_ >= 0; }
  std::uint16_t port() const { return port_; }

  void Start(Handler handler) {
    handler_ = std::move(handler);
    thread_ = std::thread([this] { Run(); });
  }

 private:
  void CloseListener() {
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
  }

  bool StopRequested() {
    std::lock_guard<std::mutex> lock(mutex_);
    return stop_;
  }

  void Run() {
    while (!StopRequested()) {
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
    while (request.find("\r\n\r\n") == std::string::npos) {
      const ssize_t received = ::recv(client, buffer, sizeof(buffer), 0);
      if (received <= 0) {
        return;
      }
      request.append(buffer, static_cast<std::size_t>(received));
      if (request.size() > 64 * 1024) {
        return;
      }
    }
    const std::string response = handler_(request);
    if (response.empty()) {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stop_; });
      return;
    }
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

  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  Handler handler_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stop_ = false;
};

bool MakeHttpUrl(std::uint16_t port, const std::string& path, toy::net::Url* url) {
  const std::string text = "http://127.0.0.1:" + std::to_string(port) + path;
  std::string error;
  const std::optional<toy::net::Url> parsed = toy::net::ParseUrl(text, &error);
  if (!parsed) {
    std::fprintf(stderr, "http test failed: cannot build URL '%s': %s\n", text.c_str(),
                 error.c_str());
    g_failures.fetch_add(1);
    return false;
  }
  *url = *parsed;
  return true;
}

void TestContentLength() {
  LoopbackServer server;
  Check(server.ok(), "the loopback server is listening");
  if (!server.ok()) {
    return;
  }
  server.Start([](const std::string& request) {
    Check(request.starts_with("GET /hello HTTP/1.1\r\n"), "request line is GET <path> HTTP/1.1");
    Check(request.find("\r\nHost: 127.0.0.1:") != std::string::npos,
          "Host header carries the non-default port");
    Check(request.find("\r\nConnection: close\r\n") != std::string::npos,
          "request asks to close the connection");
    Check(request.find("\r\nUser-Agent: ") != std::string::npos, "request has a User-Agent");
    return std::string(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "hello");
  });

  toy::net::Url url;
  if (!MakeHttpUrl(server.port(), "/hello", &url)) {
    return;
  }
  const toy::net::FetchOptions options;
  toy::net::Response response;
  std::string error;
  Check(toy::net::Fetch(url, options, &response, &error), "Fetch succeeds for a 200 response");
  Check(error.empty(), "a successful fetch clears the error");
  Check(response.status == 200, "status is 200");
  Check(response.body == "hello", "the Content-Length body is read");
  Check(response.HeaderValue("content-TYPE") == "text/plain",
        "HeaderValue matches header names case-insensitively");
  Check(response.final_url.path == "/hello", "final_url is the requested URL");
}

void TestChunkedBody() {
  LoopbackServer server;
  Check(server.ok(), "the loopback server is listening");
  if (!server.ok()) {
    return;
  }
  server.Start([](const std::string&) {
    return std::string(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n\r\n");
  });

  toy::net::Url url;
  if (!MakeHttpUrl(server.port(), "/chunked", &url)) {
    return;
  }
  toy::net::Response response;
  std::string error;
  Check(toy::net::Fetch(url, toy::net::FetchOptions{}, &response, &error),
        "chunked fetch succeeds");
  Check(response.body == "hello world", "the chunked body is decoded");
}

void TestAbsoluteRedirect() {
  LoopbackServer server;
  Check(server.ok(), "the loopback server is listening");
  if (!server.ok()) {
    return;
  }
  const std::uint16_t port = server.port();
  server.Start([port](const std::string& request) -> std::string {
    if (request.starts_with("GET /start ")) {
      return "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + std::to_string(port) +
             "/final\r\nContent-Length: 0\r\n\r\n";
    }
    if (request.starts_with("GET /final ")) {
      return "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\ndone";
    }
    return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
  });

  toy::net::Url url;
  if (!MakeHttpUrl(port, "/start", &url)) {
    return;
  }
  toy::net::Response response;
  std::string error;
  Check(toy::net::Fetch(url, toy::net::FetchOptions{}, &response, &error),
        "a 302 with an absolute Location is followed");
  Check(response.status == 200, "the redirect target status is 200");
  Check(response.body == "done", "the redirect target body is returned");
  Check(response.final_url.path == "/final", "final_url is updated after the redirect");
  Check(response.final_url.port == port, "final_url keeps the port of the redirect target");
}

void TestRootRelativeRedirect() {
  LoopbackServer server;
  Check(server.ok(), "the loopback server is listening");
  if (!server.ok()) {
    return;
  }
  server.Start([](const std::string& request) -> std::string {
    if (request.starts_with("GET /start ")) {
      return "HTTP/1.1 301 Moved Permanently\r\nLocation: /final\r\nContent-Length: 0\r\n\r\n";
    }
    if (request.starts_with("GET /final ")) {
      return "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    }
    return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
  });

  toy::net::Url url;
  if (!MakeHttpUrl(server.port(), "/start", &url)) {
    return;
  }
  toy::net::Response response;
  std::string error;
  Check(toy::net::Fetch(url, toy::net::FetchOptions{}, &response, &error),
        "a redirect with a root-relative Location is followed");
  Check(response.status == 200, "the root-relative redirect target status is 200");
  Check(response.body == "ok", "the root-relative redirect target body is returned");
  Check(response.final_url.path == "/final", "final_url uses the root-relative target path");
}

void TestNotFound() {
  LoopbackServer server;
  Check(server.ok(), "the loopback server is listening");
  if (!server.ok()) {
    return;
  }
  server.Start([](const std::string&) {
    return std::string("HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found");
  });

  toy::net::Url url;
  if (!MakeHttpUrl(server.port(), "/missing", &url)) {
    return;
  }
  toy::net::Response response;
  std::string error;
  Check(toy::net::Fetch(url, toy::net::FetchOptions{}, &response, &error),
        "a 404 response is still a completed fetch");
  Check(response.status == 404, "the 404 status is reported");
  Check(response.body == "not found", "the 404 body is returned");
}

void TestRedirectLimit() {
  LoopbackServer server;
  Check(server.ok(), "the loopback server is listening");
  if (!server.ok()) {
    return;
  }
  const std::uint16_t port = server.port();
  server.Start([port](const std::string&) -> std::string {
    return "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + std::to_string(port) +
           "/loop\r\nContent-Length: 0\r\n\r\n";
  });

  toy::net::Url url;
  if (!MakeHttpUrl(port, "/loop", &url)) {
    return;
  }
  toy::net::FetchOptions options;
  options.max_redirects = 2;
  toy::net::Response response;
  std::string error;
  Check(!toy::net::Fetch(url, options, &response, &error),
        "a redirect loop fails once max_redirects is exceeded");
  Check(!error.empty(), "the redirect loop failure sets an error");
  Check(error.find("redirect") != std::string::npos, "the redirect loop error mentions redirects");
}

void TestRedirectToUnsupportedScheme() {
  LoopbackServer server;
  Check(server.ok(), "the loopback server is listening");
  if (!server.ok()) {
    return;
  }
  server.Start([](const std::string&) {
    return std::string(
        "HTTP/1.1 302 Found\r\nLocation: https://example.com/\r\nContent-Length: 0\r\n\r\n");
  });

  toy::net::Url url;
  if (!MakeHttpUrl(server.port(), "/secure", &url)) {
    return;
  }
  toy::net::Response response;
  std::string error;
  Check(!toy::net::Fetch(url, toy::net::FetchOptions{}, &response, &error),
        "a redirect to https is an error (no TLS yet)");
  Check(error.find("https") != std::string::npos, "the redirect error names the https scheme");
}

void TestConnectionRefused() {
  // Reserve an ephemeral port and close it again, so nothing is listening there.
  const int reserved = ::socket(AF_INET, SOCK_STREAM, 0);
  Check(reserved >= 0, "a temporary socket is created");
  if (reserved < 0) {
    return;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  Check(::bind(reserved, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
        "the temporary socket binds to loopback");
  socklen_t address_size = sizeof(address);
  Check(::getsockname(reserved, reinterpret_cast<sockaddr*>(&address), &address_size) == 0,
        "the temporary port is read back");
  const std::uint16_t port = ntohs(address.sin_port);
  ::close(reserved);

  toy::net::Url url;
  if (!MakeHttpUrl(port, "/", &url)) {
    return;
  }
  toy::net::FetchOptions options;
  options.connect_timeout_ms = 1000;
  toy::net::Response response;
  std::string error;
  Check(!toy::net::Fetch(url, options, &response, &error), "a refused connection is an error");
  Check(!error.empty(), "the refused connection sets an error");
}

void TestReadTimeout() {
  LoopbackServer server;
  Check(server.ok(), "the loopback server is listening");
  if (!server.ok()) {
    return;
  }
  // Empty response: the server accepts the request and then stays silent.
  server.Start([](const std::string&) { return std::string(); });

  toy::net::Url url;
  if (!MakeHttpUrl(server.port(), "/silent", &url)) {
    return;
  }
  toy::net::FetchOptions options;
  options.connect_timeout_ms = 1000;
  options.read_timeout_ms = 200;
  toy::net::Response response;
  std::string error;
  Check(!toy::net::Fetch(url, options, &response, &error), "a silent server hits the read timeout");
  Check(error.find("timed out") != std::string::npos, "the read timeout error mentions timing out");
}

}  // namespace

int main() {
  TestContentLength();
  TestChunkedBody();
  TestAbsoluteRedirect();
  TestRootRelativeRedirect();
  TestNotFound();
  TestRedirectLimit();
  TestRedirectToUnsupportedScheme();
  TestConnectionRefused();
  TestReadTimeout();
  if (g_failures.load() != 0) {
    std::fprintf(stderr, "%d http check(s) failed\n", g_failures.load());
    return 1;
  }
  std::printf("http tests passed\n");
  return 0;
}
