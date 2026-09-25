# P-7 — `--isolate` children load a single-case plan instead of re-expanding the test

> Step 5 of [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md). Targets
> `src/common/runner.cpp` (+ unit tests). See [`reference/workflow.md`](reference/workflow.md).

## Goal

Every isolated child the parent launches receives the parent's already-selected case as a one-entry
case plan, so it no longer expands every param combination of the case's test just to find one case
— with unchanged results, classification, and the manual `--run-case` behavior.

## Background (measured, yawgpu/Metal, 2026-09-25)

`runSingleCase()` calls `collectCases({query}, …)`, which expands the **whole test** and then picks
one case. A `--run-case` child for a `textureSampleGrad:sampled_3d_coords` case (35,235 cases) takes
~0.122 s wall, of which ~0.065 s is that expansion (a `map:mapAsync,read` child: ~0.048 s total).
Isolated sweeps of large tests therefore spend roughly half their per-child time re-expanding.

## Scope

**In:**
- **Parent side** — every place that launches an isolated child for a case it already holds as a
  `CaseRun` (`collectIsolatedRuns`, `collectIsolatedParallelRuns` / `startQueuedIsolatedChildren`,
  `collectSelectiveRuns`): before starting the child, write a temporary one-entry case plan for that
  case (reuse `TemporaryCasePlan` with `positions = {position}` against the parent's `cases`, or an
  equivalent one-case serialization that yields a valid v2 plan), and add
  `--case-plan <file>` to the child's args after `--run-case <query>`.
  - The temp file is owned by the child's state (e.g. a `std::unique_ptr<TemporaryCasePlan>` in
    `IsolatedChildState`, or held by the caller for `runIsolatedChild`) and is removed once the child
    has been reaped — normal exit, crash, timeout kill, or failed start. No temp files may remain
    after a run.
  - `runIsolatedChild(options, query)` gains the case (or its plan path) as needed; its callers pass
    the `CaseRun`/position they already have.
  - POSIX and Windows paths stay consistent (Windows code is not compiled here — read carefully).
- **Child side** (`runSingleCase`): if `options.casePlanPath` is non-empty, load it with
  `loadCasePlan()`; require exactly one entry whose `run.query == runCaseQuery`, otherwise print
  `RESULT\tfail\tcase plan does not match --run-case` and return 0 (same protocol as the existing
  mismatch line). Run that entry exactly as today (`aggregateCaseResult(query, runCase(...))`), and
  print the same `RESULT` line. With no `--case-plan`, the existing path (including the
  `--sample-formats` fallback) is unchanged — manual `--run-case` keeps working.
- CLI: no new flags; `--case-plan` already exists and is documented as internal. `--run-case` +
  `--case-plan` is the only new combination.
- GPU-free unit test (`src/unittests/main.cpp`): extend/extract what is needed so a test can check
  that the child-side selection accepts a matching one-entry plan and rejects a plan whose single
  entry's query differs (a small pure helper that takes the loaded plan and the query is fine).

**Out (non-goals):**
- The parent's own case expansion; `--workers` shard workers (already plan-based); result protocol;
  timeout semantics; `--emit-crash-list` format.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeeds with no warnings; MSVC
      `/W4 /WX` hygiene (no `__builtin_*`, no `g`, no shadowing, no braced-init macro in a ternary).
- [ ] `build-yawgpu/cts_unittests` exits 0 with the new test.
- [ ] `--list-cases 'webgpu:*'` byte-identical to the pre-change binary.
- [ ] (Claude, GPU) yawgpu, `--isolate` sequential and `--isolate --workers 6`, on
      `webgpu:api,operation,buffers,*` and a `--sample-formats` textureSampleGrad test: `--output`
      JSON identical to the pre-change binary; no `/tmp/cts-case-plan-*` files left afterwards.
- [ ] (Claude, GPU) `--crash-list` run with a list naming a few cases: JSON identical to pre-change.
- [ ] (Claude, GPU) manual `cts --run-case '<case>'` (no plan) still prints the same RESULT line.
- [ ] (Claude) wall time of an isolated textureSampleGrad run recorded before/after.
- [ ] Changes confined to `src/common/runner.cpp`, `src/common/case_plan.{h,cpp}` (only if a helper
      is needed), `include/cts/test.h` (only if needed for the test), `src/unittests/main.cpp`.

## References

- [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md) — step 5.
- [`per-shard-case-plans.md`](per-shard-case-plans.md) — plan format v2, `TemporaryCasePlan`.
- [`remove-isolate-retries.md`](remove-isolate-retries.md) — current isolate protocol.
