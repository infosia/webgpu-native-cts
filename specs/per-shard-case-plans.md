# P-2 — Per-shard case plans with global positions

> Step 2 of [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md). Targets
> `src/common/case_plan.{h,cpp}` and `src/common/runner.cpp` (+ unit tests). See
> [`reference/workflow.md`](reference/workflow.md).

## Goal

Each `--workers` shard worker loads a case-plan file that contains **only its own cases**, each
tagged with its **global** case position, so no worker materializes (or reads) other shards' params,
subcases, or queries — with no change to results, ordering, `--shard-from` restart semantics, or
corrupt-plan detection.

## Background

Today `collectParallelRuns()` writes one plan with every case (`TemporaryCasePlan(cases)`), and every
worker (and every restarted worker) runs `loadCasePlan()` on the whole file, then filters with
`caseSelectedByShard(i, options)` where `i` is the index in the full plan. With N workers the whole
suite's params/subcases/queries are held N+1 times. The parent's `WorkerState::positions` and the
restart argument `--shard-from positions[next]` are **global** indices; they must keep that meaning.

## Scope

**In:**
- **Plan format v2** (`kCasePlanVersion = 2`). Header unchanged except the version. Each entry gains
  a leading `u64` global position, before `file`:
  `position:u64, file:string, name:string, params, subcases`.
- **Writer:** serialize a chosen subset of cases with their global positions:
  `serializeCasePlan(path, cases, positions)` writes, in order, `cases[p]` for each `p` in
  `positions` (a strictly increasing list of indices into `cases`), recording `p` as the entry's
  position. Throw `std::runtime_error` if `positions` is not strictly increasing or any `p >=
  cases.size()`.
- **Reader:** `loadCasePlan(path)` returns entries with their positions. New validation, reported
  through the existing `CasePlanReader::fail()` ("corrupt case-plan file …"): unsupported version
  (v1 files must be rejected with `unsupported version 1`), positions not strictly increasing.
  All existing checks (magic, unknown test, truncation, trailing data) stay.
- **Parent (`collectParallelRuns`)**: compute each shard's `positions` (as today), and for each
  non-empty shard write a separate temporary plan containing only those positions; spawn that
  shard's worker (and any restart of it) with `--case-plan <that shard's file>`. All temp files are
  removed when the run finishes, including on exceptions (RAII; `TemporaryCasePlan` must become
  movable or be held via `std::unique_ptr`). Works on both POSIX and Windows paths.
- **Worker (`collectShardResultRunsFromPlan`)**: iterate the loaded entries and run an entry iff
  `caseSelectedByShard(entry.position, options)` — i.e. `--shard-from` and `--shard I/N` are applied
  to the **global** position exactly as before. Factor the selection into a small pure helper that
  unit tests can call (declare in `common/case_plan.h` or `include/cts/test.h`, matching how
  existing tests reach internals).
- Unit tests (GPU-free) in `src/unittests/main.cpp`:
  - update `testCasePlanRoundTrip` to the new API; round-trip a **subset** (e.g. positions `{0, 2}`
    of 3 cases) and check positions, tests, params, subcases, queries;
  - writer rejects non-increasing / out-of-range positions;
  - reader rejects a hand-built v1 header (`unsupported version 1`) and a v2 file whose positions
    are not strictly increasing;
  - selection helper: with a shard-2-of-3 plan (positions `{2, 5, 8, 11}`), `shardFrom = 0` selects
    all four, `shardFrom = 8` selects `{8, 11}`, `shardFrom = 12` selects none.

**Out (non-goals):**
- Shrinking the **parent's** retained `cases` (it still needs queries and expected result counts);
  parameter-expansion changes (step 4); `--isolate` children (step 5).
- Any change to `--shard-from`/`--shard`/`--case-plan` CLI syntax, the RESULT line protocol, or
  `--shard-results` runs without `--case-plan` (`collectShardResultRuns()` is untouched).
- Streaming/skip-scanning a shared full plan (rejected in favour of per-shard files).

## Interfaces

```cpp
// src/common/case_plan.h
struct PlannedCase {
    size_t position = 0;   // global index in the parent's ordered case list
    CaseRun run;
};

void serializeCasePlan(const std::string& path,
                       const std::vector<CaseRun>& cases,
                       const std::vector<size_t>& positions);
std::vector<PlannedCase> loadCasePlan(const std::string& path);
```

Names may differ only if an existing convention in the file requires it; report any deviation.
Worker invocation stays
`<cts> <forwarded> <queries> --shard I/N --shard-results [--shard-from K] --case-plan <shard file>`.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 1` succeeds with no warnings;
      MSVC `/W4 /WX`-clean code (no `__builtin_*`, no `g`, no shadowing, no braced-init macro in a
      ternary).
- [ ] `build-yawgpu/cts_unittests` exits 0 with the tests listed above.
- [ ] `--list-cases 'webgpu:*'` output byte-identical to the pre-change binary.
- [ ] (Claude, GPU) yawgpu `webgpu:api,operation,buffers,*` and `--sample-formats`
      `webgpu:shader,execution,expression,call,builtin,textureSample:*`, sequential and
      `--workers 6`: `--output` JSON and stdout log byte-identical to the pre-change binary.
- [ ] (Claude) during a `--workers 6` run, each worker's plan file holds only its shard (≈1/6 of the
      single-plan size), and all plan temp files are gone after the run.
- [ ] (Claude) worker RSS shortly after startup is measured before/after on a large query; the
      after number must not be higher.
- [ ] Changes confined to `src/common/case_plan.{h,cpp}`, `src/common/runner.cpp`,
      `include/cts/test.h` (only if needed), `src/unittests/main.cpp`.

## Verification

- Coding agent: build + `cts_unittests` + `--list-cases` (no GPU runs).
- Claude: GPU comparisons with the sandbox disabled; plan-file sizes via a directory listing of the
  temp dir during a run; worker RSS via `ps` sampling.

## References

- [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md) — step 2, invariants.
- [`fix-posix-worker-fork-exec.md`](fix-posix-worker-fork-exec.md) — why workers load a plan.
- [`parallel-workers.md`](parallel-workers.md) — shard / crash-resume protocol.
- `docs/06-build-and-run.md` — `--workers`, internal `--shard-from` / `--case-plan` flags.
