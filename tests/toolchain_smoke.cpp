// Verifies the baseline every other test depends on: C++20 mode and a working
// compile / link / run cycle through CMake and CTest.

#include <array>
#include <cstdio>
#include <string_view>

int main() {
#if __cplusplus < 202002L
#error "toybrowser must be compiled as C++20 or later"
#endif

  constexpr std::array<int, 3> kNumbers{1, 2, 3};
  constexpr std::string_view kProgramName = "toybrowser";
  if (kNumbers.size() != 3 || kProgramName.empty()) {
    std::fprintf(stderr, "toolchain smoke test: basic standard library checks failed\n");
    return 1;
  }

  std::printf("toolchain smoke test passed: %s, C++%ld\n", kProgramName.data(),
              static_cast<long>(__cplusplus / 100));
  return 0;
}
