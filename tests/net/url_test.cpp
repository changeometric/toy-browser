// Tests for URL parsing and serialization (http and file schemes).

#include "toy/net/url.h"

#include <cstdio>
#include <optional>
#include <string>

namespace {

using toy::net::Url;

int g_failures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "url test failed: %s\n", description);
    ++g_failures;
  }
}

bool ParseSucceeds(const std::string& text, Url* url) {
  std::string error;
  const std::optional<Url> parsed = toy::net::ParseUrl(text, &error);
  if (!parsed) {
    std::fprintf(stderr, "url test failed: cannot parse '%s': %s\n", text.c_str(), error.c_str());
    ++g_failures;
    return false;
  }
  Check(error.empty(), "a successful parse clears the error string");
  *url = *parsed;
  return true;
}

void ExpectUrl(const std::string& text, const Url& expected, const char* description) {
  Url parsed;
  if (!ParseSucceeds(text, &parsed)) {
    return;
  }
  const bool matches = parsed.scheme == expected.scheme && parsed.host == expected.host &&
                       parsed.port == expected.port && parsed.path == expected.path &&
                       parsed.query == expected.query && parsed.fragment == expected.fragment;
  if (!matches) {
    std::fprintf(stderr, "url test failed: %s (parsed from '%s')\n", description, text.c_str());
    ++g_failures;
  }
}

void ExpectParseError(const std::string& text, const char* description) {
  std::string error;
  const bool parsed = toy::net::ParseUrl(text, &error).has_value();
  Check(!parsed, description);
  Check(!error.empty(), "a failed parse sets an error string");
}

void ExpectSerialize(const Url& url, const std::string& expected, const char* description) {
  const std::string text = toy::net::SerializeUrl(url);
  if (text != expected) {
    std::fprintf(stderr, "url test failed: %s (expected '%s', got '%s')\n", description,
                 expected.c_str(), text.c_str());
    ++g_failures;
  }
}

void TestHttpParsing() {
  ExpectUrl("http://Example.COM:8080/a%20b?q=1#frag",
            Url{"http", "example.com", 8080, "/a b", "q=1", "frag"},
            "http URL with port, escaped path, query and fragment");
  ExpectUrl("http://example.com", Url{"http", "example.com", 80, "/", "", ""},
            "http URL defaults to port 80 and path /");
  ExpectUrl("http://example.com?x=1", Url{"http", "example.com", 80, "/", "x=1", ""},
            "http URL with a query but no path");
  ExpectUrl("http://example.com#top", Url{"http", "example.com", 80, "/", "", "top"},
            "http URL with a fragment but no path");
  ExpectUrl("HTTP://EXAMPLE.COM/Path", Url{"http", "example.com", 80, "/Path", "", ""},
            "scheme and host are lowercased while the path case is kept");
  ExpectUrl("http://example.com:1/", Url{"http", "example.com", 1, "/", "", ""},
            "smallest valid port is accepted");
  ExpectUrl("http://example.com:65535/", Url{"http", "example.com", 65535, "/", "", ""},
            "largest valid port is accepted");
  ExpectUrl("http://example.com/%41", Url{"http", "example.com", 80, "/A", "", ""},
            "percent escapes in the path are decoded");
  ExpectUrl("http://example.com/a+b?c+d", Url{"http", "example.com", 80, "/a+b", "c+d", ""},
            "'+' is kept as a literal plus, not decoded to a space");
}

void TestFileParsing() {
  ExpectUrl("file:///tmp/a%20b.txt", Url{"file", "", 0, "/tmp/a b.txt", "", ""},
            "file URL with an empty host and an escaped path");
  ExpectUrl("file://localhost/tmp/x", Url{"file", "", 0, "/tmp/x", "", ""},
            "file URL with localhost is normalized to an empty host");
  ExpectUrl("FILE:///tmp/x", Url{"file", "", 0, "/tmp/x", "", ""}, "file scheme is lowercased");
}

void TestSerialization() {
  Url url;
  if (ParseSucceeds("http://Example.COM:8080/a%20b?q=1#frag", &url)) {
    ExpectSerialize(url, "http://example.com:8080/a%20b?q=1#frag",
                    "http URL serializes host, port and escaped components");
  }
  if (ParseSucceeds("http://example.com", &url)) {
    ExpectSerialize(url, "http://example.com/", "default port is omitted on serialization");
  }
  if (ParseSucceeds("file:///tmp/a%20b.txt", &url)) {
    ExpectSerialize(url, "file:///tmp/a%20b.txt", "file URL round-trips percent escapes");
  }
  if (ParseSucceeds("http://example.com/100%25", &url)) {
    ExpectSerialize(url, "http://example.com/100%25", "a literal percent sign is re-encoded");
  }

  const char* const kRoundTripUrls[] = {
      "http://example.com/",
      "http://example.com:8080/a%20b?q=1#frag",
      "file:///tmp/a%20b.txt",
      "http://example.com/caf%C3%A9",
  };
  for (const char* text : kRoundTripUrls) {
    Url first;
    if (!ParseSucceeds(text, &first)) {
      continue;
    }
    Url second;
    if (!ParseSucceeds(toy::net::SerializeUrl(first), &second)) {
      continue;
    }
    const bool matches = first.scheme == second.scheme && first.host == second.host &&
                         first.port == second.port && first.path == second.path &&
                         first.query == second.query && first.fragment == second.fragment;
    Check(matches, "SerializeUrl round-trips every parsed URL");
  }
}

void TestParseErrors() {
  ExpectParseError("page.html", "a relative URL is rejected");
  ExpectParseError("/absolute/path", "a root-relative URL is rejected");
  ExpectParseError("https://example.com/", "the https scheme is rejected");
  ExpectParseError("ftp://example.com/", "the ftp scheme is rejected");
  ExpectParseError("http:/example.com", "a missing // after the scheme is rejected");
  ExpectParseError("http://", "an http URL without a host is rejected");
  ExpectParseError("http:///path", "an http URL with an empty authority is rejected");
  ExpectParseError("http://:80/", "an http URL with an empty host is rejected");
  ExpectParseError("http://example.com:/", "an http URL with an empty port is rejected");
  ExpectParseError("http://example.com:0/", "http port 0 is rejected");
  ExpectParseError("http://example.com:65536/", "an http port above 65535 is rejected");
  ExpectParseError("http://example.com:abc/", "a non-numeric http port is rejected");
  ExpectParseError("http://example.com/%zz", "an invalid percent escape is rejected");
  ExpectParseError("http://example.com/%2", "a truncated percent escape is rejected");
  ExpectParseError("file://remotehost/tmp/x", "a file URL with a remote host is rejected");
  ExpectParseError("file://", "a file URL without an absolute path is rejected");
  ExpectParseError("file:///tmp/x?q=1", "a file URL with a query is rejected");
  ExpectParseError("file:///tmp/x#frag", "a file URL with a fragment is rejected");
}

void TestNoBaseResolutionMessages() {
  std::string error;
  toy::net::ParseUrl("docs/page.html", &error);
  Check(error.find("no base resolution yet") != std::string::npos,
        "the relative URL error mentions no base resolution");
  error.clear();
  toy::net::ParseUrl("https://example.com/", &error);
  Check(error.find("no base resolution yet") != std::string::npos,
        "the unsupported scheme error mentions no base resolution");
}

}  // namespace

int main() {
  TestHttpParsing();
  TestFileParsing();
  TestSerialization();
  TestParseErrors();
  TestNoBaseResolutionMessages();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d url check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("url tests passed\n");
  return 0;
}
