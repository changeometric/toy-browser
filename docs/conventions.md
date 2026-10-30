# Conventions

Small, boring, consistent. Only rules that keep the codebase uniform — no style for its own sake.

## Language and formatting

- C++20, standard library only. No compiler extensions.
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

## Errors

- Recoverable failures return `toy::Result<T>`-style values with a module-specific error enum.
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
- A module owns its own `CMakeLists.txt`: the root file and `tests/CMakeLists.txt` include
  `src/<module>` / `tests/<module>` as soon as that directory has a `CMakeLists.txt`. This lets
  several modules be written in parallel without editing shared build files.
- Shared build files (root `CMakeLists.txt`, `tests/CMakeLists.txt`) and `docs/` are owned by the
  Lead while parallel work is in flight; module owners do not edit them.

## Tests

- One file per subject: `tests/<module>/<topic>_test.cpp`; CTest name `<module>.<topic>`.
- Deterministic: no network, no display, no dependence on time, locale, home directory or
  installed fonts.
- Prefer golden-dump comparisons for parsers and layout; a tiny local assertion helper instead of a
  test framework dependency.
- A bug fix without a regression test is not done unless the user explicitly waives it.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

- Out-of-source builds only. Add `-DTOYBROWSER_WERROR=ON` to treat warnings as errors.
- Build directories are never committed.

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
