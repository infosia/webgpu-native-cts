# Spec — Error out when `--shard` is combined with the plain `--workers` runner

## Goal

`--shard I/N` is **silently ignored** by the plain parallel runner:
`collectParallelRuns()` (runner.cpp:1750) enumerates the full query and
partitions across `options.workers` by worker index; it never consults the
outer `options.shardIndex/shardCount`. Verified 2026-07-03: `--shard 0/16` and
`--shard 1/16` of `webgpu:api,validation,*` at `--workers 8` produced
byte-identical 354,993-line logs. A sweep driver batching by `--shard` ran the
full area once per shard (16x waste). Long-standing since `35f196f` (the flag
was born as the internal parent->worker split argument), not a regression.

Silent ignore is the bug. Make it a **hard startup error** instead.

## Where `--shard` DOES work (must keep working — do not touch these paths)

The run-mode dispatch is at runner.cpp:2236-2258. Every branch except one
honors the outer shard via `caseSelectedByShard`:

- single-process sequential (`collectRuns`) — honors shard
- `--isolate` sequential (`collectIsolatedRuns`, runner.cpp:1259) — honors shard
- `--isolate --workers N` per-case pool (`collectIsolatedParallelRuns`,
  runner.cpp:1310/1343) — honors shard
- `--crash-list` selective (`collectSelectiveRuns`, runner.cpp:1363) — honors shard
- `--list`/`--list-cases` (runner.cpp:2213) — honors shard
- internal worker child (`--shard-results`, `collectShardResultRuns[FromPlan]`,
  runner.cpp:1409/1423) — honors shard; children receive `--shard i/N
  --shard-results` from `workerArgs()` (runner.cpp:1467-1469) and never
  `--workers`, so they are unaffected by this change
- **plain `--workers` >= 2 (`collectParallelRuns`, runner.cpp:2255-2256) —
  IGNORES shard. This is the branch to guard.**

Note `--workers 1` (explicit) resolves to the sequential branch where `--shard`
works — so a flag-presence check (`workersSpecified && shard`) would wrongly
reject a working combination. The guard must key on the **dispatch decision**
(the same `workerCount >= 2 && !isolate && crash-list empty && !shardResults`
condition that selects `collectParallelRuns`), not on flag presence alone.

## What to implement

1. In `runQueries()` (runner.cpp), before any GPU/enumeration work (right where
   the dispatch condition is knowable — after `workerCount` is resolved), if the
   plain-parallel branch would be taken (`!options.shardResults`,
   `options.crashListPath.empty()`, `!options.isolate`, `workerCount >= 2`) AND
   an outer shard was requested (`options.shardCount > 0`), print to stderr:

   ```
   --shard is not supported with the plain --workers runner (it would be silently ignored); split the run by query, use --isolate --workers for a sharded per-case pool, or drop --shard
   ```

   and return exit code 1. Prefer failing fast (before `collectCases`
   enumeration) so the error is instant.
2. Update `docs/06-build-and-run.md`:
   - `--workers` row (line ~275): note it does **not** compose with `--shard`
     (hard error) — sharding a parallel run is done by query batching or
     `--isolate --workers`.
   - `--shard I/N` row (line ~276): same note from the other side.
3. Add a unit test if the existing harness-level tests (src/unittests/main.cpp
   has `caseBelongsToShard` / `resolveWorkers` coverage ~line 766/911) can reach
   the guard as a function; if the guard is only reachable through
   `runQueries()`'s dispatch, factor the condition into a small pure helper
   (e.g. `bool shardConflictsWithParallelWorkers(const RunOptions&, int
   workerCount)`) and unit-test that helper instead. Do not spawn processes in
   unit tests.

## Constraints

- Do NOT change `collectParallelRuns` behavior/semantics otherwise (no
  attempt to "make sharding work" in this task).
- Do NOT touch `workerArgs()` or the worker child protocol.
- All currently-working `--shard` combinations listed above must keep working
  unchanged.
- No changes to test sources or `listing.json`.

## Acceptance criteria

1. `cts.exe --workers 8 --shard 0/16 <query>` exits 1 immediately with the
   error message on stderr; no cases run.
2. `cts.exe --workers 1 --shard 0/16 <query>` still runs the shard sequentially.
3. `cts.exe --isolate --workers 4 --shard 0/16 <query>` still runs (per-case
   pool over the shard).
4. `cts.exe --shard 0/16 --list-cases <query>` still lists the shard.
5. Plain `cts.exe --workers 8 <query>` (no `--shard`) is unaffected.
6. Internal worker children (spawned with `--shard i/N --shard-results
   --case-plan …`, without `--workers`) are unaffected — a plain `--workers`
   run completes normally end-to-end.
7. Unit test for the factored guard helper passes; both configs compile
   serially (`cmake --build build-dawn --config Release -j 1`, same for
   build-yawgpu — note build-yawgpu may be deferred by Claude while a sweep is
   holding its cts.exe).
8. Docs updated as described.
