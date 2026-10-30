# Documentation

Reading order for a new team or a fresh agent. Read `../README.md` first for the one-paragraph
project summary, then follow this order:

1. [handoff.md](handoff.md) — current state, what is done, how to verify it, what comes next.
2. [architecture.md](architecture.md) — module map, data flow, data model, layer rules.
3. [roadmap.md](roadmap.md) — milestones, acceptance criteria, known limits, backlog.
4. [conventions.md](conventions.md) — C++, CMake, testing and documentation conventions.
5. [team-workflow.md](team-workflow.md) — how a round is planned, run and delivered.
6. [decisions.md](decisions.md) — locked technical decisions and why they are locked.
7. [dependencies.md](dependencies.md) — dependency policy and approval records.
8. [open-questions.md](open-questions.md) — decisions waiting for the user.
9. [zh/](zh/) — Chinese translations; English is always the source of truth.

Hard rules for every agent: [../AGENTS.md](../AGENTS.md).

Existing conventions worth knowing before touching code:

- One CMake library target per module, `src/<module>/`, public headers under `include/toy/<module>/`.
- The root and test `CMakeLists.txt` include a module as soon as `src/<module>/CMakeLists.txt`
  exists; module owners create their own file and never edit the shared ones.
- Executables are registered with CTest; every module keeps deterministic, display-free tests.
- Committed files are English (only `README.zh.md`, `docs/zh/**` and future i18n data are exempt).
