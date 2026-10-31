# Documentation

The [top-level README](../README.md) is the user-facing entry point: what the browser does, how to
build and run it, and where the demos are. This page is the map for everything deeper.

## Learn how it works

1. [architecture.md](architecture.md) — module map, data flow, data model, rendering and threading.
2. [roadmap.md](roadmap.md) — the milestone sequence, acceptance criteria and known limits.
3. [dependencies.md](dependencies.md) — third-party policy, approval log and why each library is used.

## Contribute

1. [../CONTRIBUTING.md](../CONTRIBUTING.md) — build, test, style and contribution workflow.
2. [conventions.md](conventions.md) — C++, CMake, testing and documentation conventions.
3. [decisions.md](decisions.md) — locked technical decisions and the reasoning behind them.
4. [../SECURITY.md](../SECURITY.md) — security model, test-only TLS keys and reporting.

## Current state and process

These are the project's development records; they are kept public so the history and the working
method are auditable.

1. [handoff.md](handoff.md) — current state, frozen contracts, verification steps, takeover guide.
2. [team-workflow.md](team-workflow.md) — round-based development process, roles, quality gates and
   lessons learned (the repository is developed with AI agents).
3. [open-questions.md](open-questions.md) — decisions the maintainer still has to make.
4. [../AGENTS.md](../AGENTS.md) — rules for AI agents working in this repository.

## Reference

- [assets/](assets/) — screenshots used by the top-level README (generated from the fixture pages).
- [zh/](zh/) — Chinese translations; the English files in this directory are the source of truth.

## Conventions worth knowing before touching code

- One CMake library target per module, `src/<module>/`, public headers under
  `src/<module>/include/toy/<module>/`.
- Module targets are registered in the explicit lists in the root and test `CMakeLists.txt`.
  New modules are added there once; module owners create only their own
  `src/<module>/CMakeLists.txt`.
- Executables are registered with CTest; every module keeps deterministic, display-free tests.
- Committed files are English (only `README.zh.md`, `docs/zh/**` and future i18n data are exempt).
- The default build is `RelWithDebInfo`; local configuration and scratch files stay in the gitignored
  `.tmp/` directory and never enter the repository.
