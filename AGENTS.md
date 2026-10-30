# AGENTS.md

Rules for AI agents (team leads, subagents and team members) working in this repository.
This file contains **only general technical rules and working discipline**. Project design,
architecture, roadmaps and decision records live in `docs/`. Do not duplicate them here; keep
this file short and stable.

This file describes the project, not any particular machine or person: it must stay free of
machine-specific environment details, toolchain versions and personal preferences. Those belong in
the gitignored `AGENTS.local.md`.

## Project nature

- From-scratch toy web browser: experimental, fun, learning project. Not commercial, not
  production-ready, not security-hardened.
- Long-lived effort. Every step is an MVP: the smallest change that produces a working,
  verifiable result. Do not design for hypothetical future needs, do not pre-build abstractions
  that nothing uses yet, prefer the simplest thing that works.
- Core components (networking, HTML parsing, CSS, layout, painting, JS) are implemented here,
  not delegated to ready-made implementations.

## Hard technical constraints

- Main language: C++20. Build system: CMake only — no second build entry point (Makefile, Meson,
  Bazel, ...).
- Target platform: Linux desktop. Cross-platform support is out of scope unless the user asks.
- Code must compile warning-free with GCC and Clang. Never silence warnings with `-w` or blanket
  pragmas.
- Every file committed to git is written in English, including code comments, docs, filenames and
  commit messages. The only allowed exceptions are `README.zh.md`, `docs/zh/**` and, once they
  exist, browser UI localization data (e.g. `assets/i18n/**`). Update the English version first,
  then the translations; English is always the source of truth.
- Do not depend on machine-specific environment: no absolute paths, no hardcoded compiler or
  library versions, no assumptions about a particular GPU, window manager, font directory or
  installed package set. Build and test steps must be reproducible on another conforming machine.
- The browser must run and render headless: every feature must be exercisable, renderable and
  testable without a window. The window is an optional presenter; headless CLI modes
  (`--dump-*`, `--screenshot`) are the primary debugging path.

## Dependency discipline (highest priority)

- NEVER install dependencies yourself: no apt/pacman/dnf, pip, npm, vcpkg, Conan, source builds;
  "just trying it out" is not an exception.
- NEVER use large or ready-made third-party components: existing HTML/CSS parsers, JS engines,
  layout or rendering engines, browser engines (WebKit/Chromium/CEF/Gecko), GUI frameworks
  (Qt/GTK/Electron and similar), single-header drop-in implementations of the above.
- Low-level libraries (libpng, zlib, freetype, ...) are allowed in principle, but ANY new
  third-party dependency — including these — requires the user's explicit approval first. Present
  purpose, license, size/maintenance cost, and whether we could implement it ourselves; wait for
  a clear yes, then record the approval in `docs/dependencies.md`.
- Do not download code or data from the network unless the user asks for it. Do not modify
  system-wide configuration, shell configuration or toolchain files.

## Engineering and code discipline

- CMake: modern target-based style (`target_link_libraries`, `target_include_directories`,
  `target_compile_features`). No global `include_directories` / `add_definitions`.
- Out-of-source builds only; build directories stay untracked.
- Never commit build artifacts, binaries, downloaded files or caches.
- Before finishing a task: configure and build; run `ctest` if tests exist. If verification is
  impossible in this environment, say so explicitly.
- Small, reversible changes. No drive-by refactors, reformatting or dependency upgrades.
- Do not swallow errors or leave silently failing paths. Unimplemented code must be marked and
  reported, not hidden behind TODOs.
- Follow the module boundaries in `docs/architecture.md`; do not introduce dependency cycles or
  bypass layers. Renderer code must not gain knowledge of HTML/CSS; the platform layer must not
  gain knowledge of page content.

## Team work

- Only the Lead creates tasks, spawns members and fixes interface contracts. Teammates claim one
  shared task, work only inside its write scope, and ask the Lead when the contract is unclear.
- The Lead owns shared files (root `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/`), reviews
  every change, runs the final build and tests, and makes commits. Teammates never commit.
- Teammates keep their own build directory, keep their module self-contained, and report changed
  files, verification output and open questions when done.
- Interface contracts must state units, ownership and error behavior explicitly. An ambiguous
  contract is a Lead bug, not a teammate bug: raise it immediately.
- Parallel work requires disjoint write scopes. If two tasks must touch the same file, the Lead
  serializes them. Blocked work is reported, never silently worked around by widening scope.
- Every new feature or requirement follows the round playbook in `docs/team-workflow.md`
  (intake -> contracts -> kickoff -> parallel work -> integration -> delivery -> retro). Team norms
  and lessons live there too; keep only rules that every agent must follow in this file.

## Git discipline

- Group related changes into one commit. Do not split small or similar edits into many tiny
  commits: a commit should be one coherent, reviewable unit of work.
- Never rewrite published history (force push, rebase, amend, reset --hard) without asking the
  user. Never commit secrets or generated files.

## Working style

- Read `docs/` and the existing code before acting. Ask the user when a design direction is
  unclear; never silently make big decisions.
- Do only what the current task requires — no future features, no scope creep. If the task is
  ambiguous, ask instead of guessing.
- `AGENTS.local.md` (gitignored) holds local instructions: interaction language and any
  machine-specific or personal preferences. Pass task-relevant parts to teammates; committed
  files always stay English.
- New working rules go into this file; design, module maps and roadmaps go into `docs/`.
- Record new lessons and requirements continuously, as they appear — not only at the end of a
  task. Rules go here, design and status into `docs/`, process lessons into
  `docs/team-workflow.md`. Knowledge that only lives in a chat is lost.
