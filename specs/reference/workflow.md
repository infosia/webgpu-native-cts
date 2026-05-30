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
cmake --build build -j
build/cts_unittests                       # harness self-tests (no GPU)
build/cts 'webgpu:api,validation,createBuffer:*'   # run a query
ctest --test-dir build                    # if registered with CTest
```

## Path hygiene

Committed files (`docs/`, `specs/`, code) must **not** contain machine-specific paths: no absolute
home paths (`/Users/<name>/…`, `/home/<name>/…`) and no `../<external-project>` workspace-layout
paths. Use neutral placeholders instead (e.g. `<wgpu-native checkout>`, "your Dawn checkout"). The
only place concrete local paths belong is `HANDOFF.md` / `REPORT.md`, which are **git-ignored**.

## Language

All artifacts (docs, specs, handoff/report, code, comments, identifiers) are **English**.
Conversation with the user is **Japanese**. See [`../../CLAUDE.md`](../../CLAUDE.md).
