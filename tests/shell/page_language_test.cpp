// M10 page language hint: the shell-private page loader extracts the page
// language from the first `lang` attribute of the root <html> element in
// document order (ASCII-whitespace trimmed, empty when absent) and stores it on
// the loaded page. Browser then passes it to the font selection for measurement
// and painting.
//
// This test drives the private loader directly (page.h) because the hint is not
// observable through the public Browser API when the installed fonts do not
// distinguish languages; the fixture fonts used by the other shell tests do not
// carry language metadata either. It is deterministic: file:// only, no fonts,
// no network, no display. The fixture HTML files are generated into the build
// tree by CMake.

#include <cstdio>
#include <optional>
#include <string>

#include "page.h"
#include "support/check.h"
#include "toy/net/url.h"

namespace {

std::string FileUrl(const std::string& path) { return "file://" + path; }

// Loads `path` through the private loader without a font selection (the bitmap
// path) and returns its language hint, or marks the load as failed.
bool LoadLanguage(const std::string& path, std::string* language) {
  std::string error;
  const std::optional<toy::net::Url> url = toy::net::ParseUrl(FileUrl(path), &error);
  if (!url) {
    std::fprintf(stderr, "page language test: cannot parse '%s': %s\n", path.c_str(),
                 error.c_str());
    return false;
  }
  toy::shell::Page page;
  if (!toy::shell::LoadPage(*url, 320, nullptr, &page, &error)) {
    std::fprintf(stderr, "page language test: cannot load '%s': %s\n", path.c_str(), error.c_str());
    return false;
  }
  *language = page.language;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <fixture-directory>\n", argv[0]);
    return 1;
  }
  const std::string directory = argv[1];
  std::string language;

  toy_test::Check(LoadLanguage(directory + "/zh.html", &language), "the zh fixture page loads");
  toy_test::Check(language == "zh-CN", "the root <html lang> value is extracted");
  toy_test::Check(LoadLanguage(directory + "/trimmed.html", &language),
                  "the trimmed fixture page loads");
  toy_test::Check(language == "zh-CN", "the extracted language is ASCII-whitespace trimmed");
  toy_test::Check(LoadLanguage(directory + "/first_attribute.html", &language),
                  "the first-attribute fixture page loads");
  toy_test::Check(language == "en", "the first lang attribute wins over a later one");
  toy_test::Check(LoadLanguage(directory + "/absent.html", &language),
                  "the no-language fixture page loads");
  toy_test::Check(language.empty(), "a page without <html lang> has an empty hint");
  toy_test::Check(LoadLanguage(directory + "/body_language.html", &language),
                  "the body-language fixture page loads");
  toy_test::Check(language.empty(), "a lang attribute on <body> is not the page language hint");
  toy_test::Check(LoadLanguage(directory + "/empty_language.html", &language),
                  "the empty-attribute fixture page loads");
  toy_test::Check(language.empty(), "an empty lang attribute yields an empty hint");
  return toy_test::Finish("page language");
}
