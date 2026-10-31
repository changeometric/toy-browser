# Contributing

Thanks for your interest. This is a from-scratch learning browser: small, reviewable changes that
keep the existing tests green are the most welcome kind.

> **AI use:** this repository is built with heavy use of AI — see
> [AI-DISCLOSURE.md](AI-DISCLOSURE.md). Contributions are reviewed in that context.

## Before you start

- Read [docs/architecture.md](docs/architecture.md) for the module map and dependency direction.
- Read [docs/conventions.md](docs/conventions.md) for C++/CMake/testing conventions.
- The browser engine parts (HTML/CSS parsing, style, layout, painting) are implemented here and must
  stay that way. Ready-made browser components (HTML/CSS parsers, JS engines, layout or rendering
  engines, GUI frameworks) are not accepted.
- Low-level libraries are allowed, but every new third-party dependency needs the maintainer's
  explicit approval and a record in [docs/dependencies.md](docs/dependencies.md) before use.

## Build and test

Requirements: Linux, CMake 3.20 or newer, and a C++20 compiler. On Debian/Ubuntu:

```sh
sudo apt install cmake g++ clang libx11-dev libpng-dev libjpeg-dev libfreetype-dev \
    libharfbuzz-dev libfontconfig1-dev libssl-dev
```

```sh
cmake --preset dev                 # tests + warnings-as-errors, build directory build-dev/
cmake --build --preset dev
ctest --test-dir build-dev --output-on-failure
```

CI runs the same configuration with both GCC and Clang; a change is expected to pass both. The
default build type is `RelWithDebInfo`; use `-DCMAKE_BUILD_TYPE=Debug` only for local debugging.
Headless runs need no display, and `--dump-*` / `--screenshot` are the primary debugging tools.

## Code and tests

- C++20, CMake, GCC and Clang warning-free (`TOYBROWSER_WERROR=ON` is the expected local setting).
- Keep module boundaries: `render` must not learn about HTML/CSS, the platform layer must not learn
  about page content, and `layout` consumes text metrics through the shell callback.
- Add deterministic, display-free tests for behavior changes. Tests must not depend on installed
  fonts, the public network, wall-clock time or machine paths; committed fixtures live under
  `tests/fixtures/` and ship their generators.
- No wall-time assertions in CTest. Performance work is verified with measurements and reported in
  the pull request instead.
- Format changed C++ files with `clang-format` (the repository ships `.clang-format`).

## Documentation and language

- Committed files are written in English, except `README.zh.md` and `docs/zh/**` (Chinese
  translations). Update the English source first.
- Update the documentation together with behavior changes, especially
  [docs/roadmap.md](docs/roadmap.md) known limits and [docs/handoff.md](docs/handoff.md) frozen
  contracts.
- Local, machine-specific or personal configuration must never be committed. Scratch files go in
  the gitignored `.tmp/` directory.

## Commits and pull requests

- One coherent change per commit; avoid mixing unrelated refactors or formatting.
- Describe what changed, why, and how you verified it (commands and results).
- Do not rewrite published history or force-push a shared branch.

## Reporting a problem

Open an issue with the page URL (or a minimal local HTML repro), the command used, the expected and
actual output, and whether it happens with `--dump-boxes` as well as with `--screenshot`. For
security-sensitive reports see [SECURITY.md](SECURITY.md).

## License

By contributing you agree that your contribution is licensed under the repository's
[MIT license](LICENSE).
