# CLAUDE.md — permanent development rules

## Roles (read first)

Implementation is done by a **separate coding agent**. **Claude plans and
orchestrates** — it authors `specs/`, emits task handoffs, reviews the coding
agent's diffs against acceptance criteria, runs `cmake --build`/`ctest`,
and manages git (`init`/`add`/`commit`). Claude does not write production
code; the coding agent does not plan, edit `specs/`, change scope, or commit.
Full detail: `specs/reference/workflow.md`.

## Layout

- `docs/` — design & architecture (durable, committed): overview, harness,
  abstraction, authoring/porting guides, build/run, roadmap, UPSTREAM, COVERAGE.
- `specs/` — task specs + acceptance criteria the coding agent implements
  against, plus `specs/reference/` (workflow, templates). Durable, committed.
- `HANDOFF.md` / `REPORT.md` (repo root, git-ignored) — the live exchange:
  Claude writes `HANDOFF.md` (task + spec ref + acceptance criteria); the
  coding agent writes `REPORT.md` (what changed, how verified). Ephemeral.

## Language

- **All repository documentation, specs, comments, and identifiers: English.**
- Conversation with the user (chat responses): Japanese.

## Privacy / repo hygiene

- No credentials, signing material, or device-specific secrets committed.
- `.gitignore`: `build/`, `.claude/`, `HANDOFF.md`/`REPORT.md`, local test transcripts.
- Generated build artifacts (the CMake build dir, `tests_generated.*`) are not
  committed. The checked-in `src/webgpu/listing.json` test catalog is the
  deliberate exception (it is the suite catalog, like upstream's `listing.js`).

## Tooling — builds

- **Always compile serially: explicit `-j 1`** on every `cmake --build` and
  `cargo build` (Claude, the coding agent, and docs/spec examples alike).
  Parallel compiles overload the dev machine (CPU/memory). CTS *run*
  parallelism (`--workers N`) is unaffected.

## Tooling — sandbox

- **Avoid `dangerouslyDisableSandbox: true` whenever possible.** Prefer
  sandboxed Bash commands. Only disable when there is no alternative — e.g.
  real-GPU e2e runs, or git writes when `.git` is not on the sandbox
  write-allowlist (prefer allowlisting `.git` via `/sandbox` so git runs
  sandboxed).
