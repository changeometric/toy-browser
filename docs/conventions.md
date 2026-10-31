# Conventions

Small, boring, consistent. Only rules that keep the codebase uniform — no style for its own sake.

## Language and formatting

- C++20. No compiler extensions; no third-party libraries (the standard library and approved OS
  interfaces only).
- Formatting is defined by the committed `.clang-format` (Google-based, 100 columns). Format files
  you touch; never reformat unrelated code.
- Files: `snake_case.h`, `snake_case.cpp`, `#pragma once` instead of include guards.
- Names: types `UpperCamel`, functions/methods `UpperCamel`, variables `snake_case`, data members
  `trailing_underscore_`, constants `kUpperCamel`.
- Namespaces: `toy`, then the module (`toy::dom`, `toy::render`, ...). No `using namespace` in
  headers.
- Public headers live in `src/<module>/include/toy/<module>/<file>.h` and are included as
  `"toy/<module>/<file>.h"`. Everything else in a module is private.
- Include order: the header for this translation unit, C headers, C++ headers, project headers.
- Small files and small functions. One clear responsibility per file.

## Documentation language

- The authoritative rule lives in [AGENTS.md](../AGENTS.md): `README.zh.md`, `docs/zh/**` and
  future i18n data may be non-English; everything else, including code, comments and commit
  messages, is English. Update English first and treat it as the source of truth.
- Team-internal communication (task board, member messages, reports to the Lead) may use any
  efficient language; English is recommended. User-facing conversation follows the user's own
  language preference, which is not stored in the repository.

## Errors

- Recoverable failures return `std::optional<T>` or `bool` plus a human-readable
  `std::string* error` (details in `docs/architecture.md`); no exceptions for control flow.
- No exceptions across module boundaries; do not use exceptions for ordinary control flow.
- Internal invariants use `assert` in debug builds; programmer errors should fail loudly.

## Memory

- `std::unique_ptr` and value types own memory; references and raw pointers never own.
- Trees use arenas addressed by integer ids (see `docs/architecture.md`). No `shared_ptr` unless
  two owners genuinely exist.

## Modules and CMake

- One static library target per module: `toy_<module>`; the `toybrowser` executable links
  `toy_shell`.
- Never depend on a higher layer. New source files are listed explicitly in CMake — no globbing.
- Every first-party target gets `toybrowser_set_warnings()`.
- A module owns its own `CMakeLists.txt`. The root and test `CMakeLists.txt` contain explicit
  module lists; the Lead adds a new module name once, and names whose directories do not exist yet
  are skipped. This keeps the shared files Lead-owned during parallel work.
- Shared build files (root `CMakeLists.txt`, `tests/CMakeLists.txt`) and `docs/` are owned by the
  Lead while parallel work is in flight; module owners do not edit them.

## Tests

- One file per subject: `tests/<module>/<topic>_test.cpp`; CTest name `<module>.<topic>`.
- Deterministic: no network, no display, no dependence on time, locale, home directory or
  installed fonts.
- Prefer golden-dump comparisons for parsers and layout. New tests use the header-only
  `tests/support/check.h` (CMake interface target `toy_test_support`) instead of copying a local
  `Check`/`g_failures` harness; older files migrate when they are touched. No test framework
  dependency.
- Network-dependent code is tested against in-process loopback servers only: a plain HTTP server in
  the `net` tests and the header-only TLS server `tests/support/tls_server.h` (committed test-only
  certificates under `tests/fixtures/tls/`, trust supplied through the CTest `ENVIRONMENT`
  property). No test may contact the public network or use installed certificate stores.
- Fixtures that are binary or otherwise non-obvious come with their generator committed next to
  them (`tests/fixtures/<kind>/generate*.py`): Python standard library only, English, paths relative
  to the script directory, deterministic output, and a README section documenting the regeneration
  command and the properties the tests rely on. Regenerating must reproduce the committed content
  (font fixtures byte-identically; PNG bytes may follow the local zlib) or the README and tests must
  be updated in the same change.
- Font-dependent code is tested with the committed self-authored fixtures in
  `tests/fixtures/fonts/` and generated fontconfig configs; tests set `FONTCONFIG_FILE` through the
  CTest `ENVIRONMENT` property: the fixture config for automatic selection/fallback tests, or an
  empty config (no font directories) to force the built-in bitmap path. No test may use installed
  fonts or the machine's font configuration. CJK layout/paint tests use
  `tests/fixtures/cjk_site/` and the `layout.cjk_line_break` tokenizer tests.
- A bug fix without a regression test is not done unless the user explicitly waives it.
- Caches, viewport culling and other optimizations must be output-neutral: add tests comparing warm
  vs cold/reference results (metrics, runs, pixels) rather than wall-time assertions. Performance is
  verified with the Lead's throwaway benchmarks in `.tmp/`, not in CTest; do not add flaky timing
  tests to the suite.

## Build

```sh
# Fast browser-only build: tests are opt-in, so this does not compile 66 test executables.
# Without CMAKE_BUILD_TYPE the build defaults to RelWithDebInfo (M11); pass
# -DCMAKE_BUILD_TYPE=Debug for unoptimised debugging.
cmake -S . -B build
cmake --build build

# Full verification: build and run the tests as well.
cmake -S . -B build -DTOYBROWSER_BUILD_TESTS=ON -DTOYBROWSER_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

- Out-of-source builds only. Add `-DTOYBROWSER_WERROR=ON` to treat warnings as errors.
- `CMakePresets.json` provides `default` (browser only) and `dev` (tests + warnings-as-errors); CI
  runs the same configure/build/test sequence with GCC and Clang. Presets are convenience only —
  the explicit `cmake` commands above are the source of truth and must keep working.
- The default build type is `RelWithDebInfo`; do not remove that default (an unoptimised default
  made pages several times slower) and do not remove `-ffp-contract=off` (it keeps floating-point
  layout geometry byte-identical across build types and GCC/Clang).
- Tests are opt-in (`TOYBROWSER_BUILD_TESTS`, default `OFF`) so a plain build only compiles the
  browser; always pass it for a verification run. `cmake --build build --target toybrowser` is the
  other fast way to build just the browser in a configured test build.
- Build directories are never committed.
- `CMAKE_EXPORT_COMPILE_COMMANDS` is on, so `compile_commands.json` appears in the configured
  build directory for clangd and IDEs; build directories stay untracked.
- `cmake --install` installs the executable and README/LICENSE into the standard `bin/` and
  `share/doc/<project>/` directories; keep the install rules working and covered by the CI smoke
  step. Screenshots under `docs/assets/` are documentation only (generated from the committed
  fixture pages), not test assertions.

## Logging and diagnostics

- A few small logging helpers, no logging framework. Library code does not print page output to
  stdout; the CLI `--dump-*` flags are responsible for output.
- Diagnostics go to stderr; test expectations compare dump output, not logs.

## Commits

- English, imperative messages; prefixes such as `feat:`, `fix:`, `docs:`, `build:`, `test:`,
  `refactor:`.
- Group related changes into one coherent commit instead of many tiny ones: similar or detailed
  edits belong together. Do not mix unrelated work in the same commit.
- Do not rewrite published history without the user's consent.
