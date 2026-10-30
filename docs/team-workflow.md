# Team workflow

How the Lead and teammates work together. Process rules that every agent must follow are in
[AGENTS.md](../AGENTS.md); this file explains them and records lessons.

## Roles

- **Lead** — planner. Owns plans, documentation, interface contracts, the shared task board,
  shared build files (root `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/`) and final
  verification. Does not write module code when a teammate can do it; writes only contracts,
  shared wiring and documents.
- **Teammates** — implementers. One module or slice per task, own their write scope, verify
  locally, report back. They do not create other tasks, spawn members or commit.

## Task board

- One task per module or vertical slice, containing: goal, exact write scope, interface contract,
  acceptance criteria with runnable commands.
- Claim with the current revision; complete only when acceptance passes.
- Dependencies are recorded with `blocked_by`; tasks without dependencies start immediately.
- The Lead creates tasks with the exact acceptance commands so "done" is not subjective.

## Parallel work

- Disjoint write scopes, no exceptions. Shared files are Lead-owned and touched once per milestone.
- The root and test `CMakeLists.txt` include a module as soon as `src/<module>/CMakeLists.txt`
  (or `tests/<module>/CMakeLists.txt`) exists, so adding a module never requires editing shared
  build files.
- If a module needs a sibling's header or target while that sibling is still being written, code
  against the contract and retry the configure step; never create files outside your scope.
- The Lead routes interface decisions (units, ownership, error behavior) to every affected member
  as soon as they change.

## Verification and delivery

- Teammate: own build directory, build + `ctest` + the task's demo command, then report changed
  files, verification output and open questions.
- Lead: fresh build with both GCC and Clang, full `ctest`, headless screenshot check, a manual
  smoke test of anything that needs a display, review of the diff for scope and quality, then one
  grouped commit for the milestone.
- Documentation is updated in the same commit when behavior or status changes.

## Recording lessons and requirements

- Summarize and record continuously while working, not only at the end of a milestone.
- Put each new item in the right place: project rules -> `AGENTS.md`; design/interface decisions ->
  `docs/architecture.md`; status -> `docs/roadmap.md`; dependency approvals ->
  `docs/dependencies.md`; team process and lessons -> this file; open decisions ->
  `docs/open-questions.md`.
- The Lead updates the affected document in the same commit as the change; teammates report
  lessons and contract ambiguities in their reports so the Lead can record them.
- Knowledge that exists only in a chat message is not part of the project.

## Communication

- Blockers and contract questions go to the Lead immediately, not after the work is done.
- Reports are short and factual: what changed, what was verified, what remains uncertain.

## Lessons learned

- M1: state units and layout of shared structs explicitly in the contract — the `PixelView.stride`
  unit (pixels per row) needed a cross-module confirmation that a precise contract would have
  avoided.
- M1: CMake module auto-inclusion removed the main shared-file contention point of parallel work.
- Keep module `CMakeLists.txt` files self-contained; the Lead edits shared build files once per
  milestone.
- Teammates never run git commands; the Lead commits per milestone to keep history readable.
- M1: stray object files appeared at the repository root because a member compiled ad hoc outside
  their build directory. Always build inside your own build directory; the Lead keeps object and
  library files ignored and cleans the tree before committing.
- M1: window-dependent behavior cannot be unit-tested headlessly. The module owner complemented
  the dependency-free tests with a one-off X11 readback check (pixels, resize, key, close), and the
  Lead finished with a short real-window smoke test plus PPM pixel validation.
- M1: the Lead re-ran fresh GCC and Clang builds with warnings as errors and the full CTest suite
  after merging parallel work, then made one grouped commit for the milestone.
