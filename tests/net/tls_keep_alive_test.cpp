// Tests for toy::net::Client over TLS: two https requests must reuse one connection
// (one TCP connect and one TLS handshake), asserted through the shared loopback TLS
// server's connection/request counters.
//
// The committed test-only certificates in tests/fixtures/tls/ are the only trust
// anchor; it is supplied by CTest through SSL_CERT_FILE. Nothing here touches the
// public network or the machine's trust store.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "support/check.h"
#include "support/tls_server.h"
#include "toy/net/http.h"
#include "toy/net/url.h"

namespace {

using toy::net::Client;
using toy::net::FetchOptions;
using toy::net::ParseUrl;
using toy::net::Response;
using toy::net::Url;
using toy_test::Check;

std::string TlsFixture(const char* name) {
  return std::string(TOYBROWSER_TLS_FIXTURES) + "/" + name;
}

bool MakeUrl(const std::string& text, Url* url) {
  std::string error;
  const std::optional<Url> parsed = ParseUrl(text, &error);
  if (!parsed) {
    std::fprintf(stderr, "tls keep-alive test: cannot build URL '%s': %s\n", text.c_str(),
                 error.c_str());
    toy_test::Check(false, "the test URL parses");
    return false;
  }
  *url = *parsed;
  return true;
}

bool StartServer(toy_test::TlsServer* server, toy_test::TlsServer::Handler handler) {
  std::string error;
  if (!server->Start(TlsFixture("localhost_cert.pem"), TlsFixture("localhost_key.pem"),
                     std::move(handler), &error)) {
    std::fprintf(stderr, "tls keep-alive test: cannot start the TLS server: %s\n", error.c_str());
    toy_test::Check(false, "the loopback TLS server starts");
    return false;
  }
  return true;
}

// Two https requests to the same very origin: the server reports one accepted
// connection and two served requests, so the second request reused the TLS session
// (there was no second handshake).
void TestTlsConnectionReuse() {
  toy_test::TlsServer server;
  if (!StartServer(&server, [](std::string_view target) {
        toy_test::HttpResponse response;
        response.status = 200;
        response.body = "tls:" + std::string(target);
        response.keep_alive = true;
        return response;
      })) {
    return;
  }
  Client client;
  Url first_url;
  if (!MakeUrl(server.Url("/one"), &first_url)) {
    return;
  }
  Url second_url;
  if (!MakeUrl(server.Url("/two"), &second_url)) {
    return;
  }
  Response response;
  std::string error;
  Check(client.Fetch(first_url, FetchOptions{}, &response, &error),
        "the first https keep-alive fetch succeeds");
  Check(error.empty(), "the first https fetch clears the error");
  Check(response.status == 200, "the first https response status is 200");
  Check(response.body == "tls:/one", "the first https response body is read");
  Check(response.final_url.scheme == "https", "final_url keeps the https scheme");

  Check(client.Fetch(second_url, FetchOptions{}, &response, &error),
        "the second https keep-alive fetch succeeds");
  Check(error.empty(), "the second https fetch clears the error");
  Check(response.body == "tls:/two", "the second https response body is read");
  Check(response.final_url.path == "/two", "final_url is the second request URL");

  Check(server.connection_count() == 1, "both https requests reuse one TLS connection");
  Check(server.request_count() == 2, "the TLS server served two requests");
}

}  // namespace

int main() {
  TestTlsConnectionReuse();
  return toy_test::Finish("tls keep-alive test");
}
