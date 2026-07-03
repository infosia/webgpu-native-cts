# Workflow — planning ↔ coding-agent loop

The authoritative description of how work flows between **Claude** (planner/orchestrator) and the
**coding agent** (implementer). Referenced by [`../../CLAUDE.md`](../../CLAUDE.md).

## Roles

| | Claude (planner/orchestrator) | Coding agent (implementer) |
|---|---|---|
| Writes | `docs/`, `specs/`, `HANDOFF.md`, commit messages | production code (`src/`, `include/`, `tools/`, build files), `REPORT.md` |
| Does **not** | write production code | plan, edit `specs/`/`docs/`, change scope, commit |
| Also | reviews diffs vs acceptance criteria, runs `cmake --build`/`ctest`, manages git | — |

## Artifacts

- **`docs/`** — design & architecture (durable, committed).
- **`specs/`** — task specs + acceptance criteria (durable, committed). One spec per bounded unit
  of work, e.g. `specs/phase0-build-skeleton.md`.
- **`specs/reference/`** — process references (this file, `task-template.md`).
- **`HANDOFF.md`** (repo root, git-ignored) — the *current* task handoff Claude writes for the
  coding agent: which spec, the bounded scope, files expected to change, acceptance criteria,
  constraints, and any context the agent needs.
- **`REPORT.md`** (repo root, git-ignored) — the coding agent's report back: what changed, how it
  was verified, any deviations from the spec, and open questions.

## The loop

1. **Spec.** Claude writes or updates a spec in `specs/` (goal, in/out scope, interfaces,
   acceptance criteria, verification plan), grounded in `docs/`.
2. **Handoff.** Claude writes `HANDOFF.md` pointing at the spec with one concrete, bounded task.
3. **Implement.** The coding agent implements only that task, then writes `REPORT.md`.
4. **Review.** Claude reviews the diff against the acceptance criteria and runs
   `cmake --build build` / `ctest` (or the relevant target).
5. **Decide.**
   - **Pass** → Claude commits (see Git), then clears/rotates `HANDOFF.md` and `REPORT.md`.
   - **Fail** → Claude appends required changes to `HANDOFF.md`; back to step 3.

The coding agent never advances scope on its own: anything not in the spec/handoff is out of
scope and goes back to Claude as a question in `REPORT.md`.

## Verifying against yawgpu (fast → full)

yawgpu is the primary conformance subject, so most CTS work is a find→fix loop against it. The
**fast mode** (`cts --sample-formats`, which runs one representative texture format per major decode
family while keeping every *structural* axis full — see
[`../format-sampling-mode.md`](../format-sampling-mode.md)) is the **default driver** of this loop; a
**full run** is only for finalizing. Real-GPU runs use the Bash sandbox **disabled** (otherwise Metal
enumerates no adapters and every case false-fails); the coding agent never runs GPU CTS — Claude runs
all of it.

**Vertical-first.** Every divergence this suite has surfaced (F-025/026/027/028) was **structural** —
upload path, buffer layout, mip levels, 3D depth slices — **not** format-specific; one representative
format reproduces each. So format breadth is low-yield: prefer ports/sweeps that are dense in
*structural* patterns (dimension, mip, layer/slice, origin, partial sub-box, alignment edge cases,
aspect) over those that mainly enumerate formats, and lean on the fast mode (rep format × full
structural) to find them cheaply. The periodic full-format pass (step 4) still catches any truly
format-specific defect.

1. **Port** the new or changed test (coding agent) → Claude builds the backends.
2. **Find (fast).** `cts --sample-formats` on **Dawn** (a quick sanity that the test itself is
   correct — Dawn is the oracle) and on **yawgpu** (surface divergences fast). Triage findings into
   `docs/FINDINGS.md`.
3. **Fix.** The user fixes yawgpu → re-run `cts --sample-formats` on yawgpu to confirm the
   representative formats pass.
4. **Finalize stats (full).** Once fast yawgpu is clean, run the **full** suite (no flag) on yawgpu
   for the authoritative counts and to catch any non-representative-format-specific defect the
   sample missed; run the **full** suite on **Dawn** once to lock the oracle (this Dawn-full result
   is also the cross-backend Dawn record).
5. **Cross-backend record.** When yawgpu is fully clean, run the **full** suite on **wgpu-native**
   (the Dawn-full result from step 4 is reused — the test is unchanged), then update
   `docs/FINDINGS.md` / `docs/COVERAGE.md` / `README.md`.

Fast-mode counts are for iteration only — never report a `--sample-formats` summary as conformance
coverage (its stderr notice says as much). The committed catalog (`src/webgpu/listing.json`) and the
authoritative pass/fail counts always come from full runs.

**Combined (cross-test) pass.** Resource-lifetime bugs that only bite *across* tests sharing one
process — e.g. F-029, where `image_copy` leaked device resources and poisoned every later GPU test —
are invisible to per-file runs. Periodically, and whenever a new operation file lands, run a
**combined** whole-listing fast-mode pass (all file-level `:*` queries in a single `cts
--sample-formats` invocation) and confirm a clean summary.

## Spec format

Every task spec follows [`task-template.md`](task-template.md):

- **Goal** — one sentence.
- **Scope** — in / out (explicit non-goals).
- **Interfaces** — the exact signatures/headers/CLI this task must produce or match.
- **Acceptance criteria** — checkable, binary statements (commands that must succeed, outputs
  that must match). These are what Claude reviews against.
- **Verification** — how to prove it (build target, unit test, `cts --list` output, etc.).
- **References** — the `docs/` sections that define the design.

## Acceptance criteria style

Prefer mechanically checkable criteria:

- "`cmake --build build --target cts` succeeds with `-Werror`."
- "`build/cts_unittests` exits 0."
- "`build/cts --list 'webgpu:api,validation,createBuffer:*'` prints exactly N case lines matching
  the upstream count."

Avoid vague criteria ("works correctly", "is clean").

## Git (Claude only)

- Commit **only** after acceptance criteria pass and the build/tests are green.
- For non-trivial work, branch off the default branch first; never commit straight to the default
  branch when the change is substantial.
- Commit-message style: concise imperative subject (`area: summary`), then a body explaining the
  what/why. Match the existing history.
- The coding agent never runs `git commit`/`push`.

## Build / test commands

From [`../../docs/06-build-and-run.md`](../../docs/06-build-and-run.md):

```bash
cmake -S . -B build -DCTS_BACKEND=wgpu-native -DCTS_WGPU_NATIVE_DIR=<dir>
cmake --build build -j 1     # ALWAYS serial (-j 1): parallel compiles overload the dev machine
build/cts_unittests                       # harness self-tests (no GPU)
build/cts 'webgpu:api,validation,createBuffer:*'   # run a query
ctest --test-dir build                    # if registered with CTest
```

## Coding-agent command execution (codex output-polling constraint)

The coding agent runs in **codex**, whose `exec_command` is asynchronous: it launches the process,
then drains stdout in **30-second polling windows** with limited output per chunk. A command that
streams a burst of output fills the stdout pipe buffer and **blocks on `write()` until codex reads it
30 s later** (pipe back-pressure). This throttles throughput ~100×: a test/build that finishes in
~25 s when run freely (Claude's Bash, the user's terminal) can take **30–73 min** inside codex purely
from this drain — root-caused 2026-06-17 on the sibling yawgpu project from `~/.codex/sessions`
receipts (the build "Finished" at +2.5 min; the remaining ~70 min was 30 s polls trickling test
output). It is **not** build/link time and **not** lock contention.

**Rule:** in a codex handoff, any long-running or verbose command (test suites, full builds —
`ctest`, `cmake --build`, `build/cts_unittests`, and the cargo gates on backend deps) must redirect
output to a file and report the exit code, never stream to the console. Builds must also be
**serial** — an explicit `-j 1` on every `cmake --build` / `cargo build` (parallel compiles
overload the dev machine; this applies to Claude's own builds too):

```bash
ctest --test-dir build > /tmp/out.log 2>&1; echo "EXIT=$?"; tail -n 40 /tmp/out.log
```

This lets the process run at full speed while codex reads only the small tail. A test-name **filter
does not avoid the cost** if the runner still spawns many binaries that each flush output; redirect to
a file (preferred) or target a single binary. **Claude** runs the full build/test gate directly via
its own Bash (no polling harness), so it remains the backstop. GPU CTS is Claude-only regardless (see
"Verifying against yawgpu").

## Path hygiene

Committed files (`docs/`, `specs/`, code) must **not** contain machine-specific paths: no absolute
home paths (`/Users/<name>/…`, `/home/<name>/…`) and no `../<external-project>` workspace-layout
paths. Use neutral placeholders instead (e.g. `<wgpu-native checkout>`, "your Dawn checkout"). The
only place concrete local paths belong is `HANDOFF.md` / `REPORT.md`, which are **git-ignored**.

## Language

All artifacts (docs, specs, handoff/report, code, comments, identifiers) are **English**.
Conversation with the user is **Japanese**. See [`../../CLAUDE.md`](../../CLAUDE.md).
