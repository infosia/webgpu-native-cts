# P-5 — Fewer copies in param expansion; drop per-case param data once consumed

> Step 4 of [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md) (the localized part;
> incremental/lazy case generation stays out). See [`reference/workflow.md`](reference/workflow.md).

## Goal

Case enumeration moves instead of copying parameter records, and processes that keep the case list
for the whole run (the `--workers` parent, the sequential runner) stop retaining each case's params
and subcases once they are no longer needed — with byte-identical case order, queries, and results.

## Background (measured, yawgpu build, 2026-09-25)

- `--list-cases 'webgpu:*'`: 267,868 cases, ~1.3 s, ~813 MiB max RSS.
- `--workers 6 'webgpu:*'` parent: ~550–630 MB physical footprint for the whole run, although after
  writing the per-shard plans it only needs each case's `query` and expected result count.
- Copies today: `ParamsBuilder::expand()` filters copy every kept record and combine/expand never
  reserve; `sampleFormatsInCases()` copies kept cases/subcases out of a by-value input;
  `collectCases()` copies `c.params`/`c.subcases` out of the (then discarded) expansion.

## Scope

**In:**
1. `src/common/params.cpp` `ParamsBuilder::expand()`:
   - `Filter` ops (case and subcase level): filter in place, preserving order
     (`std::remove_if` + `erase` or equivalent), no record copies.
   - `Combine` / `CombineWithParams`: `reserve()` the known product size; for each source record,
     copy it for all but the last produced value and **move** it into the last one.
   - `Expand`: same move-for-last pattern (size known only after calling the expander per record;
     the expander must still be called exactly once per record, in the same order, with the
     record still intact).
   - Subcase-only extraction: `reserve` and move where the source is no longer used.
   - Operation semantics, value order, `abortOnCaseKeyCollision` checks: unchanged.
2. `src/common/format_sample.cpp` `sampleFormatsInCases()`: move kept cases/subcases out of the
   by-value `cases` instead of copying (the returned-unchanged paths stay as they are). Thresholds,
   hook calls, stats: unchanged.
3. `src/common/runner.cpp` `collectCases()`: take the expansion by non-const value, compute the
   query from `c.params` **before** moving, then move params and subcases into the `CaseRun`.
4. `collectParallelRuns()`: after all per-shard plan files are written, release every
   `CaseRun`'s `params` and `subcases` storage (e.g. swap with empty) while keeping `query`
   and the expected result count. Anything that later reads a case (restart spawn,
   `recordWorkerLine`, `advanceCompletedWorkerCases`, the final merge) must only use `query` and the
   expected count — store the count explicitly (e.g. a parallel `std::vector<size_t>` or a field),
   since `expectedResultCount()` currently derives it from `subcases.size()`. Plans are written
   before the first worker is spawned, i.e. restarts reuse the already-written file.
5. `collectRuns()` (sequential): after `runCase(cases[i])`, release that case's params/subcases.
6. `--list-cases`: unchanged behavior (it benefits from 1–3 automatically).

**Out (non-goals):**
- Lazy/incremental subcase generation, streaming `--list-cases`, changing `CaseRun` layout beyond an
  optional expected-count field, `--isolate` paths (step 5), case-plan format.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeeds with no warnings; MSVC
      `/W4 /WX` hygiene (no `__builtin_*`, no `g`, no shadowing, no braced-init macro in a ternary).
- [ ] `build-yawgpu/cts_unittests` exits 0 (existing params / query / format-sample tests unchanged
      and passing). Add a GPU-free test covering a builder with case-level combine+filter+expand and
      subcase-level combine+filter, asserting the exact expanded order and subcase records.
- [ ] `--list-cases 'webgpu:*'` and `--list 'webgpu:*'` byte-identical to the pre-change binary, with
      and without `--sample-formats`.
- [ ] (Claude) max RSS / wall of `--list-cases 'webgpu:*'` recorded before/after; the after RSS is
      not higher.
- [ ] (Claude, GPU) buffers + textureSample (`--sample-formats`) sequential and `--workers 6`: JSON
      and log identical to the pre-change binary; `--workers 6 'webgpu:*'` parent footprint shortly
      after startup recorded before/after.
- [ ] Changes confined to `src/common/params.cpp`, `src/common/format_sample.cpp`,
      `src/common/runner.cpp`, `src/common/case_plan.h` (only for an optional count field),
      `src/unittests/main.cpp`.

## References

- [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md) — step 4, invariants.
- [`per-shard-case-plans.md`](per-shard-case-plans.md) — plan writing / restart flow.
- [`format-sampling-mode.md`](format-sampling-mode.md) — `--sample-formats` semantics.
