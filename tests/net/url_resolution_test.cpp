// Tests for toy::net::ResolveUrl: resolving a reference against an absolute
// http://, https:// or file:// base URL (RFC 3986 section 5.2, MVP subset).

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "support/check.h"
#include "toy/net/url.h"

namespace {

using toy::net::Url;

constexpr const char* kHttpBase = "http://example.com/dir/page.html?x=1#f";
constexpr const char* kHttpsBase = "https://example.com/dir/page.html?x=1#f";
constexpr const char* kFileBase = "file:///a/b/page.html";

void ExpectResolved(const char* base_text, std::string_view reference, std::string_view expected,
                    const char* description) {
  const std::optional<Url> base = toy::net::ParseUrl(base_text, nullptr);
  if (!base) {
    std::fprintf(stderr, "  cannot parse test base '%s'\n", base_text);
    toy_test::Check(false, "the test base URL parses");
    return;
  }
  std::string error = "stale error";
  const std::optional<Url> resolved = toy::net::ResolveUrl(*base, reference, &error);
  if (!resolved) {
    std::fprintf(stderr, "  base '%s' + reference '%.*s' failed: %s\n", base_text,
                 static_cast<int>(reference.size()), reference.data(), error.c_str());
    toy_test::Check(false, description);
    return;
  }
  toy_test::Check(error.empty(), "a successful resolve clears the error string");
  const std::string actual = toy::net::SerializeUrl(*resolved);
  if (actual != expected) {
    std::fprintf(stderr, "  base '%s' + reference '%.*s': expected '%.*s', got '%s'\n", base_text,
                 static_cast<int>(reference.size()), reference.data(),
                 static_cast<int>(expected.size()), expected.data(), actual.c_str());
  }
  toy_test::Check(actual == expected, description);
}

void ExpectResolveError(const char* base_text, std::string_view reference,
                        const char* description) {
  const std::optional<Url> base = toy::net::ParseUrl(base_text, nullptr);
  if (!base) {
    std::fprintf(stderr, "  cannot parse test base '%s'\n", base_text);
    toy_test::Check(false, "the test base URL parses");
    return;
  }
  std::string error;
  const std::optional<Url> resolved = toy::net::ResolveUrl(*base, reference, &error);
  if (resolved) {
    std::fprintf(stderr, "  base '%s' + reference '%.*s' unexpectedly resolved to '%s'\n",
                 base_text, static_cast<int>(reference.size()), reference.data(),
                 toy::net::SerializeUrl(*resolved).c_str());
  }
  toy_test::Check(!resolved.has_value(), description);
  toy_test::Check(!error.empty(), "a failed resolve sets an error string");
}

void TestRelativePaths() {
  ExpectResolved(kHttpBase, "b.html", "http://example.com/dir/b.html",
                 "a relative path replaces the last base path segment");
  ExpectResolved(kHttpBase, "sub/", "http://example.com/dir/sub/",
                 "a trailing slash in the reference is kept");
  ExpectResolved(kHttpBase, "./a/../b", "http://example.com/dir/b",
                 "leading '.' and inner '..' segments are removed");
  ExpectResolved(kHttpBase, "../up", "http://example.com/up", "'..' climbs one directory");
  ExpectResolved(kHttpBase, ".", "http://example.com/dir/", "'.' resolves to the base directory");
  ExpectResolved(kHttpBase, "..", "http://example.com/", "'..' resolves to the parent directory");
  ExpectResolved(kHttpBase, "../../../../x", "http://example.com/x",
                 "dot segments never escape the root");
  ExpectResolved(kHttpBase, "....", "http://example.com/dir/....",
                 "a run of dots longer than two is an ordinary name");

  ExpectResolved("http://example.com", "x", "http://example.com/x",
                 "an http base without a written path has path '/'");
  ExpectResolved("http://example.com", "", "http://example.com/", "an empty reference keeps '/'");
  ExpectResolved("http://example.com/dir/", "x", "http://example.com/dir/x",
                 "a trailing base slash makes the relative path a sibling");
  ExpectResolved("http://example.com/dir/", "../x", "http://example.com/x",
                 "'..' from a trailing-slash base goes up one level");
  ExpectResolved("http://example.com/dir/", ".", "http://example.com/dir/",
                 "'.' against a directory base keeps the trailing slash");
}

void TestHttpsBase() {
  ExpectResolved(kHttpsBase, "b.html", "https://example.com/dir/b.html",
                 "a relative reference keeps the https scheme");
  ExpectResolved(kHttpsBase, "", "https://example.com/dir/page.html?x=1",
                 "an empty reference keeps the https base URL without its fragment");
  ExpectResolved(kHttpsBase, "/x/y", "https://example.com/x/y",
                 "an absolute path keeps the https scheme");
  ExpectResolved(kHttpsBase, "//other.org/z", "https://other.org/z",
                 "a scheme-relative reference inherits the https scheme");
  ExpectResolved(kHttpsBase, "//other.org:8443/z", "https://other.org:8443/z",
                 "a scheme-relative https reference keeps a non-default port");
  ExpectResolved(kHttpsBase, "//example.com:443/z", "https://example.com/z",
                 "an explicit https default port is normalized away");
  ExpectResolved("https://example.com:8443/dir/page", "", "https://example.com:8443/dir/page",
                 "an empty reference keeps a non-default https port");
  ExpectResolved("https://example.com:8443/dir/page", "x", "https://example.com:8443/dir/x",
                 "a relative https path keeps the non-default port");
  ExpectResolved("https://example.com", "x", "https://example.com/x",
                 "an https base without a written path has path '/'");
  ExpectResolved(kHttpsBase, "?q=2", "https://example.com/dir/page.html?q=2",
                 "a query-only reference keeps the https scheme");
  ExpectResolved(kHttpsBase, "#top", "https://example.com/dir/page.html?x=1#top",
                 "a fragment-only reference keeps the https scheme and query");
}

void TestSchemeSwitches() {
  ExpectResolved(kHttpBase, "https://other.org/z", "https://other.org/z",
                 "an absolute https reference switches away from an http base");
  ExpectResolved(kHttpsBase, "http://other.org/z", "http://other.org/z",
                 "an absolute http reference switches away from an https base");
  ExpectResolved(kHttpBase, "HTTPS://other.org/z", "https://other.org/z",
                 "an absolute https reference lowercases its scheme");
  ExpectResolved(kHttpsBase, "https://example.com", "https://example.com/",
                 "an absolute https reference without a path gets '/'");
  ExpectResolved(kHttpsBase, "https://other.org/a/b/../", "https://other.org/a/",
                 "dot segments are removed from an absolute https reference path");
  ExpectResolved(kFileBase, "https://example.com/x", "https://example.com/x",
                 "an absolute https reference against a file base switches scheme");
}

void TestAbsolutePaths() {
  ExpectResolved(kHttpBase, "/x/y", "http://example.com/x/y",
                 "an absolute path replaces the whole base path");
  ExpectResolved(kHttpBase, "/../x", "http://example.com/x",
                 "a leading '..' below the root is dropped");
  ExpectResolved(kHttpBase, "/a/./b/../c", "http://example.com/a/c",
                 "dot segments in an absolute path are removed");
  ExpectResolved(kHttpBase, "/a/..", "http://example.com/", "'/a/..' resolves to the root");
  ExpectResolved(kHttpBase, "/a/.", "http://example.com/a/", "'/a/.' keeps a trailing slash");
  ExpectResolved(kHttpBase, "/", "http://example.com/", "the root path resolves to itself");
}

void TestSchemeRelative() {
  ExpectResolved(kHttpBase, "//other.org/z", "http://other.org/z",
                 "a scheme-relative reference keeps the base scheme");
  ExpectResolved(kHttpBase, "//other.org:81", "http://other.org:81/",
                 "a scheme-relative reference keeps its port");
  ExpectResolved(kHttpBase, "//Other.ORG/A/../b", "http://other.org/b",
                 "a scheme-relative host is lowercased and dot segments are removed");
  ExpectResolved(kHttpBase, "//example.com/x", "http://example.com/x",
                 "a scheme-relative reference may repeat the base host");
  ExpectResolved("http://example.com:8080/dir/", "//other.org/x", "http://other.org/x",
                 "the default port is dropped when the reference names no port");
}

void TestQueryAndFragment() {
  ExpectResolved(kHttpBase, "?q=2", "http://example.com/dir/page.html?q=2",
                 "a query-only reference keeps the path and replaces the query");
  ExpectResolved(kHttpBase, "#top", "http://example.com/dir/page.html?x=1#top",
                 "a fragment-only reference keeps the path and the query");
  ExpectResolved(kHttpBase, "", "http://example.com/dir/page.html?x=1",
                 "an empty reference drops the base fragment");
  ExpectResolved(kHttpBase, "?", "http://example.com/dir/page.html",
                 "an empty query clears the base query");
  ExpectResolved(kHttpBase, "#", "http://example.com/dir/page.html?x=1",
                 "an empty fragment clears the base fragment");
  ExpectResolved(kHttpBase, "b.html#f", "http://example.com/dir/b.html#f",
                 "a relative path with a fragment keeps the fragment");
  ExpectResolved(kHttpBase, "b.html?q#f", "http://example.com/dir/b.html?q#f",
                 "a relative path with a query and a fragment keeps both");
  ExpectResolved("http://example.com/dir/", "#top", "http://example.com/dir/#top",
                 "a fragment-only reference keeps an empty base query absent");
}

void TestAbsoluteReferences() {
  ExpectResolved(kHttpBase, "http://Other.ORG:8080/a/../b?q#f", "http://other.org:8080/b?q#f",
                 "an absolute http reference supplies scheme, authority, path and query");
  ExpectResolved(kHttpBase, "HTTP://other.org/z", "http://other.org/z",
                 "an absolute http reference lowercases its scheme");
  ExpectResolved(kHttpBase, "http://example.com", "http://example.com/",
                 "an absolute http reference without a path gets '/'");
  ExpectResolved(kHttpBase, "http://example.com/a/b/../", "http://example.com/a/",
                 "dot segments are removed from an absolute reference path");
}

void TestPorts() {
  ExpectResolved("http://example.com:8080/dir/page", "", "http://example.com:8080/dir/page",
                 "an empty reference keeps a non-default port");
  ExpectResolved("http://example.com:8080/dir/page", "x", "http://example.com:8080/dir/x",
                 "a relative path keeps a non-default port");
  ExpectResolved("http://example.com:8080/dir/page", "//other.org/x", "http://other.org/x",
                 "a scheme-relative reference drops the base port when it names none");
  ExpectResolved("http://example.com:8080/dir/page", "//example.com:8080/x",
                 "http://example.com:8080/x", "a scheme-relative reference may repeat the port");
  ExpectResolved(kHttpBase, "//other.org:80/x", "http://other.org/x",
                 "an explicit default port is normalized away");
}

void TestEncoding() {
  ExpectResolved(kHttpBase, "a%20b.html", "http://example.com/dir/a%20b.html",
                 "percent escapes in a relative path survive resolution");
  ExpectResolved(kHttpBase, "b.html?q=a%20b", "http://example.com/dir/b.html?q=a%20b",
                 "percent escapes in a query survive resolution");
  ExpectResolved(kHttpBase, "b.html#a%20b", "http://example.com/dir/b.html#a%20b",
                 "percent escapes in a fragment survive resolution");
  ExpectResolved(kHttpBase, "caf%C3%A9.html", "http://example.com/dir/caf%C3%A9.html",
                 "multi-byte percent escapes in a path survive resolution");
  ExpectResolved(kHttpBase, "a+b.html", "http://example.com/dir/a+b.html",
                 "'+' in a path stays a literal plus");
  ExpectResolved("http://example.com/a%20b/page?q=a%20b#f", "",
                 "http://example.com/a%20b/page?q=a%20b",
                 "an empty reference keeps escaped base components and drops the fragment");
}

// The reference examples from RFC 3986 section 5.4 with base
// "http://a/b/c/d;p?q". "//g" is written as "http://g/" because ParseUrl
// normalizes an empty http path to "/".
void TestRfc3986Examples() {
  constexpr const char* kBase = "http://a/b/c/d;p?q";
  ExpectResolved(kBase, "g", "http://a/b/c/g", "RFC 3986 5.4: 'g'");
  ExpectResolved(kBase, "./g", "http://a/b/c/g", "RFC 3986 5.4: './g'");
  ExpectResolved(kBase, "g/", "http://a/b/c/g/", "RFC 3986 5.4: 'g/'");
  ExpectResolved(kBase, "/g", "http://a/g", "RFC 3986 5.4: '/g'");
  ExpectResolved(kBase, "//g", "http://g/", "RFC 3986 5.4: '//g'");
  ExpectResolved(kBase, "?y", "http://a/b/c/d;p?y", "RFC 3986 5.4: '?y'");
  ExpectResolved(kBase, "g?y", "http://a/b/c/g?y", "RFC 3986 5.4: 'g?y'");
  ExpectResolved(kBase, "#s", "http://a/b/c/d;p?q#s", "RFC 3986 5.4: '#s'");
  ExpectResolved(kBase, "g#s", "http://a/b/c/g#s", "RFC 3986 5.4: 'g#s'");
  ExpectResolved(kBase, "g?y#s", "http://a/b/c/g?y#s", "RFC 3986 5.4: 'g?y#s'");
  ExpectResolved(kBase, ";x", "http://a/b/c/;x", "RFC 3986 5.4: ';x'");
  ExpectResolved(kBase, "g;x", "http://a/b/c/g;x", "RFC 3986 5.4: 'g;x'");
  ExpectResolved(kBase, "g;x?y#s", "http://a/b/c/g;x?y#s", "RFC 3986 5.4: 'g;x?y#s'");
  ExpectResolved(kBase, "", "http://a/b/c/d;p?q", "RFC 3986 5.4: empty reference");
  ExpectResolved(kBase, ".", "http://a/b/c/", "RFC 3986 5.4: '.'");
  ExpectResolved(kBase, "./", "http://a/b/c/", "RFC 3986 5.4: './'");
  ExpectResolved(kBase, "..", "http://a/b/", "RFC 3986 5.4: '..'");
  ExpectResolved(kBase, "../", "http://a/b/", "RFC 3986 5.4: '../'");
  ExpectResolved(kBase, "../g", "http://a/b/g", "RFC 3986 5.4: '../g'");
  ExpectResolved(kBase, "../..", "http://a/", "RFC 3986 5.4: '../..'");
  ExpectResolved(kBase, "../../", "http://a/", "RFC 3986 5.4: '../../'");
  ExpectResolved(kBase, "../../g", "http://a/g", "RFC 3986 5.4: '../../g'");
  ExpectResolved(kBase, "../../../g", "http://a/g", "RFC 3986 5.4: '../../../g'");
  ExpectResolved(kBase, "../../../../g", "http://a/g", "RFC 3986 5.4: '../../../../g'");
  ExpectResolved(kBase, "/./g", "http://a/g", "RFC 3986 5.4: '/./g'");
  ExpectResolved(kBase, "/../g", "http://a/g", "RFC 3986 5.4: '/../g'");
  ExpectResolved(kBase, "g.", "http://a/b/c/g.", "RFC 3986 5.4: 'g.'");
  ExpectResolved(kBase, ".g", "http://a/b/c/.g", "RFC 3986 5.4: '.g'");
  ExpectResolved(kBase, "g..", "http://a/b/c/g..", "RFC 3986 5.4: 'g..'");
  ExpectResolved(kBase, "..g", "http://a/b/c/..g", "RFC 3986 5.4: '..g'");
  ExpectResolved(kBase, "./../g", "http://a/b/g", "RFC 3986 5.4: './../g'");
  ExpectResolved(kBase, "./g/.", "http://a/b/c/g/", "RFC 3986 5.4: './g/.'");
  ExpectResolved(kBase, "g/./h", "http://a/b/c/g/h", "RFC 3986 5.4: 'g/./h'");
  ExpectResolved(kBase, "g/../h", "http://a/b/c/h", "RFC 3986 5.4: 'g/../h'");
  ExpectResolved(kBase, "g;x=1/./y", "http://a/b/c/g;x=1/y", "RFC 3986 5.4: 'g;x=1/./y'");
  ExpectResolved(kBase, "g;x=1/../y", "http://a/b/c/y", "RFC 3986 5.4: 'g;x=1/../y'");
  ExpectResolved(kBase, "g?y/./x", "http://a/b/c/g?y/./x", "RFC 3986 5.4: 'g?y/./x'");
  ExpectResolved(kBase, "g?y/../x", "http://a/b/c/g?y/../x", "RFC 3986 5.4: 'g?y/../x'");
  ExpectResolved(kBase, "g#s/./x", "http://a/b/c/g#s/./x", "RFC 3986 5.4: 'g#s/./x'");
  ExpectResolved(kBase, "g#s/../x", "http://a/b/c/g#s/../x", "RFC 3986 5.4: 'g#s/../x'");
}

void TestFileBase() {
  ExpectResolved(kFileBase, "c.html", "file:///a/b/c.html",
                 "a relative path resolves under the file base directory");
  ExpectResolved(kFileBase, "/x", "file:///x", "an absolute path resolves at the file root");
  ExpectResolved(kFileBase, "../x", "file:///a/x", "'..' climbs a file directory");
  ExpectResolved(kFileBase, "../../../../x", "file:///x",
                 "file dot segments never escape the root");
  ExpectResolved(kFileBase, ".", "file:///a/b/", "'.' keeps the file directory trailing slash");
  ExpectResolved(kFileBase, "..", "file:///a/",
                 "'..' from a file resolves to the parent directory");
  ExpectResolved(kFileBase, "", "file:///a/b/page.html", "an empty reference keeps the file path");
  ExpectResolved(kFileBase, "c.html#frag", "file:///a/b/c.html",
                 "a file result drops the fragment");
  ExpectResolved(kFileBase, "?q=1", "file:///a/b/page.html", "a file result drops the query");
  ExpectResolved(kFileBase, "c.html?q#f", "file:///a/b/c.html",
                 "a file result drops both a query and a fragment");
  ExpectResolved(kFileBase, "//localhost/x", "file:///x",
                 "a scheme-relative localhost reference resolves to an empty file host");
  ExpectResolved(kFileBase, "file:///x?q=1#f", "file:///x",
                 "an absolute file reference drops its query and fragment");
  ExpectResolved(kFileBase, "http://example.com/x", "http://example.com/x",
                 "an absolute http reference against a file base switches scheme");
  ExpectResolved("file:///", "a", "file:///a", "the file root works as a base");
  ExpectResolved("file:///a/b/", "c", "file:///a/b/c",
                 "a file base with a trailing slash resolves a sibling");
}

void TestErrors() {
  ExpectResolveError(kHttpBase, "mailto:a@b", "a mailto reference is rejected");
  ExpectResolveError(kHttpBase, "ftp://example.com/x", "the ftp scheme is rejected");
  ExpectResolveError(kHttpBase, "http:/example.com/x",
                     "an absolute http reference without '//' is rejected");
  ExpectResolveError(kHttpsBase, "https:/example.com/x",
                     "an absolute https reference without '//' is rejected");
  ExpectResolveError(kHttpBase, "file:/x", "an absolute file reference without '//' is rejected");
  ExpectResolveError(kHttpBase, "http:///path", "an http reference without a host is rejected");
  ExpectResolveError(kHttpBase, "https:///path", "an https reference without a host is rejected");
  ExpectResolveError(kHttpBase, "http://example.com:0/",
                     "an http reference with port 0 is rejected");
  ExpectResolveError(kHttpBase, "https://example.com:0/",
                     "an https reference with port 0 is rejected");
  ExpectResolveError(kHttpBase, "b.html%zz", "an invalid percent escape is rejected");
  ExpectResolveError(kHttpBase, "//", "a scheme-relative reference with an empty host is rejected");
  ExpectResolveError(kFileBase, "//remotehost/x",
                     "a file reference with a remote host is rejected");
}

void TestNullErrorPointer() {
  const std::optional<Url> base = toy::net::ParseUrl("http://example.com/dir/page.html", nullptr);
  toy_test::Check(base.has_value(), "the test base URL parses");
  if (!base) {
    return;
  }
  toy_test::Check(toy::net::ResolveUrl(*base, "b.html", nullptr).has_value(),
                  "resolving works with a null error pointer");
  toy_test::Check(toy::net::ResolveUrl(*base, "https://x/", nullptr).has_value(),
                  "an https reference resolves with a null error pointer");
  toy_test::Check(!toy::net::ResolveUrl(*base, "ftp://x/", nullptr).has_value(),
                  "resolution failures work with a null error pointer");
}

}  // namespace

int main() {
  TestRelativePaths();
  TestHttpsBase();
  TestSchemeSwitches();
  TestAbsolutePaths();
  TestSchemeRelative();
  TestQueryAndFragment();
  TestAbsoluteReferences();
  TestPorts();
  TestEncoding();
  TestRfc3986Examples();
  TestFileBase();
  TestErrors();
  TestNullErrorPointer();
  return toy_test::Finish("url resolution test");
}
