// Tests for toy::net::Fetch with file:// URLs.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include "toy/net/http.h"
#include "toy/net/url.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "file test failed: %s\n", description);
    ++g_failures;
  }
}

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

std::string FileUrlFor(const std::filesystem::path& path) {
  return "file://" + PercentEncodePath(path.string());
}

bool ParseUrlOrFail(const std::string& text, toy::net::Url* url) {
  std::string error;
  const std::optional<toy::net::Url> parsed = toy::net::ParseUrl(text, &error);
  if (!parsed) {
    std::fprintf(stderr, "file test failed: cannot parse '%s': %s\n", text.c_str(), error.c_str());
    ++g_failures;
    return false;
  }
  *url = *parsed;
  return true;
}

bool WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file << content;
  return static_cast<bool>(file);
}

void RemoveFile(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

void TestPlainFile() {
  const std::filesystem::path path = std::filesystem::current_path() / "net_file_test_plain.txt";
  const std::string content = "file scheme test\n";
  Check(WriteFile(path, content), "the plain test file is written");
  toy::net::Url url;
  if (!ParseUrlOrFail(FileUrlFor(path), &url)) {
    RemoveFile(path);
    return;
  }
  Check(url.scheme == "file", "the test URL uses the file scheme");
  Check(url.path == path.string(), "the file URL path is the decoded absolute path");
  toy::net::Response response;
  std::string error;
  Check(toy::net::Fetch(url, toy::net::FetchOptions{}, &response, &error),
        "Fetch reads a plain file");
  Check(error.empty(), "a successful file fetch clears the error");
  Check(response.status == 200, "a file fetch reports status 200");
  Check(response.body == content, "a file fetch reads the whole file");
  Check(response.final_url.path == path.string(), "a file fetch reports the requested URL");
  RemoveFile(path);
  Check(!std::filesystem::exists(path), "the plain test file is removed");
}

void TestPercentEncodedSpace() {
  const std::filesystem::path path = std::filesystem::current_path() / "net file test.txt";
  const std::string content = "spaces are fine\n";
  Check(WriteFile(path, content), "the file with a space in its name is written");
  const std::string text = FileUrlFor(path);
  Check(text.find("%20") != std::string::npos, "the test URL percent-encodes the space");
  Check(text.find(' ') == std::string::npos, "the test URL has no raw spaces");
  toy::net::Url url;
  if (!ParseUrlOrFail(text, &url)) {
    RemoveFile(path);
    return;
  }
  Check(url.path == path.string(), "the percent-encoded space is decoded into the path");
  Check(url.path.find(' ') != std::string::npos, "the decoded path contains the space");
  toy::net::Response response;
  std::string error;
  Check(toy::net::Fetch(url, toy::net::FetchOptions{}, &response, &error),
        "Fetch reads a file whose path is percent-encoded");
  Check(response.body == content, "the percent-encoded file body is returned");
  RemoveFile(path);
  Check(!std::filesystem::exists(path), "the file with a space in its name is removed");
}

void TestMissingFile() {
  const std::filesystem::path path = std::filesystem::current_path() / "net_file_test_missing.txt";
  RemoveFile(path);
  toy::net::Url url;
  if (!ParseUrlOrFail(FileUrlFor(path), &url)) {
    return;
  }
  toy::net::Response response;
  std::string error;
  Check(!toy::net::Fetch(url, toy::net::FetchOptions{}, &response, &error),
        "a missing file is an error");
  Check(error.find("not found") != std::string::npos, "the missing file error says not found");
}

void TestDirectory() {
  toy::net::Url url;
  if (!ParseUrlOrFail(FileUrlFor(std::filesystem::current_path()), &url)) {
    return;
  }
  toy::net::Response response;
  std::string error;
  Check(!toy::net::Fetch(url, toy::net::FetchOptions{}, &response, &error),
        "a directory path is an error");
  Check(error.find("directory") != std::string::npos, "the directory error mentions a directory");
}

}  // namespace

int main() {
  TestPlainFile();
  TestPercentEncodedSpace();
  TestMissingFile();
  TestDirectory();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d file check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("file tests passed\n");
  return 0;
}
