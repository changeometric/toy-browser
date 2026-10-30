#pragma once

// Minimal assertion helpers shared by the CTest executables.
//
// Each test executable is expected to be one translation unit (one subject file
// with its own main), which keeps the inline failure counter deterministic. A
// test links the header-only `toy_test_support` interface target and reports
// with toy_test::Finish; no test framework dependency is introduced.
//
// Usage:
//   #include "support/check.h"
//
//   int main() {
//     toy_test::Check(1 + 1 == 2, "arithmetic works");
//     return toy_test::Finish("subject test");
//   }

#include <cstdio>

namespace toy_test {

inline int failures = 0;

inline void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "check failed: %s\n", description);
    ++failures;
  }
}

inline int Finish(const char* name) {
  if (failures != 0) {
    std::fprintf(stderr, "%s: %d failure(s)\n", name, failures);
    return 1;
  }
  std::printf("%s passed\n", name);
  return 0;
}

}  // namespace toy_test
