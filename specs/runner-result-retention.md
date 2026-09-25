# P-1 — Stop retaining transmitted worker results; move results; batch worker output per case

> Steps 1 + 6 of [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md). Localized to
> `src/common/runner.cpp` (+ unit tests). See [`reference/workflow.md`](reference/workflow.md).

## Goal

Shard workers stop accumulating results they have already sent to the parent, result merges move
instead of copy, and each worker writes/flushes one case's result lines at once — with no change to
observable results.

## Scope

**In:**
- `collectShardResultRuns()` and `collectShardResultRunsFromPlan()` no longer keep results after
  `emitShardResults()`; they return `void` (the only caller, `runQueries()`, discards the value).
  Each case's `std::vector<SubcaseResult>` is dropped after it is emitted.
- `emitShardResults()`: write all lines of the case, then flush **once** per case (line format
  byte-identical: `RESULT\t<status>\t<query>\t<sanitized message>\n`).
- `collectRuns()` (sequential): move each case's results into the output (e.g.
  `std::make_move_iterator`) instead of copying.
- `collectParallelRuns()` final merge: `reserve()` the total result count, then move from
  `resultsByCase[i]` (move iterators); optionally release each `resultsByCase[i]` after moving.
- `recordWorkerLine()`: move the parsed result into `resultsByCase` (`std::move(*parsed)`), not copy.
- `drainWorkerLines()`: with batched output one read can now contain many lines; avoid the
  quadratic `buffer.erase(0, …)` per line — scan with an offset and erase the consumed prefix once
  per call. Partial trailing line handling must be unchanged.

**Out (non-goals):**
- Sharing query strings through case IDs (separate follow-up).
- Per-shard plan files / changes to `case_plan.{h,cpp}` (step 2).
- Changes to `--isolate` paths, param expansion, GPU caches, or the result line protocol.
- Streaming aggregation of results.

## Interfaces

```cpp
// src/common/runner.cpp (file-local)
void collectShardResultRuns(const RunOptions& options,
                            const std::vector<Query>& queries,
                            FormatSampleStats* stats);
void collectShardResultRunsFromPlan(const RunOptions& options);
void emitShardResults(const std::vector<SubcaseResult>& results); // one flush per call
```

If any of these are declared in a header, update the declaration consistently. No CLI changes.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 1` succeeds with no new warnings
      (code also MSVC `/W4 /WX`-clean: no `__builtin_*`, no shadowing).
- [ ] `build-yawgpu/cts_unittests` exits 0, including a new GPU-free test that feeds
      `drainWorkerLines`-equivalent input (or the function itself if made testable) as: several
      complete lines in one chunk, a line split across two chunks, a CRLF line, and a trailing
      partial line — and checks the recorded results/order. If the function is file-local, expose
      the minimum needed via the existing internal header used by other runner unit tests.
- [ ] `--list-cases 'webgpu:*'` output is byte-identical before/after (no plan change).
- [ ] GPU runs (Claude): for `webgpu:api,operation,buffers,*` and
      `webgpu:shader,execution,expression,call,builtin,textureSample:*` on yawgpu, the sequential and
      `--workers 6` runs produce identical pass/fail/skip/crash counts, and `--json` output is
      identical between pre-change and post-change binaries (ordering included).
- [ ] Worker crash handling unchanged: a case that aborts mid-run is still reported as Crash and
      the worker restarts after it (existing behavior; verified by existing unit tests + a spot run).
- [ ] No functional diff outside `src/common/runner.cpp`, its header (if any), and
      `src/unittests/main.cpp`.

## Verification

- Build + unit tests (coding agent): commands above, serial `-j 1`.
- Claude: GPU comparisons above with the sandbox disabled; optional RSS sampling of a worker
  during a long `--workers 6` run to confirm RSS no longer grows with transmitted results.

## References

- [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md) — steps 1 and 6, invariants.
- [`parallel-workers.md`](parallel-workers.md) — worker protocol and crash-resume design.
