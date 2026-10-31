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

## 1b. Shared state

- **Task board** — provided by the agent runtime: create tasks, claim/complete with the current
  `revision`. A task has an id, subject, description (goal, write scope, contract, acceptance),
  owner, status, `revision`, `blocked_by` and advertised write scopes. If a runtime has no task
  board, keep the same fields in a tracked `docs/tasks/<milestone>.md` per round.
- **New module registration** — the root and test `CMakeLists.txt` hold explicit module lists.
  The Lead adds a new module name once; directories that do not exist yet are skipped. Module
  owners never edit those shared lists.
- **Git** — one grouped commit per round, made by the Lead only.

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
- Builds in its own directory (configure with `-DTOYBROWSER_BUILD_TESTS=ON` when its module has
  tests); never touches another member's build tree.
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

1. Fresh build with each supported compiler group (GCC and Clang), configured with
   `-DTOYBROWSER_BUILD_TESTS=ON -DTOYBROWSER_WERROR=ON` (tests are opt-in; a plain configure
   builds only the browser), warnings as errors.
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
- English first: update `README.md` before `README.zh.md` and make their status lines match in the
  same round.
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
- Repository files in English; user-facing conversation follows the user's own language
  preference; personal and machine-specific details stay out of the repository.
- No implementer commits, installs or downloads; dependency changes need user approval first.
- Keep history readable: one grouped commit per round, not a stream of fragments.
- Docs are updated while working, not reconstructed at the end.

## 11. Communication protocol

- Blocker messages: what is blocked, what is needed, what can continue meanwhile.
- Contract questions: state the ambiguity, propose a default, note the affected members.
- Completion reports: files changed, verification evidence, uncertainties, risks.
- No polling loops; wait for events instead of repeatedly asking.
- Team-internal messages may use whatever language is most efficient (English recommended for
  precision); conversation with the user follows the user's own language preference.
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
- M3: the English `README.md` status drifted behind `README.zh.md` because a scripted text
  replacement silently did nothing. Verify automated doc edits (grep the new text after replacing)
  and check README/README.zh parity before delivery.
- Review round after M3 (two read-only reviewers with fresh context, code and docs, one round):
  found one real bug (`--size` unbounded), two contract/robustness gaps (`file://` ignored the
  body-size cap, dump indentation was quadratic) and several stale docs. A cheap review every 2-3
  milestones is worth it; keep reviewers read-only and route fixes to module owners.
- M4: publishing the css public header first let the style module be written truly in parallel
  against the contract; the only coordination was "retry configure until the css target exists".
- M4: the same dump-indentation problem reappeared in the new style dump, so the owner applied the
  existing cap pattern immediately; when fixing a class of bug, check the other dumps too.
- M4: teammate self-reports flagged contract gaps (inline `!important`, named-color values,
  unbounded style dump indentation) that were accepted or fixed by the Lead; keep that reporting
  habit — it is cheaper than a review round.
- M5: publishing the layout public header as an explicit checkpoint (the M4 lesson applied up
  front) let the shell/CLI owner start in parallel with the layout implementation; the only extra
  coordination was one short "header published" message.
- M5: two Lead-owned enabling changes — the header-only `tests/support/check.h` plus a
  `toy_test_support` interface target — removed a repeated infrastructure decision from the
  implementers and fulfilled the roadmap backlog item without touching old tests.
- M5: the layout contract had six genuinely ambiguous corner cases (br vs display, trailing empty
  lines, split-inline padding, whitespace-only nodes, hidden html, fit checks with pending inline
  padding). Listing them in one message and getting one approval round-trip was much cheaper than
  discovering divergent behavior at integration.
- M5: because the box dump is user-visible and frozen, the Lead hand-derived the expected end-to-end
  geometry in the contract and both the layout and shell owners derived it independently; they
  matched on the first try. Put computed golden values in the contract when an output format is
  frozen.
- M5: deep-nesting safety was designed in from the start (iterative layout, no DOM recursion, capped
  dump, 10k-div regression test) instead of being discovered by a review round; the full 31-test
  suite runs in under a second.
- M6: the header-checkpoint pattern worked a third time — publishing `render::DisplayList` early let
  the `paint` module and then the shell/CLI slice proceed in parallel against a frozen contract.
- M6: Lead review of the rasterizer found a real robustness bug (huge CSS `font-size` made glyph
  iteration unbounded and overflowed the source-mapping arithmetic). It went back to the module
  owner as a small follow-up task with a reproduction; pre-fix vs post-fix screenshots proved normal
  output unchanged. Treat renderers as adversarial-input code from the start.
- M6: a deterministic renderer gives a very cheap acceptance gate — `cmp` two compilers' PPMs, plus
  converting one screenshot to PNG and looking at it, caught nothing but would have caught glyph or
  paint-order mistakes that assertions alone can miss.
- M6: the shell owner derived the pixel-test geometry from the frozen `--dump-boxes` output before
  writing assertions, so the end-to-end test failed only on the not-yet-implemented rasterizer, not
  on layout disagreements.
- M6: window mode was smoke-tested without extra tooling by launching the browser, finding its
  specific client window in `xwininfo -root -tree`, and capturing it with ImageMagick `import`
  (no `xdotool` needed); capture only the client window, not the root.
- M7: freezing all five public headers up front (net/layout/paint/platform/shell) let three modules
  plus the shell session be written in parallel against explicit contracts; the cost is that the
  default full build is broken in `toy_shell` until the shell wave lands, so every kickoff prompt
  must say "build only your own targets, the full build is expected to fail". That sentence removed
  the only likely source of wasted debugging.
- M7: one headless `shell::Browser` session class made links, history, scrolling and titles testable
  without a display, and the window loop became a thin presenter; routing `--screenshot` through the
  same class means the two presentation paths cannot drift apart.
- M7: a pre-round `git worktree` of the previous commit is a cheap byte-identical regression gate
  when painting changes: after adding viewport clip commands, all dumps and scroll-0 screenshots
  were `cmp`-identical to the baseline.
- M7: platform input cannot be unit-tested headlessly, so the platform owner wrote a throwaway X11
  smoke program (title readback, button/keysym mapping) in `.tmp/`; the Lead's real-window smoke
  then only had to prove the shell wiring.
- M7: the real-window smoke needs the *client* window, not the window-manager frame (both carry the
  title); find it with `xwininfo -root -tree`, capture with `import -window`, and inject input with
  a small XSendEvent helper linked only against libX11 — no `xdotool` and no new dependency.
- M7: hardcoding the fixture's link coordinates from a `--dump-boxes` run (as in M6) kept the
  navigation test headless and fast; the fixture and coordinates must move together.
- M7: a read-only subagent audit of the hand-authored glyph table (parse the bytes, print each
  glyph as ASCII art, compare against the intended character) found 11 real shape/alignment errors
  that every existing pixel test missed, because those tests only rendered `H` and `H` is
  invariant under mirroring/flipping. The fixes were verified by rendering a zoomed before/after
  sheet. Glyphs are reviewed visually rather than by a frozen fingerprint test; render a sample
  when glyph quality is questioned — and do not re-add the removed `render.glyphs` test.
- M8a: publishing all frozen interface headers before spawning (`render::Image`/`DecodePng`,
  `DrawImage` on the display list, `LayoutOptions::replaced_sizes`, `paint::ImageMap`) let render,
  layout+paint and shell work in parallel without negotiating signatures; the only contract defect
  was a boundary-wording ambiguity ("at or above" vs "larger than") caught by an owner. Freeze the
  exact inclusive/exclusive boundary semantics in the header, not just the constant name.
- M8a: the user's clarification that "from scratch" means browser functionality, not low-level
  codecs/font machinery, unblocked M8; scope clarifications must be recorded in
  `docs/decisions.md` and `docs/dependencies.md` immediately so later rounds do not re-litigate them.
- M8a: hand-generated fixtures with documented exact pixel content (a throwaway script in `.tmp/`,
  only the PNGs committed, `tests/fixtures/images/README.md` describing every pixel) enabled both
  decoder tests and end-to-end pixel assertions without any third-party test data.
- M8a: the M7 `git worktree` byte-compat gate paid off again — image-less dumps and screenshots
  were `cmp`-identical to HEAD after adding a new display-list command and an image map, so the
  round could prove it changed nothing for existing pages.
- M8a: giving `layout` a caller-supplied intrinsic-size map keyed by DOM node id (instead of a
  render dependency) kept the layer boundary intact and let layout be tested with fake sizes;
  `shell` builds the layout sizes and the paint image map from one page load so they cannot disagree.
- M8a: a pimpl-style public header (forward-declared page type, `std::unique_ptr`, out-of-line
  destructor) kept the growing private page model out of `toy/shell/browser.h` while the public
  session API stayed unchanged; `Browser` becoming non-copyable was fine because nothing copied it.
- M8a: libpng's simplified API plus an explicit byte-wise RGBA→ARGB conversion avoided host-endian
  assumptions, and checking the header size caps before allocation made adversarial images cheap to
  reject; the owner's ASan/UBSan plus randomized-decode scratch run found nothing and is a habit
  worth keeping for every new decoder.
- M8a: CTest cannot cheaply express "the command succeeds and writes a file of the right size" in
  C++, so a small `cmake -P` helper was used for the CLI/screenshot path only, leaving pixel
  assertions to the C++ test; keep such helpers narrow.
- M8b: the frozen pimpl header (`Font` with a private `Impl`) needed a private rendering bridge; the
  owner caught that a namespace-scope rasterizer could not reach the font state and proposed a
  minimal `friend struct internal::FontRasterizer` + forward declaration. Approving a small,
  signature-preserving header delta was cheaper than reshaping the design; when freezing a pimpl
  header, plan the internal bridge in the same contract.
- M8b: a second contract ambiguity (`DrawTextWithFont(font=null)` could mean "bitmap path" or "no
  command") was caught before implementation. Freeze whether a builder drops or appends a command
  for degenerate inputs; the builder-vs-rasterizer behavior (no-op here) belongs in the header text.
- M8b: the owner's risk report (no TTF raster size cap, so hostile CSS could make freetype allocate
  huge glyph bitmaps) was turned into a small follow-up task before the round closed; a clamped
  effective `kMaxRealFontSizePx` for metrics and painting fixed it and was tested with exact
  equalities. Adversarial-renderer review remains a standard habit (M6 lesson repeated).
- M8b: a self-authored minimal TTF generated with a stdlib-only throwaway script (committed font +
  README documenting design metrics/family, generator not committed) kept tests deterministic and
  free of installed fonts, exactly like the M8a hand-generated PNG fixtures. For fontconfig, a
  test-local `FONTCONFIG_FILE` whose only `<dir>` is the fixture folder made the family-lookup path
  deterministic too.
- M8b: anti-aliased pixels are library-version dependent, so real-font tests assert ink regions, AA
  presence (blended values between text color and background), clip bounds, no-ops and determinism
  instead of hardcoded coverage bytes; GCC/Clang byte-identical PPM `cmp` remains the cross-compiler
  gate. This follows the same visual-review approach as the bitmap font.
- M8b: keeping real fonts opt-in (default stays the bitmap font) made the M8a byte-compatibility
  gate trivial and kept all goldens meaningful; the tiny header bridge plus the opt-in CLI meant no
  existing test needed a pixel update, only the paint call sites gained a null font argument.
- M8b: the shell owner derived the real-font dump geometry independently with a scratch layout
  harness before the render implementation landed (as in M5/M6), so integration only had to confirm
  it; contract numbers in the task description again paid off.
- M9: the Lead built the cross-task test infrastructure before kickoff (committed test-only
  certificates plus a header-only in-process TLS loopback server), validated it with `openssl
  s_client` and a throwaway `.tmp` smoke program, and handed the frozen API to both implementers;
  this removed the "who writes the shared test server" negotiation and let the net and shell tasks
  run in parallel.
- M9: a frozen M7 assumption became a real failure once https worked: the nav fixture linked to
  `https://example.com/` and the test expected an unsupported-scheme error, so the full suite would
  have touched the public network and failed. The Lead repurposed the fixture link to `ftp://` and
  extended the shell owner's scope, keeping the failure-case coverage deterministic. Re-audit old
  "unsupported" tests whenever a scheme becomes supported.
- M9: TLS test determinism came from committed certificates plus the standard env override: the CTest
  `ENVIRONMENT` property sets `SSL_CERT_FILE=<test CA>`, which exercises the real verification path
  (the same binary without the env fails), instead of adding an in-code "trust this CA" hook. That
  keeps test-only trust out of production code and proves verification is always on.
- M9: the net owner handled the OpenSSL pitfalls explicitly: one lazily created process-wide
  `SSL_CTX` (trust env read once), non-blocking handshake/read poll loops on
  `WANT_READ`/`WANT_WRITE`, retrying `SSL_read`/`SSL_write` with the same arguments, treating clean
  `close_notify` and unclean EOF as end of stream, `SSL_OP_IGNORE_UNEXPECTED_EOF` with a reason-code
  fallback, and a once-per-process `SIGPIPE` ignore because OpenSSL's write path does not use
  `MSG_NOSIGNAL`. These belong in the contract, not only in the completion report.
- M9: the byte-compatibility gate became mechanical with a `.tmp` script that compares stdout,
  stderr, exit status and PPM bytes across 12 http/file cases between a `git worktree` of the
  previous commit and the new build, and between GCC and Clang; everything was identical, which let
  the round prove the net refactor changed nothing for existing pages.
- M9: manual public-site checks against documentation pages and public TLS test pages
  validated the security behavior end to end (expired/self-signed/wrong-host pages fail with
  readable errors and exit 1; normal pages fetch, dump and screenshot). Such checks stay manual and
  their network details never enter the repository.
- M10: the Lead froze the style/layout/paint/shell public headers and generated/validated the new
  fixture fonts before kickoff, so two implementers could work in parallel; the full tree was
  transiently broken, and the kickoff prompts again said "build only your own targets".
- M10: the implementer caught a structural bug in the Lead-generated fixture fonts with an exact
  diagnosis (the new TTFs' hhea table had one extra field, so `numberOfHMetrics` read as 0 and every
  shaped advance was 0). `fc-query` parsing a font does not prove its metrics are right: validate
  generated fixtures through real shaping/metrics, and re-validate after fixing the generator
  (hhea length 36, correct `numberOfHMetrics`); the generators are now committed next to the fonts
  so this validation is repeatable.
- M10: plan review found that font fallback alone would not make Chinese readable: layout broke
  lines only at ASCII whitespace, so a space-less Chinese paragraph was one unbreakable "word"
  overflowing the viewport. Two follow-up tasks (M10-D CJK break units + punctuation rules, M10-E
  strengthened Chinese fixture tests) were added with the same layout owner. Check end-to-end
  readability of the target language at intake, not just font lookup.
- M10: the user clarified mid-round that Chinese was only an example and that English + Chinese
  features come first, other languages best-effort. That was recorded in the roadmap known limits
  (generic fontconfig character coverage for other scripts; no bidi/RTL, full UAX#14 or dictionary
  breaking this round), which prevented fixture/scope growth.
- M10: determinism came from two generated fontconfig configs: the empty config (no font
  directories) forces the bitmap canvas and keeps every pre-M10 golden byte-identical, while the
  fixture config (only the self-authored fonts, generic families mapped to the Latin face) drives
  the new auto-selection and Han-fallback tests. No installed font and no in-code trust hook.
- M10: the byte-compatibility gate needed a deliberate update when a frozen format changed on
  purpose: the style dump gained `font-family=`, so the gate script normalizes that field before
  comparing everything else byte for byte, instead of loosening the gate or re-baselining goldens.
- M10: the parallel follow-up tasks were coordinated through the task board (reopen/new task +
  dependency notes): the shell owner first made the Chinese fixture tests segmentation-agnostic so
  they stayed green, then froze exact per-unit/wrap geometry once the layout task landed. That
  avoided a big test rewrite at integration.
- M10: manual real-site checks (no font flags, system fonts) confirmed fallback and wrapping while
  also exposing the pre-existing limits (external stylesheets still unimplemented, etc.); such
  checks stay manual and their network/site details never enter the repository.
- M11: the round started with measurements, not code: a throwaway benchmark (stage timings, scroll/resize
  frames, a callgrind profile and microbenchmarks) showed the real costs were missing text caches,
  O(page) work per frame and an unoptimised default build — not fill rate. Decide "GPU or not" from
  measured bottlenecks; in this case GPU was rejected with data (same raster time at 800x600 and
  1600x1000, text work dominated by shaping/glyph loading).
- M11: the Lead froze the work as three disjoint tasks before spawning (render caches, paint+shell
  caches, platform/window loop), which let three implementers run in parallel on disjoint files; the
  main coordination cost was that full-tree gates only became meaningful once all three landed.
- M11: a default `RelWithDebInfo` build with `-ffp-contract=off` was a one-line configuration change
  that removed 3-7x of measured bitmap-path cost while keeping every output byte-identical; verify
  such build changes with the full test suite, byte-compat gate and GCC/Clang PPM equality, never by
  assuming optimisation is output-neutral for floating-point layout.
- M11: the render owner first proved hb_font reuse equivalence with a standalone harness (7 fonts,
  14 sizes, CJK/Cyrillic/kerning, interleaved sizes) and then with a 5463-case old-vs-new exact
  comparison before relying on it; caches must be proven output-neutral, not assumed.
- M11: culling and cache tests deliberately compared against unculled/cold references (edge text,
  italic/AA overhang, images, tall backgrounds, a 1 px viewport sweep whose oracle was verified by
  injecting an off-by-one bug) instead of only asserting "it is faster"; a conservative
  `ceil(2 * font_size_px)` text margin is documented in the contract.
- M11: the window-loop owner removed a latent double layout (Browser::Open plus first Expose) and
  coalesced X11 event bursts; because queue/window behavior cannot be unit-tested headlessly, the
  evidence was a throwaway libX11 smoke that injected resize bursts/exposes and temporary
  instrumentation (removed afterwards). Keep that habit for event-loop changes.
- M11: the before/after benchmark harness and scrolled-frame reference generator stayed in `.tmp`
  (uncommitted); the delivery gates were full GCC/Clang WERROR tests, the bitmap byte-compat gate,
  and a 33-frame scrolled-screenshot comparison against the pre-M11 binaries. Do not add wall-time
  assertions to CTest; performance evidence belongs in the round report.
- M12: the Lead froze the `style` header delta and wrote/validated the shared header-only loopback
  HTTP server *before* spawning, so the shell implementer could assert exact request sequences
  (`/page.html`, `/redirect.css`, `/real.css`) instead of sleeping or touching the network. Validate
  shared test infrastructure with a throwaway `.tmp` smoke (here: a real `net::Fetch` redirect
  through the server) before handing it to implementers; the same pattern as M9's TLS fixtures.
- M12: two implementers worked in parallel against the frozen header even though the shell needed
  the style symbol at final link time; the kickoff prompts said "build your own targets, report if
  the symbol is missing", which avoided wasted debugging. The Lead still re-ran the full GCC and
  Clang gates from fresh build trees rather than trusting the reports.
- M12: the byte-compat gate needed explicit negative fixtures — a page with no `<link>` and a page
  whose links are all filtered/failing — to prove the new page-triggered resource requests changed
  nothing for pages without applicable sheets. Add such "no-op feature input" fixtures whenever a
  round makes pages request more resources, and keep them in the gate case list.
- M12: the manual real-site check (`--dump-style`/`--screenshot https://www.python.org/`) was the
  highest-value acceptance evidence: it proved external CSS actually applies, and it also exposed
  the deliberate media-filter limit (`media="not print, ..."` skipped). Keep one documented manual
  real-site check per real-page-facing round; transient conditions are acceptable as long as the
  result is reported honestly.
- M12: request-count assertions (one fetch per `<link>` occurrence, no dedup) are the deterministic
  way to freeze resource-loading behavior; do not assert wall time. Loopback servers are test-only
  infrastructure and must not leak into production code.
- M13: the Lead froze the `net::Client` header, extended the shared loopback HTTP/TLS test servers
  with persistent-connection modes and connection/request counters, and validated them with a
  throwaway `.tmp` smoke before spawning. That let both implementers assert connection reuse and
  request counts immediately instead of inventing their own servers. For any networking round, put
  the counting test infrastructure in place first.
- M13: the serial dependency between module halves was handled by freezing the header and letting
  the shell implementer write against it while the net implementer developed behind it; the shell
  reported "compiled, blocked on link symbols" without wasted debugging, and the Lead messaged it
  as soon as the net symbol landed. Keep that explicit "compiled / waiting for symbol" checkpoint.
- M13: byte-compatibility had to cover the new transport transparently: the 23-case gate compared
  the pre-M13 binary with the new one (dumps, exit status, PPMs, including the M12 css_site pages)
  and GCC/Clang outputs; all were identical. When a round changes how bytes are fetched, the
  no-output-change gate is the primary safety net, not only the unit tests.
- M13: the acceptance measurement is manual and environment-dependent: run the old and new binaries
  back-to-back on the same page and report medians/ranges (python.org 800x600: 2.07 s -> 0.99 s
  median; w3.org: 3.88 s -> 1.79 s, with byte-identical screenshots). Never put these numbers in
  CTest; record them in the round report and docs.
- M13: both implementers stress-ran the counter-based tests (15-20 repeats) to check determinism
  before delivery; keep that habit for any test that observes connection/request counts.

- M14a: the roadmap's CSS batch was too large for one round, so intake used a real-site property
  survey (fetch the stylesheets into `.tmp/`, count declarations with a throwaway script) to pick the
  highest-impact slice: values & typography first (%, rem, colors, logical properties, line-height,
  text-align, visibility, min/max sizes, auto margins), box decorations (borders/box-sizing/overflow)
  to M14b. Scope decisions backed by measured frequency are cheaper to defend and to re-scope.
- M14a: the frozen header published the whole value model (`Length`/`AutoLength`/`LineHeight`/
  `TextAlign`/`Visibility`) before spawning, which let style, layout and paint/shell work in parallel
  against one contract. Layout/paint could compile immediately and run their tests as soon as the
  style implementation landed; the "compiled, waiting for computed values" checkpoint avoided
  duplicate work. A full-tree gate only becomes meaningful after all three land.
- M14a: appending the new style-dump fields at the END of each line (instead of inserting them
  mid-line) kept every existing regex/golden matching; the byte-compat gate needed only one
  normalization slice (`sed -E 's/ min-width=.*$//'`) and a committed legacy fixture proved pre-M14
  box dumps/PPMs are byte-identical. Prefer additive, end-of-line dump changes when a frozen format
  must grow.
- M14a: the cross-compiler gate was worth running again — GCC 15 silently accepted unused
  anonymous-namespace constants in a new layout test that Clang `-Werror` rejected. Any round that
  adds test helpers must build and run the full suite with both compilers, not just the module
  subsets.
- M14a: manual real-site comparison (explicitly allowed by the user) supplied both the acceptance
  evidence and the next round's scope: gnu.org's red nav bar, w3.org navigation line-spacing and
  python.org's centered/line-height elements visibly improved, while the unsupported-property
  frequency (borders ~385 declarations, border-radius ~121, position/float ~219) directly motivated
  splitting borders into M14b. Keep real-site content/screenshots in `.tmp/` and out of tests/repo.
- M14a: bitmap and real-font text differ by design — bitmap glyphs stay top-aligned in the line box
  (paint ignores the baseline for the bitmap path) while real fonts use the half-leading baseline,
  so line-height/underline screenshots are only representative on real sites (system fonts). Tests
  pin the deterministic bitmap path and the contract documents the divergence; do not "fix" the
  bitmap path without a pixel-compat decision.

- M14b: the box-decoration round showed the value of the used-width helper contract — border widths
  enter layout/painting only through `UsedBorderWidth` (0 for none/hidden), and every border/box
  term is appended last in the float expressions, so a page with default borders keeps bit-identical
  geometry. Verify this with the previous round's binary (31-case gate) instead of trusting the
  module tests, and add a capstone comparison against an even older baseline (M13) to prove the
  M14 changes only affected pages that use the new properties.
- M14b: overflow clipping was deliberately scoped to painting (padding-box clip pairs, no
  scrollbars, `ContentHeight` unchanged, either-axis-hidden clips). Writing that choice into the
  paint contract and roadmap known-limits made the e2e pixel tests unambiguous; an ambiguous
  "overflow" scope would have pulled in a whole scroll-container project.
- M14b: border/decoration paint simplifications (every style solid, 1px decoration rows with frozen
  offsets, currentColor resolved at paint/dump time, alpha dropped only in the dump) were frozen in
  the contract header/README before implementation; the paint tests assert exact command geometry
  and a hand-built pre-M14b reference list, which is what kept the "no-new-property pages unchanged"
  promise verifiable.
- M14b: the real-site check again doubled as the M15 scoping survey: python.org now paints 148 used
  borders and its universal `*{box-sizing:border-box}` reflows 836 elements (23.8% pixel diff),
  w3.org's skip-link border applies (0.53%), gnu.org's skip link paints its border (6.65%); the
  remaining gap list (position/float 219 declarations, border-radius 121, box-shadow 129, display
  inline-block/flex/grid 132, transforms 105, CSS variables, @media/@font-face) is the M15+ backlog.
- M14 overall: splitting the oversized CSS batch into two sub-rounds with independent contracts,
  task sets and commits (M14a values/typography, M14b box decorations) kept each reviewable and
  allowed the same three teammates to be reused; each sub-round ran its own GCC/Clang, byte-compat
  and real-site gates. When a roadmap batch is clearly larger than one round, propose the split
  during intake with measured evidence instead of starting a mega-round.
