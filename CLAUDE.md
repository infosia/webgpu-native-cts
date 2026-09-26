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

- **Compile in parallel: `-j 8`** on `cmake --build` and `cargo build`. The
  former `-j 1` rule was written for a weaker dev machine and no longer
  applies. (A full `cts` build — 642 spec TUs — takes ~3 min at `-j 8`.)
- **Memory is the constraint at *run* time, not build time.** A `cts` run can
  grow to many GB per process, and `--workers N` multiplies it by N; two
  whole-machine freezes have been caused this way. Cap every exploratory or
  full-area run so a runaway kills the process, not the host:

  ```bash
  systemd-run --user --scope -q -p MemoryMax=8G -p MemorySwapMax=0 --collect \
    /usr/bin/time -f 'peakRSS=%MKB elapsed=%es' build-yawgpu/cts --workers 1 '<query>'
  ```

  `systemd-run` is Linux-only; this cap applies to the Linux host (29 GB RAM).
  8G, not less: `createBindGroup:buffer,resource_binding_size` allocates
  max-binding-size buffers and legitimately needs >3G on yawgpu/Vulkan (it was
  killed at 3G and passed at 6G). The cap exists to protect the host, not to
  detect leaks — track leaks via peakRSS instead.

  - The scope's cgroup covers the whole process tree, so the cap is the
    **total across all workers**: with `--workers N`, scale `MemoryMax`
    accordingly and keep it well inside the host's RAM. Raise `--workers` only
    after a single-process run of the same query is known to fit well inside
    the host's RAM divided by N.
  - **Exit 143 with empty output means the cap was hit**, not a test failure —
    the whole query's results are lost. Split the query per test to locate the
    heavy one.

## Tooling — sandbox

- **Avoid `dangerouslyDisableSandbox: true` whenever possible.** Prefer
  sandboxed Bash commands. Only disable when there is no alternative — e.g.
  real-GPU e2e runs, or git writes when `.git` is not on the sandbox
  write-allowlist (prefer allowlisting `.git` via `/sandbox` so git runs
  sandboxed).
