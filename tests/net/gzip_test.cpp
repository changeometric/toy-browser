// Tests for the M13 gzip support of toy::net::Client and for the unchanged one-shot
// Fetch: transparent decoding, identity, corrupt/unsupported codings, the decoded
// body cap and the fact that Fetch still negotiates no compression and does not
// decode.
//
// Fixtures are compressed in memory with zlib; every request goes to the shared
// in-process loopback HTTP server, never the public network.

#include <zlib.h>

#include <cstddef>
#include <cstdio>
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
using toy::net::Fetch;
using toy::net::FetchOptions;
using toy::net::ParseUrl;
using toy::net::Response;
using toy::net::Url;
using toy_test::Check;

bool MakeUrl(const std::string& text, Url* url) {
  std::string error;
  const std::optional<Url> parsed = ParseUrl(text, &error);
  if (!parsed) {
    std::fprintf(stderr, "gzip test: cannot build URL '%s': %s\n", text.c_str(), error.c_str());
    toy_test::Check(false, "the test URL parses");
    return false;
  }
  *url = *parsed;
  return true;
}

bool StartServer(toy_test::HttpServer* server, toy_test::HttpServer::Handler handler) {
  std::string error;
  if (!server->Start(std::move(handler), &error)) {
    std::fprintf(stderr, "gzip test: cannot start the HTTP server: %s\n", error.c_str());
    toy_test::Check(false, "the loopback HTTP server starts");
    return false;
  }
  return true;
}

// A raw persistent response with a gzip-encoded Content-Length body.
std::string GzipLengthResponse(std::string_view compressed) {
  return "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " +
         std::to_string(compressed.size()) + "\r\n\r\n" + std::string(compressed);
}

// The same body encoded with chunked transfer encoding, split into two chunks and
// followed by a trailer line to exercise the incremental framing.
std::string ChunkedBody(std::string_view data) {
  std::string body;
  const std::size_t split = data.size() / 2;
  const std::string_view chunks[] = {data.substr(0, split), data.substr(split)};
  for (const std::string_view chunk : chunks) {
    if (chunk.empty()) {
      continue;
    }
    char size_line[32];
    const int written = std::snprintf(size_line, sizeof(size_line), "%zx", chunk.size());
    body.append(size_line, static_cast<std::size_t>(written));
    body += "\r\n";
    body.append(chunk);
    body += "\r\n";
  }
  body += "0\r\nX-Trailer: ignored\r\n\r\n";
  return body;
}

std::string GzipChunkedResponse(std::string_view compressed) {
  return "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nTransfer-Encoding: chunked\r\n\r\n" +
         ChunkedBody(compressed);
}

// Compresses `input` as one gzip member using the same zlib the module uses. The
// fixtures stay deterministic: no timestamps are written (the default gzip header
// contains a zero mtime).
std::string GzipCompress(std::string_view input) {
  z_stream state{};
  if (deflateInit2(&state, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    toy_test::Check(false, "the gzip fixture compressor initializes");
    return {};
  }
  struct DeflateGuard {
    z_stream* state;
    ~DeflateGuard() { deflateEnd(state); }
  } guard{&state};

  state.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
  state.avail_in = static_cast<uInt>(input.size());
  std::string output;
  char buffer[4096];
  for (;;) {
    state.next_out = reinterpret_cast<Bytef*>(buffer);
    state.avail_out = static_cast<uInt>(sizeof(buffer));
    const int result = deflate(&state, Z_FINISH);
    if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR) {
      toy_test::Check(false, "the gzip fixture compresses");
      return {};
    }
    output.append(buffer, sizeof(buffer) - state.avail_out);
    if (result == Z_STREAM_END) {
      return output;
    }
    if (result == Z_BUF_ERROR) {
      toy_test::Check(false, "the gzip fixture compression makes progress");
      return {};
    }
  }
}

// A gzip body with Content-Length is decoded transparently and the header stays
// visible to the caller.
void TestGzipContentLength() {
  const std::string original = "gzip content-length body: hello hello hello hello";
  const std::string compressed = GzipCompress(original);
  Check(!compressed.empty(), "the gzip content-length fixture compresses");
  toy_test::HttpServer server;
  if (!StartServer(&server, [compressed](std::string_view) {
        return GzipLengthResponse(compressed);
      })) {
    return;
  }
  Client client;
  Url url;
  if (!MakeUrl(server.Url("/gzip"), &url)) {
    return;
  }
  Response response;
  std::string error;
  Check(client.Fetch(url, FetchOptions{}, &response, &error),
        "a gzip Content-Length response is fetched");
  Check(error.empty(), "a decoded gzip fetch clears the error");
  Check(response.status == 200, "the gzip response status is 200");
  Check(response.body == original, "the gzip Content-Length body is decoded");
  Check(response.HeaderValue("Content-Encoding") == "gzip",
        "the Content-Encoding header is preserved");
  Check(response.HeaderValue("content-length") == std::to_string(compressed.size()),
        "the raw Content-Length header is preserved");
}

// A gzip body with chunked transfer encoding (including a trailer) is decoded.
void TestGzipChunked() {
  const std::string original = "gzip chunked body: alpha beta gamma delta";
  const std::string compressed = GzipCompress(original);
  Check(!compressed.empty(), "the gzip chunked fixture compresses");
  toy_test::HttpServer server;
  if (!StartServer(&server, [compressed](std::string_view) {
        return GzipChunkedResponse(compressed);
      })) {
    return;
  }
  Client client;
  Url url;
  if (!MakeUrl(server.Url("/chunked-gzip"), &url)) {
    return;
  }
  Response response;
  std::string error;
  Check(client.Fetch(url, FetchOptions{}, &response, &error),
        "a gzip chunked response is fetched");
  Check(response.body == original, "the gzip chunked body is decoded");
  Check(response.HeaderValue("transfer-encoding") == "chunked",
        "the chunked transfer encoding header is preserved");
  Check(response.HeaderValue("Content-Encoding") == "gzip",
        "the chunked Content-Encoding header is preserved");
}

// Content-Encoding: identity passes the body through unchanged.
void TestIdentityCoding() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view) {
        const std::string body = "plain identity body";
        return "HTTP/1.1 200 OK\r\nContent-Encoding: identity\r\nContent-Length: " +
               std::to_string(body.size()) + "\r\n\r\n" + body;
      })) {
    return;
  }
  Client client;
  Url url;
  if (!MakeUrl(server.Url("/identity"), &url)) {
    return;
  }
  Response response;
  std::string error;
  Check(client.Fetch(url, FetchOptions{}, &response, &error),
        "an identity Content-Encoding response is fetched");
  Check(response.body == "plain identity body", "the identity body is unchanged");
  Check(response.HeaderValue("content-encoding") == "identity",
        "the identity Content-Encoding header is preserved");
}

// A body that claims gzip but is not a gzip stream is a readable error.
void TestCorruptGzip() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view) {
        const std::string body = "this is not gzip data";
        return "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " +
               std::to_string(body.size()) + "\r\n\r\n" + body;
      })) {
    return;
  }
  Client client;
  Url url;
  if (!MakeUrl(server.Url("/corrupt"), &url)) {
    return;
  }
  Response response;
  response.status = 7;
  std::string error;
  Check(!client.Fetch(url, FetchOptions{}, &response, &error),
        "a corrupt gzip body fails the fetch");
  Check(!error.empty(), "the corrupt gzip body sets an error");
  Check(error.find("gzip") != std::string::npos, "the corrupt gzip error mentions gzip");
  Check(response.status == 7, "the response is untouched on a gzip error");
}

// An unsupported Content-Encoding is a readable error.
void TestUnsupportedCoding() {
  toy_test::HttpServer server;
  if (!StartServer(&server, [](std::string_view) {
        const std::string body = "brotli bytes";
        return "HTTP/1.1 200 OK\r\nContent-Encoding: br\r\nContent-Length: " +
               std::to_string(body.size()) + "\r\n\r\n" + body;
      })) {
    return;
  }
  Client client;
  Url url;
  if (!MakeUrl(server.Url("/brotli"), &url)) {
    return;
  }
  Response response;
  std::string error;
  Check(!client.Fetch(url, FetchOptions{}, &response, &error),
        "an unsupported Content-Encoding fails the fetch");
  Check(error.find("br") != std::string::npos,
        "the unsupported-encoding error names the coding");
  Check(error.find("Content-Encoding") != std::string::npos,
        "the unsupported-encoding error names the header");
}

// The decoded body is bounded by max_body_bytes even when the encoded body is small.
void TestDecodedBodyLimit() {
  const std::string original(4096, 'A');
  const std::string compressed = GzipCompress(original);
  Check(!compressed.empty(), "the gzip limit fixture compresses");
  Check(compressed.size() <= 100,
        "the compressed limit fixture fits in the raw body limit");
  toy_test::HttpServer server;
  if (!StartServer(&server, [compressed](std::string_view) {
        return GzipLengthResponse(compressed);
      })) {
    return;
  }
  Client client;
  Url url;
  if (!MakeUrl(server.Url("/bomb"), &url)) {
    return;
  }
  FetchOptions options;
  options.max_body_bytes = 100;
  Response response;
  std::string error;
  Check(!client.Fetch(url, options, &response, &error),
        "a decoded body above max_body_bytes is rejected");
  Check(error.find("size limit") != std::string::npos,
        "the decoded body limit error mentions the size limit");
}

// The one-shot Fetch keeps its pre-M13 behavior: no Accept-Encoding, no decoding.
void TestOneShotFetchUnchanged() {
  const std::string original = "fetch keeps gzip bodies encoded";
  const std::string compressed = GzipCompress(original);
  Check(!compressed.empty(), "the one-shot gzip fixture compresses");
  toy_test::HttpServer server;
  if (!StartServer(&server, [compressed](std::string_view) {
        return "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " +
               std::to_string(compressed.size()) + "\r\nConnection: close\r\n\r\n" + compressed;
      })) {
    return;
  }
  Url url;
  if (!MakeUrl(server.Url("/raw"), &url)) {
    return;
  }
  Response response;
  std::string error;
  Check(Fetch(url, FetchOptions{}, &response, &error), "the one-shot Fetch succeeds");
  Check(response.body == compressed, "the one-shot Fetch does not decode gzip bodies");
  Check(response.HeaderValue("Content-Encoding") == "gzip",
        "the one-shot Fetch still exposes Content-Encoding");

  const std::vector<std::string> requests = server.requests();
  Check(requests.size() == 1, "the one-shot Fetch sends exactly one request");
  if (!requests.empty()) {
    Check(requests[0].find("Accept-Encoding") == std::string::npos,
          "the one-shot Fetch does not advertise Accept-Encoding");
    Check(requests[0].find("\r\nConnection: close\r\n") != std::string::npos,
          "the one-shot Fetch still asks to close the connection");
  }
}

}  // namespace

int main() {
  TestGzipContentLength();
  TestGzipChunked();
  TestIdentityCoding();
  TestCorruptGzip();
  TestUnsupportedCoding();
  TestDecodedBodyLimit();
  TestOneShotFetchUnchanged();
  return toy_test::Finish("gzip test");
}
