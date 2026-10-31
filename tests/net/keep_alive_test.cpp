// Tests for toy::net::Client over HTTP/1.1: idle-connection reuse, persistence
// decisions, chunked framing, stale-connection retry and idle closing.
//
// Everything runs against the shared in-process loopback HTTP server
// (tests/support/http_server.h); no public network and no wall-time assertions. The
// only timing-sensitive check is the destructor test, which relies on the server
// serving one connection at a time: a leaked idle connection delays the next accept
// past the client's short read timeout, so a missing close fails deterministically.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/check.h"
#include "support/http_server.h"
#include "toy/net/http.h"
#include "toy/net/url.h"

namespace {

using toy::net::Client;
using toy::net::FetchOptions;
using toy::net::ParseUrl;
using toy::net::Response;
using toy::net::Url;
using toy_test::Check;

// The request target: the second token of the request line.
std::string RequestPath(std::string_view request) {
  const std::size_t method_end = request.find(' ');
  if (method_end == std::string_view::npos) {
    return {};
  }
  const std::size_t path_begin = method_end + 1;
  const std::size_t path_end = request.find(' ', path_begin);
  if (path_end == std::string_view::npos) {
    return {};
  }
  return std::string(request.substr(path_begin, path_end - path_begin));
}

// Percent-encodes a filesystem path for use in a file:// URL (a build directory may
// contain spaces or other characters that are not valid verbatim in a URL).
std::string PercentEncodePath(const std::string& text) {
  constexpr char kHexDigits[] = "0123456789ABCDEF";
  std::string encoded;
  for (const char character : text) {
    const bool unreserved = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z') ||
                            (character >= '0' && character <= '9') || character == '-' ||
                            character == '.' || character == '_' || character == '~';
    if (unreserved || character == '/') {
      encoded.push_back(character);
      continue;
    }
    const auto byte = static_cast<unsigned char>(character);
    encoded.push_back('%');
    encoded.push_back(kHexDigits[byte >> 4]);
    encoded.push_back(kHexDigits[byte & 0x0F]);
  }
  return encoded;
}

bool MakeUrl(const std::string& text, Url* url) {
  std::string error;
  const std::optional<Url> parsed = ParseUrl(text, &error);
  if (!parsed) {
    std::fprintf(stderr, "keep-alive test: cannot build URL '%s': %s\n", text.c_str(),
                 error.c_str());
    toy_test::Check(false, "the test URL parses");
    return false;
  }
  *url = *parsed;
  return true;
}

bool StartServer(toy_test::HttpServer* server, toy_test::HttpServer::Handler handler) {
  std::string error;
  if (!server->Start(std::move(handler), &error)) {
    std::fprintf(stderr, "keep-alive test: cannot start the HTTP server: %s\n", error.c_str());
    toy_test::Check(false, "the loopback HTTP server starts");
    return false;
  }
  return true;
}

bool FetchText(Client& client, const std::string& url_text, const FetchOptions& options,
               Response* response, std::string* error) {
  Url url;
  if (!MakeUrl(url_text, &url)) {
    return false;
  }
  return client.Fetch(url, options, response, error);
}

bool AllRequestsContain(const toy_test::HttpServer& server, std::string_view needle) {
  const std::vector<std::string> requests = server.requests();
  if (requests.empty()) {
    return false;
  }
  for (const std::string& request : requests) {
    if (request.find(needle) == std::string::npos) {
      return false;
    }
  }
  return true;
}

std::vector<std::string> RequestPaths(const toy_test::HttpServer& server) {
  std::vector<std::string> paths;
  for (const std::string& request : server.requests()) {
    paths.push_back(RequestPath(request));
  }
  return paths;
}

// A raw persistent response: Content-Length, no Connection header (HTTP/1.1 default
// is keep-alive), body returned verbatim.
std::string LengthResponse(std::string_view body) {
  return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" +
         std::string(body);
}

// Same-origin reuse: three requests, Content-Length and chunked bodies, one connection.
void TestSameOriginReuse() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view request) -> std::string {
        const std::string path = RequestPath(request);
        if (path == "/length") {
          return "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nalpha";
        }
        if (path == "/chunked") {
          return "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                 "5\r\nalpha\r\n"
                 "5\r\nbravo\r\n"
                 "0\r\n\r\n";
        }
        return LengthResponse("chair");
      })) {
    return;
  }
  Client client;
  const FetchOptions options;
  Response response;
  std::string error;

  Check(FetchText(client, server.Url("/length"), options, &response, &error),
        "the first keep-alive fetch succeeds");
  Check(response.body == "alpha", "the Content-Length body is read");
  Check(error.empty(), "a successful Client fetch clears the error");

  Check(FetchText(client, server.Url("/chunked"), options, &response, &error),
        "a chunked keep-alive fetch succeeds");
  Check(response.body == "alphabravo", "the chunked body is decoded");
  Check(response.status == 200, "the chunked response status is 200");

  Check(FetchText(client, server.Url("/last"), options, &response, &error),
        "the third keep-alive fetch succeeds");
  Check(response.body == "chair", "the third response body is read");
  Check(response.final_url.path == "/last", "final_url is the requested URL");

  Check(server.connection_count() == 1, "three same-origin requests reuse one connection");
  Check(server.request_count() == 3, "the server served all three requests");
  const std::vector<std::string> paths = RequestPaths(server);
  Check(paths.size() == 3 && paths[0] == "/length" && paths[1] == "/chunked" &&
            paths[2] == "/last",
        "the requests arrived in order");
  Check(AllRequestsContain(server, "\r\nConnection: keep-alive\r\n"),
        "Client requests announce keep-alive");
  Check(AllRequestsContain(server, "\r\nAccept-Encoding: gzip\r\n"),
        "Client requests advertise gzip");
}

// A response with `Connection: close` must not be pooled: the next request opens a
// new connection.
void TestConnectionCloseResponse() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view request) -> std::string {
        if (RequestPath(request) == "/close") {
          return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 5\r\n\r\nclose";
        }
        return LengthResponse("open");
      })) {
    return;
  }
  Client client;
  const FetchOptions options;
  Response response;
  std::string error;
  Check(FetchText(client, server.Url("/close"), options, &response, &error),
        "the Connection: close response is still fetched");
  Check(response.body == "close", "the closing response body is read");
  Check(FetchText(client, server.Url("/open"), options, &response, &error),
        "the request after a closed connection succeeds");
  Check(response.body == "open", "the second response body is read");
  Check(server.connection_count() == 2,
        "a Connection: close response forces a new connection next time");
  Check(server.request_count() == 2, "the server served two requests");
}

// Two origins each keep their own idle connection.
void TestTwoOrigins() {
  toy_test::HttpServer first;
  toy_test::HttpServer second;
  if (!StartServer(&first, [](std::string_view request) {
        return LengthResponse("first:" + RequestPath(request));
      })) {
    return;
  }
  if (!StartServer(&second, [](std::string_view request) {
        return LengthResponse("second:" + RequestPath(request));
      })) {
    return;
  }
  Client client;
  const FetchOptions options;
  Response response;
  std::string error;

  Check(FetchText(client, first.Url("/a"), options, &response, &error), "the first origin responds");
  Check(response.body == "first:/a", "the first origin body is correct");
  Check(FetchText(client, second.Url("/b"), options, &response, &error),
        "the second origin responds");
  Check(response.body == "second:/b", "the second origin body is correct");
  Check(FetchText(client, first.Url("/c"), options, &response, &error),
        "the first origin is reused");
  Check(FetchText(client, second.Url("/d"), options, &response, &error),
        "the second origin is reused");
  Check(first.connection_count() == 1, "the first origin keeps one connection");
  Check(second.connection_count() == 1, "the second origin keeps one connection");
  Check(first.request_count() == 2 && second.request_count() == 2,
        "each origin served two requests");
}

// Simulates a server that drops an idle keep-alive connection without announcing it
// (SetForceCloseAfterResponse): the next request reusing that connection fails
// before any response byte arrives and must be retried once on a fresh connection.
void TestStaleIdleConnectionRetried() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view request) {
        return LengthResponse("body:" + RequestPath(request));
      })) {
    return;
  }
  Client client;
  const FetchOptions options;
  Response response;
  std::string error;

  // From now on the server closes every connection right after its response without
  // announcing it, so the client pools a stale connection each time. The flag is set
  // before the request starts, which keeps the server's post-response check race-free.
  server.SetForceCloseAfterResponse(true);
  Check(FetchText(client, server.Url("/one"), options, &response, &error),
        "the first fetch succeeds");
  Check(response.body == "body:/one", "the first body is correct");

  // The pooled connection is stale: the request fails before any response byte
  // arrives and must be retried once on a fresh connection.
  Check(FetchText(client, server.Url("/two"), options, &response, &error),
        "a stale idle connection is retried once on a fresh connection");
  Check(error.empty(), "the retried fetch clears the error");
  Check(response.body == "body:/two", "the retried fetch returns the response body");
  Check(server.connection_count() == 2, "the retry opened exactly one fresh connection");
  Check(server.request_count() == 2, "the stale attempt never reached the server");
}

// A fresh connection that fails is reported, never retried.
void TestFreshConnectionFailureReported() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view) { return std::string(); })) {
    return;
  }
  Client client;
  const FetchOptions options;
  Response response;
  response.status = 7;
  response.body = "sentinel";
  std::string error;
  Check(!FetchText(client, server.Url("/gone"), options, &response, &error),
        "a server that closes without responding fails the fetch");
  Check(!error.empty(), "the fresh-connection failure is reported");
  Check(error.find("closed") != std::string::npos,
        "the fresh-connection failure is a readable connection-closed error");
  Check(response.status == 7 && response.body == "sentinel",
        "the response is untouched on failure");
  Check(server.request_count() == 1, "a fresh-connection failure is not retried");
}

// CloseIdleConnections drops the pooled connection, so the next request connects anew.
void TestCloseIdleConnections() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view request) {
        return LengthResponse("body:" + RequestPath(request));
      })) {
    return;
  }
  Client client;
  const FetchOptions options;
  Response response;
  std::string error;
  Check(FetchText(client, server.Url("/first"), options, &response, &error),
        "the first fetch succeeds");
  Check(server.connection_count() == 1, "the first fetch opened one connection");
  client.CloseIdleConnections();
  Check(FetchText(client, server.Url("/second"), options, &response, &error),
        "the fetch after CloseIdleConnections succeeds");
  Check(server.connection_count() == 2,
        "CloseIdleConnections closes the idle connection instead of reusing it");
  Check(server.request_count() == 2, "the server served both requests");
}

// The destructor closes idle connections too. The shared server serves one connection
// at a time, so a connection leaked by the destructor keeps the server away from the
// next client until its own socket timeout; the short read timeout below makes that a
// deterministic failure without asserting wall time in the passing case.
void TestDestructorClosesIdleConnections() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view request) {
        return LengthResponse("body:" + RequestPath(request));
      })) {
    return;
  }
  {
    Client client;
    const FetchOptions options;
    Response response;
    std::string error;
    Check(FetchText(client, server.Url("/first"), options, &response, &error),
          "the inner client fetch succeeds");
    Check(server.connection_count() == 1, "the inner client opened one connection");
  }
  Client second;
  FetchOptions options;
  options.read_timeout_ms = 1000;
  Response response;
  std::string error;
  Check(FetchText(second, server.Url("/second"), options, &response, &error),
        "the destructor closed the idle connection so the next fetch is served");
  Check(error.empty(), "the fetch after the destructor clears the error");
  Check(response.body == "body:/second", "the fetch after the destructor reads the body");
  Check(server.connection_count() == 2, "the fetch after the destructor opened a new connection");
  Check(server.request_count() == 2, "the server served both requests");
}

// file:// through Client behaves like FetchFile: status 200, body, requested URL.
void TestClientFileUrl() {
  const std::filesystem::path path =
      std::filesystem::current_path() / "net_keep_alive_test_file.txt";
  const std::string content = "client file body\n";
  {
    std::ofstream file(path, std::ios::binary);
    Check(static_cast<bool>(file), "the client file fixture is created");
    if (!file) {
      return;
    }
    file << content;
  }
  Client client;
  Url url;
  const bool parsed = MakeUrl("file://" + PercentEncodePath(path.string()), &url);
  Response response;
  std::string error;
  Check(parsed && client.Fetch(url, FetchOptions{}, &response, &error),
        "Client fetches a file URL");
  Check(response.status == 200, "a Client file fetch reports status 200");
  Check(response.body == content, "a Client file fetch returns the file body");
  Check(response.final_url.path == path.string(), "final_url is the requested file URL");
  Check(error.empty(), "a successful Client file fetch clears the error");
  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);
}

// A same-origin redirect reuses the pooled connection for the next hop.
void TestRedirectReusesConnection() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view request) -> std::string {
        if (RequestPath(request) == "/start") {
          return "HTTP/1.1 302 Found\r\nLocation: /final\r\nContent-Length: 0\r\n\r\n";
        }
        return LengthResponse("redirected");
      })) {
    return;
  }
  Client client;
  Url url;
  if (!MakeUrl(server.Url("/start"), &url)) {
    return;
  }
  Response response;
  std::string error;
  Check(client.Fetch(url, FetchOptions{}, &response, &error),
        "a same-origin redirect is followed through the pool");
  Check(response.status == 200, "the redirect target status is 200");
  Check(response.body == "redirected", "the redirect target body is read");
  Check(response.final_url.path == "/final", "final_url is updated after the redirect");
  Check(server.connection_count() == 1, "the redirect hop reuses the same connection");
  Check(server.request_count() == 2, "the redirect and its target each sent one request");
}

// HTTP/1.0 keeps a connection only with an explicit `Connection: keep-alive`.
void TestHttp10Persistence() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view request) -> std::string {
        if (RequestPath(request) == "/drop") {
          return "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nok";
        }
        return "HTTP/1.0 200 OK\r\nConnection: keep-alive\r\nContent-Length: 2\r\n\r\nok";
      })) {
    return;
  }
  Client client;
  const FetchOptions options;
  Response response;
  std::string error;
  Check(FetchText(client, server.Url("/keep"), options, &response, &error),
        "an HTTP/1.0 keep-alive response is fetched");
  Check(FetchText(client, server.Url("/keep"), options, &response, &error),
        "the HTTP/1.0 keep-alive connection is reused");
  Check(server.connection_count() == 1, "HTTP/1.0 keep-alive keeps one connection");
  Check(FetchText(client, server.Url("/drop"), options, &response, &error),
        "an HTTP/1.0 response without keep-alive is fetched");
  Check(server.connection_count() == 1, "the closing HTTP/1.0 response used the pool");
  Check(FetchText(client, server.Url("/next"), options, &response, &error),
        "the request after a closed HTTP/1.0 connection succeeds");
  Check(server.connection_count() == 2,
        "HTTP/1.0 without keep-alive closes the connection after the exchange");
  Check(server.request_count() == 4, "the HTTP/1.0 server served four requests");
}

// A persistent response without a length delimiter is an error; the connection is
// closed and a later request still works on a fresh connection.
void TestPersistentResponseWithoutDelimiter() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view request) -> std::string {
        if (RequestPath(request) == "/undelimited") {
          return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nopen ended";
        }
        return LengthResponse("recovered");
      })) {
    return;
  }
  Client client;
  const FetchOptions options;
  Response response;
  response.status = 7;
  std::string error;
  Check(!FetchText(client, server.Url("/undelimited"), options, &response, &error),
        "a persistent response without a delimiter fails the fetch");
  Check(error.find("delimiter") != std::string::npos,
        "the missing-delimiter error names the delimiter");
  Check(response.status == 7, "the response is untouched when the delimiter is missing");
  Check(FetchText(client, server.Url("/after"), options, &response, &error),
        "a fetch after the missing-delimiter failure succeeds");
  Check(response.body == "recovered", "the recovery response body is read");
  Check(server.connection_count() == 2,
        "the undelimited connection was closed and the next request reconnected");
}

// The raw Content-Length body is bounded by max_body_bytes.
void TestBodyLimit() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view) {
        const std::string body(100, 'x');
        return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
               "\r\n\r\n" + body;
      })) {
    return;
  }
  Client client;
  Url url;
  if (!MakeUrl(server.Url("/large"), &url)) {
    return;
  }
  FetchOptions options;
  options.max_body_bytes = 10;
  Response response;
  std::string error;
  Check(!client.Fetch(url, options, &response, &error),
        "a raw body above max_body_bytes is rejected");
  Check(error.find("size limit") != std::string::npos,
        "the raw body limit error mentions the size limit");
}

}  // namespace

int main() {
  TestSameOriginReuse();
  TestConnectionCloseResponse();
  TestTwoOrigins();
  TestStaleIdleConnectionRetried();
  TestFreshConnectionFailureReported();
  TestCloseIdleConnections();
  TestDestructorClosesIdleConnections();
  TestClientFileUrl();
  TestRedirectReusesConnection();
  TestHttp10Persistence();
  TestPersistentResponseWithoutDelimiter();
  TestBodyLimit();
  return toy_test::Finish("keep-alive test");
}
