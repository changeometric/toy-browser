# One executable per test subject. Each is registered with CTest.
# Tests must be deterministic and must not need a network, a display or
# installed fonts.

add_executable(toolchain_smoke toolchain_smoke.cpp)
toybrowser_set_warnings(toolchain_smoke)
add_test(NAME toolchain.smoke COMMAND toolchain_smoke)
