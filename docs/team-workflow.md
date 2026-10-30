# Team process

How the Lead and implementers run a round of work. This is the playbook; the rules every agent
must follow are in [AGENTS.md](../AGENTS.md).

## The model in one page

- Work happens in **rounds**: one milestone or requirement, ending in one grouped commit.
- The **Lead** plans, freezes contracts, owns shared files, reviews, verifies and commits.
  **Implementers** own modules, write code, verify locally and report evidence.
- Every round: **Intake → Contracts → Kickoff → Parallel work → Integration → Delivery → Retro**.
- The **task board** is the shared state; **`docs/`** is the shared memory; **verification
  evidence** is the only definition of "done".
- Default parallelism: 2–3 implementers, one module each, disjoint write scopes.

## 1. Roles

**Lead** — planner and integrator.

- Turns a request into one milestone slice with explicit non-goals.
- Writes interface contracts: signatures, units, ownership, error behavior, file formats.
- Owns shared files (root `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/`, `AGENTS.md`) and all
  commits. Does not implement module code when an implementer can.
- Answers contract questions, re-scopes, keeps the task board and docs current.
- Reviews every diff, runs the final verification, delivers the round to the user.
- Escalates user-level decisions instead of guessing.

**Implementer** — module owner.

- Reads `AGENTS.md`, the docs and its task before writing code.
- Claims the task with the current revision and stays inside its write scope.
- Keeps a private build directory, verifies locally (build, tests, demo command).
- Reports changed files, verification output and uncertainties; raises blockers immediately.
- Never commits, never installs, never edits shared files or a sibling's module.

**Peer reviewer** — optional, used when the Lead authored the change or a module is risky. Same
checklist as the Lead review; findings go to the owner, not into a direct edit.

## 2. Phase 0 — Intake

Before any code:

- Restate the request in one sentence: goal, scope, non-goals, success criteria.
- Check constraints: dependencies needed (approved?), platform, determinism, testability.
- List decisions that only the user can make; ask early, before contracts are frozen.
- Pick exactly one milestone slice that can be demoed at the end.
- Identify risky parts (visual output, networking, parsing edge cases) and how they will be
  verified.
- Estimate: number of modules, likely files, parallel split, expected size. If a slice looks
  larger than one round, cut it.

Output: a short plan to the user and, when accepted, updated roadmap entries.

## 3. Phase 1 — Plan and freeze contracts

Split the slice into 2–4 tasks. One task = one module (or one thin vertical path) = one owner.

Task description template:

```text
Goal: one sentence.
Write scope: the only paths the owner may modify.
Contract: public headers/types, units, ownership, error behavior, file formats.
Acceptance: exact commands that must pass and the expected observable result.
Dependencies: other tasks (blocked_by) or "none".
Rules: no commits, no installs, English files, private build directory.
```

Definition of Ready — all must be true before spawning:

- The contract is explicit; no unit or lifetime is left to interpretation.
- Acceptance commands are runnable and unambiguous.
- Any new dependency has user approval recorded in `docs/dependencies.md`.
- Write scopes are disjoint; shared files have exactly one owner (the Lead).
- Each task is small enough for one session; oversized tasks are split.

## 4. Phase 2 — Kickoff

- Spawn at most three implementers. Each prompt is self-contained: repo path, docs to read, task
  id, contract summary, scope, build commands, rules, report format.
- The implementer claims the task with the current revision, then reads before writing.
- Contract defects found during reading are reported to the Lead; the Lead fixes the contract and
  re-announces it to every affected member.

## 5. Phase 3 — Parallel work

Lead:

- Watches the task board and answers questions promptly; never lets a member idle on a blocker.
- Announces every contract change to all affected members at once.
- Records decisions in the right document while the round is running.
- Keeps replies to "what changed / what is unclear".
- Does not start implementing a module that has an owner; wiring and shared files only.

Implementer:

- Stays inside the write scope; a needed change elsewhere becomes a message, not an edit.
- Builds in its own directory; never touches another member's build tree.
- Reports blockers and ambiguities early, with what is blocked and what is needed.
- Keeps the module self-contained and follows module boundaries.
- No drive-by refactors, no new abstractions without a contract change.

Checkpoints are lightweight: report only on completion, on a blocker, or when the Lead asks.

## 6. Phase 4 — Integration and verification

The Lead never trusts a report alone. Review:

- Scope: only the declared files changed; no shared files touched by implementers.
- Boundaries: no layer violations, no dependency cycles, no page logic in the renderer.
- Quality: MVP-sized, no speculative abstractions, no TODOs hiding unfinished work, no
  machine-specific paths or versions, English comments, formatting clean.
- Tests: nominal, edge and failure paths; deterministic; no display or network.

Verification gates, run by the Lead after merging parallel work:

1. Fresh build with each supported compiler group (GCC and Clang), warnings as errors.
2. Full `ctest` suite on each fresh build.
3. The round's demo command, with its observable output checked (not just exit code).
4. Manual smoke for anything that needs hardware or a display (window, network), documented in
   the report.
5. `git status` clean of build artifacts; no stray files.

Defects go back to the owner as a follow-up task or message; the Lead does not silently patch a
module it does not own. Only wiring and shared-file fixes are the Lead's.

## 7. Phase 5 — Delivery

- Update the documents the round changed: roadmap status, README status, architecture or
  conventions when behavior changed, dependency approvals, open questions, lessons.
- Make one grouped commit per round with an English message that names the milestone.
- Close the tasks; keep them on the board as the round's audit trail.
- Report to the user: what shipped, verification evidence, what changed in the docs, remaining
  risks, and the proposed next round. Ask for acceptance or corrections.

## 8. Phase 6 — Retrospective

Three questions, short answers, recorded in the lessons log:

1. What worked and should stay?
2. What hurt or wasted time?
3. What should change in the playbook, a convention or `AGENTS.md`?

Only durable rules go into `AGENTS.md`; the rest lives here. Add the lesson in the same commit as
the round when possible. Clean up stale build directories and stray files.

## 9. Round types

- **Feature / milestone** — the default: contracts, parallel modules, full verification.
- **Bugfix** — write the failing regression test first, fix, verify the test fails before and
  passes after; no scope widening.
- **Refactor** — behavior-preserving, existing tests must stay green, one module at a time; no
  refactor mixed into a feature round.
- **Spike / research** — timeboxed, no production code, output is a short document or decision;
  findings feed the next plan.
- **Docs round** — no code; verify links, statuses and checklists still match reality.

## 10. Collaboration conventions

- One owner per path; shared files have exactly one owner, the Lead.
- Contract first, code second: units, ownership, errors and formats are decided before work starts.
- No silent scope widening; extra work becomes a new task or a question.
- Evidence over claims: every completion report includes the commands run and their output.
- Isolated build directories; never build inside the source tree or a sibling's build dir.
- Deterministic tests: no display, network, fonts, locale or clock dependencies in unit tests.
- Repository files in English; user-facing conversation in Chinese; personal and machine-specific
  preferences stay in the gitignored `AGENTS.local.md`.
- No implementer commits, installs or downloads; dependency changes need user approval first.
- Keep history readable: one grouped commit per round, not a stream of fragments.
- Docs are updated while working, not reconstructed at the end.

## 11. Communication protocol

- Blocker messages: what is blocked, what is needed, what can continue meanwhile.
- Contract questions: state the ambiguity, propose a default, note the affected members.
- Completion reports: files changed, verification evidence, uncertainties, risks.
- No polling loops; wait for events instead of repeatedly asking.
- User decisions are escalated, never invented.

## 12. Anti-patterns

- Spawning many members for small work; more coordination than work.
- Vague contracts ("just make it work") that produce silent rework.
- Implementers editing shared files or each other's modules.
- Accepting "tests pass" without fresh builds and observable output.
- Multiple agents committing, or committing unverified parallel work.
- Scope creep, speculative abstractions, refactors smuggled into features.
- Documentation drift: code lands, docs keep describing the previous state.
- Tests that need a display, a network or installed fonts.
- The Lead heroically fixing a module instead of routing the defect to its owner.
- Rewriting history that is already published.

## 13. Lessons log

- M1: state units of shared structures explicitly in the contract; `PixelView.stride` needed a
  cross-module confirmation that a precise contract would have avoided.
- M1: CMake module auto-inclusion removed the main shared-file contention point of parallel work.
- M1: window behavior cannot be unit-tested headlessly; the module owner added a one-off readback
  check and the Lead finished with a real-window smoke test plus PPM pixel validation.
- M1: stray object files at the repository root came from ad-hoc compilation; build only in your
  own build directory.
- M1: the Lead re-ran fresh GCC and Clang builds with warnings as errors and the full test suite
  after merging parallel work, then made one grouped commit.
- M2: the Lead must update the shared module lists in root/`tests` CMake before spawning a module
  owner; the net module was found missing from those lists during kickoff.
- M2: adding a field to a struct that is aggregate-initialized triggers
  `-Wmissing-field-initializers` under `-Wextra -Werror`; prefer field-wise setup in callers or
  brace initialization that names every field.
- M2: end-to-end CLI behavior can be tested with CMake alone — `file(WRITE)` fixture plus
  `PASS_REGULAR_EXPRESSION`/`WILL_FAIL` — no test framework or stdout capture needed.
- M2: a non-2xx HTTP response is a successful transport, so `--dump-body` prints the error page
  body; navigation semantics arrive with the later milestones.
- M3: pre-updating the shared CMake module lists before spawning (the M2 lesson) removed the
  kickoff blocker entirely; the two serial tasks (dom, then shell) needed no other coordination.
- M3: decisions outside the frozen contract (verbatim comment data, whitespace routing, no implied
  head routing) were reported by the owner and accepted as documented MVP limits in the roadmap's
  known-limits list rather than silently changed.
- M3: robustness checks for parsers (random/malformed inputs, deep nesting, ASan/UBSan) were cheap
  and worth keeping as a habit for the dom and css modules.
- M3: a test assertion built on CMake regex was itself verified with `cmake -P` before being
  trusted; assertions need verification too.
