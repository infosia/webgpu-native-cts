# H2 — parallel worker execution (`--workers N` + `--shard I/N`)

> Harness performance feature. Runs the suite across N OS **processes** on one machine (~N× faster),
> with output identical to a sequential run. Implementation by the coding agent. See
> [`reference/workflow.md`](reference/workflow.md).

## Goal

Cut wall-clock for the large format-coverage tests (e.g. `image_copy` = 137256 subcases) by running
the case space across **N child processes**, each with its own WebGPU instance/device, and merging
the results in the parent. **Processes, not threads** — the WebGPU device/queue are not
concurrency-safe across backends, and the harness's uncaptured-error routing is a single global
(`g_currentTest`, `src/common/harness.cpp:18`) plus a `ProcessEvents`-polled event loop; both are
single-threaded by design. Process-sharding sidesteps all of that (this is how upstream CTS scales,
and the roadmap already plans it — [`07-roadmap.md`](../docs/07-roadmap.md) "worker/chunk model …
the result model is already designed to merge shards").

## Scope

**In:**
- `--shard I/N` — deterministic case-subset selection (a CI primitive; also reflected in
  `--list-cases`). Cross-platform (it is pure case filtering).
- `--workers N` — **POSIX** orchestrator: fork N children, each running a shard in-process, read
  their result streams concurrently, merge, and print the unified summary (same format + exit code as
  a sequential run). Composes with `--sample-formats` and `--expectations`.
- An internal `--shard-results` machine-stream mode the orchestrator passes to children.
- Crash-resume so one aborting case doesn't lose a whole shard.
- GPU-free unit tests + docs.

**Out (non-goals):**
- **Threads** — rejected (rationale above); do not add `std::thread` to the run loop.
- **Windows `--workers`** — follow-up (H2b). `--shard I/N` itself works on Windows (pure filtering);
  `--workers` on Windows errors with a clear message for now.
- Combining `--workers` with `--isolate` or `--crash-list` — error in v1 (different execution
  models; `--workers` children run in-process). Concurrent per-case isolation is a later step.

## Interfaces

### `include/cts/test.h` — `RunOptions`

```cpp
struct RunOptions {
    ...
    int  workers = 0;        // --workers N      (0/1 = sequential; >=2 = orchestrate)
    int  shardIndex = -1;    // --shard I/N      (I)
    int  shardCount = 0;     // --shard I/N      (N; 0 = no sharding)
    bool shardResults = false; // --shard-results (internal: machine stream, no summary)
    ...
};
```

### `src/common/runtime/main.cpp` — parsing

- `--shard I/N` → parse `"I/N"` into `shardIndex`/`shardCount`; require `N>=1`, `0<=I<N`.
- `--workers N` → `workers = N`; require `N>=1`. (`--workers 0` may map to
  `std::thread::hardware_concurrency()` as "auto"; document it.)
- `--shard-results` → `shardResults = true` (internal; **not** in `printUsage`, **is** forwarded).
- Add `[--workers N]` and `[--shard I/N]` to `printUsage`.
- **Forwarding:** the orchestrator builds each child's argv itself (see below); `--shard-results`
  must be forwardable. Leave `--workers`/`--shard`/`--expectations` OFF the children (the parent owns
  those); DO forward backend/`--sample-formats` (the existing `forwardedArgs`).

### Sharding (deterministic, by case index)

In the case-collection path, after `collectCases(queries)` (or the equivalent ordered case list),
a case at ordered position `idx` belongs to shard `I` of `N` iff `idx % N == I`. Round-robin by case
index (not contiguous blocks) so large/small cases spread evenly. The order is the existing stable
`collectCases` order, so the partition is deterministic and reproducible.

Apply this filter wherever cases are enumerated for a run when `shardCount > 0`:
`--list-cases`, the in-process run path, and the shard child.

### `src/common/runner.cpp` — dispatch in `runQueries`

Add, before the existing `--crash-list`/`--isolate`/default branches:

1. **Orchestrator** — `options.workers >= 2` (POSIX; error if `--isolate`/`--crash-list` also set, or
   on Windows):
   - For `I in [0, workers)`: `fork`+`exec` this executable with argv =
     `{exe, forwardedArgs..., queries..., "--shard", "I/workers", "--shard-results"}` — a pipe per
     child's stdout (mirror `runIsolatedChild`'s POSIX fork/exec/pipe at ~`runner.cpp:490`).
   - Read all child pipes **concurrently** with `poll()`/`select()` (parent-side I/O multiplexing —
     no GPU threads). Parse each `RESULT\t<status>\t<query>\t<message>` line into a `SubcaseResult`,
     tagging it with its shard and the case it belongs to. Track, per shard, which cases have
     reported.
   - **Crash-resume:** if a child exits abnormally (`WIFSIGNALED` / non-zero) before finishing its
     shard, mark the **first unreported** case of that shard as `Crash` ("shard worker aborted"), then
     re-spawn a child to run that shard's **remaining** cases (pass `--shard I/N --shard-from K` with
     `K` = the next case position; see below) so one crasher doesn't lose the rest. (A clean backend
     never hits this; it makes `--workers` robust on aborting backends too.)
   - After all children finish: merge all `SubcaseResult`s, **re-ordered into the original
     `collectCases` case order** (within a case, the child's emission order), then
     `printRunResults(merged, expectations)` — so stdout + summary + exit code are **identical to a
     sequential run** for the same query and `--expectations`.
2. **Shard child (machine stream)** — `shardCount > 0 && shardResults`:
   run the in-process path over shard `I`'s cases and, for each subcase, immediately print
   `RESULT\t<status>\t<query>\t<message>` (newlines in `message` → spaces; **flush per line** so the
   parent sees live progress and has partial results if the child later aborts). No human summary.
3. **Shard standalone** — `shardCount > 0 && !shardResults` (e.g. CI: `cts --shard 1/4 'query'`):
   run the in-process path over shard `I`'s cases with normal human output + summary + exit code
   (so each CI machine runs and judges its own shard).

Add a `--shard-from K` (internal) to resume a shard at case position `K` (used only by crash-resume).

### `--list-cases --shard I/N`

`--list-cases` honours `shardCount`: print only shard `I`'s case queries. (For partition checks.)

### Compatibility

- `--workers` ⊕ {`--isolate`, `--crash-list`}: error with a clear message.
- `--workers` on Windows: error ("`--workers` is POSIX-only for now; use `--shard I/N` across
  processes/CI"). `--shard I/N` and `--list-cases --shard` work on all platforms.
- `--workers 1` ≡ sequential (no children). `--sample-formats` composes (forwarded to children).

## Acceptance criteria

GPU-free (coding agent — **no GPU runs**):

- [ ] `cmake --build build-yawgpu --target cts cts_unittests gen_listings` succeeds (+ wgpu/dawn).
- [ ] `build-yawgpu/cts_unittests` exits 0, with new tests:
  - **partition**: for `N ∈ {1,3,8}` over a sample query's case list, shards `0..N-1` are pairwise
    disjoint and their union equals the full case set; the assignment is `idx % N` and deterministic.
  - **RESULT-line parse**: a `RESULT\t<status>\t<query>\t<message>` line round-trips to the right
    `SubcaseResult` (incl. a message containing spaces; tab/newline sanitized).
- [ ] Partition holds end-to-end via listing:
      `build-yawgpu/cts --list-cases 'webgpu:api,operation,command_buffer,image_copy:undefined_params:*'`
      = M lines; the eight `--list-cases --shard I/8 '<same>'` (I=0..7) outputs are disjoint and their
      union (sorted) equals the M lines.
- [ ] `build-yawgpu/cts --help` lists `--workers` and `--shard` (not `--shard-results`).
- [ ] `build-yawgpu/cts --workers 4 --isolate 'webgpu:…:*'` exits non-zero with a clear
      "incompatible" message; same for `--workers` + `--crash-list`.
- [ ] No file under `expectations/` is modified.

Claude verifies on real GPU (sandbox off):
- `cts --workers 8 --sample-formats '<image_copy:* query>'` on yawgpu yields the **same** `summary:`
  line (pass/skip/fail/crash) as the sequential run and the same per-case result set after re-order,
  at a fraction of the wall-clock; F-031's depth fails still merge in correctly.
- Spot-check crash-resume on wgpu-native (a shard containing `clearBuffer:clear` `size=0`, F-002).

## Verification

1. Build `cts cts_unittests gen_listings` (codex: build + unittests + listing; no GPU).
2. `cts_unittests` → 0; the `--list-cases --shard` partition checks; the `--help`/incompatibility
   checks; `git status --porcelain expectations/` empty.

## References

- `src/common/runner.cpp` — `collectCases` (~192), `runCase` (~218), `runIsolatedChild` POSIX
  fork/exec/pipe (~490), `runSingleCase` (~294), `runQueries` dispatch (~671), `printRunResults`
  (~609), `parseResultLine` (RESULT-line parsing), `--list-cases` loop (~699).
- `src/common/runtime/main.cpp` — flag parsing (~131), `printUsage` (~65), `forwardedArgs`.
- `include/cts/test.h` — `RunOptions` (~252), `SubcaseResult`.
- `src/common/harness.cpp` — `g_currentTest` (18) + uncaptured-error routing (67): the single-thread
  coupling that makes process-sharding (not threads) the right model.
- `docs/07-roadmap.md` — Cross-cutting "Crash isolation" / "Performance: case-level parallelism
  (worker processes) … result model is already designed to merge shards".
- `docs/06-build-and-run.md` §4 — CLI options table to extend.
- `specs/format-sampling-mode.md` (H1) — composes with `--sample-formats`.
