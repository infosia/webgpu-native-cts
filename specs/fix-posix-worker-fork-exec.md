# fix-posix-worker-fork-exec — POSIX `--workers` children must exec (F-146)

## Problem

`--workers N` (N >= 2) mass-fails cases on macOS that are green serially, green under
`--workers 1`, and green when the *same* shard partition is run as two concurrently
launched standalone processes. Every failure is the downstream signature
`uncaptured error: queue submit cannot use an error command buffer`.

Measured (yawgpu/Metal, `webgpu:api,operation,command_buffer,*`):

| run mode | result |
|---|---|
| `--workers 2` (fork workers) | **fail=314**, all "error command buffer" |
| `--shard 0/2` standalone process | pass=85134 fail=0 |
| `--shard 0/2` + `--shard 1/2` standalone, **concurrent** | both fail=0 |

yawgpu bisected independently (yawgpu `490743e`): reproduces on pre-fix yawgpu dylibs
and **identically on the Dawn oracle** (`immediate` 252/252, `sample_mask` ~666,
`swizzle` 6.8k–9.3k nondeterministic, `labels` ~8 under `--workers 6`) — backend-independent.

Root cause: `phaseW3-fork-worker-no-reenum.md` switched POSIX parallel workers from
`fork`+`execv` to **`fork()` without exec** (child runs `runForkedWorkerCases` over
inherited memory) to avoid per-worker re-enumeration. Its safety argument ("the parent
never initializes WebGPU/Metal, so no GPU state is inherited") is insufficient on
macOS: a forked-not-exec'd child inherits the parent's already-initialized ObjC
runtime / dispatch / framework state (Metal.framework is loaded and its static
initializers have run in the parent at dyld time, even though no GPU object was
created). One such child happens to work (`--workers 1` green); two or more doing
concurrent Metal work misbehave — GPU objects go invalid, every subsequent encoder
finishes into an error command buffer. Apple explicitly does not support using
system frameworks between `fork` and `exec`.

The re-enumeration cost that motivated phaseW3 is now solved by a different, proven
mechanism: the **case plan** (`phaseW4-windows-case-plan.md`, commit `e19b37d`).
Windows workers already `CreateProcess` with `--case-plan <file>` and run
`collectShardResultRunsFromPlan` — no re-enumeration, no fork. This task applies the
same strategy to POSIX.

## Goal

POSIX `--workers N` spawns each shard worker via `fork`+`execv` (like the `--isolate`
child at `src/common/runner.cpp:805-832`) with a `--case-plan` file, eliminating the
fork-without-exec GPU breakage while keeping enumeration cost O(1) per run.

## Scope

**In:**
- `src/common/runner.cpp` only.
- `collectParallelRuns`: build the `TemporaryCasePlan` and set
  `workerOptions.casePlanPath` **unconditionally** (remove the `#if defined(_WIN32)`
  guard around lines ~1819–1822).
- `spawnWorker` POSIX branch: in the `pid == 0` child, replace the
  `runForkedWorkerCases(cases, positions, next)` call with an `execv` of
  `options.executablePath` using the argv from `workerArgs(...)` (which already emits
  `--shard I/N --shard-results [--shard-from K] --case-plan <path>` when
  `casePlanPath` is non-empty). Keep the existing pipe/stdout/stderr dup2 wiring.
  On `execv` failure, `_exit(127)`.
- Delete `runForkedWorkerCases` and drop the `[[maybe_unused]]` on `workerArgs`
  (now used on both platforms).
- Between `fork` and `execv` do only async-signal-safe work (dup2/close/execv —
  mirror the isolation-child pattern at runner.cpp:805-832).
- `docs/06-build-and-run.md` `--workers` row: already says "POSIX uses `fork`+`exec`" —
  append that workers load the parent's ordered case plan (no re-enumeration).

**Out (non-goals):**
- No Windows changes (`CreateProcess` path stays byte-identical).
- No change to the RESULT line protocol, shard assignment (`caseBelongsToShard`),
  crash-respawn logic (`finishWorker` + `--shard-from`), or the `--isolate` pool.
- No change to serial (`--workers 1`) or `--crash-list` paths.
- No attempt to diagnose the macOS framework internals further.

## Interfaces

No new public surface. The worker child command line on POSIX becomes identical in
shape to the Windows one:

```
<cts> <forwarded args> <queries> --shard I/N --shard-results [--shard-from K] --case-plan <tmpfile>
```

Parent-side `WorkerState`, pipe pumping, `finishWorker` respawn, and result merging
are unchanged; a respawned worker also execs (it already goes through `spawnWorker`).

## Acceptance criteria

- [ ] `cmake --build build-yawgpu -j 1` (and `build-dawn`, also `-j 1`) succeed; `cts_unittests` exits 0.
- [ ] No `runForkedWorkerCases` symbol remains; POSIX `spawnWorker` child calls `execv`.
- [ ] `TemporaryCasePlan` is created on all platforms in `collectParallelRuns`.
- [ ] Regression probe (run by Claude, real GPU):
      `cts 'webgpu:api,operation,command_buffer,*' --workers 2` on yawgpu/Metal
      reports `fail=0 crash=0` and the same pass/skip totals as the serial run
      (85134+85068 pass / 35 skip).
- [ ] `cts 'webgpu:api,operation,command_buffer,programmable,immediate:*' --workers 6`
      on yawgpu and Dawn: 252 pass / 0 fail.
- [ ] Worker crash containment still works: killing one worker mid-run yields a
      `crash` result for the in-flight case and the respawned worker finishes the
      remainder (verified by the existing behavior on a crashy wgpu-native query, or
      by a manual `kill` of one worker pid during a long run).

## Verification

Build: codex builds both trees + unit tests. GPU verification is run by Claude
(codex must not run GPU binaries):

```
DYLD_LIBRARY_PATH=<yawgpu tint dir> build-yawgpu/cts 'webgpu:api,operation,command_buffer,*' --workers 2
build-dawn/cts 'webgpu:api,operation,command_buffer,programmable,immediate:*' --workers 6
```

Expected: summaries with `fail=0 crash=0`; totals match the serial baseline.

## References

- `specs/phaseW3-fork-worker-no-reenum.md` — the change this partially reverts (its
  "no GPU state inherited" precondition is invalid on macOS).
- `specs/phaseW4-windows-case-plan.md` + commit `e19b37d` — the case-plan mechanism
  this task reuses; proves `--case-plan` + `collectShardResultRunsFromPlan` end-to-end.
- `src/common/runner.cpp:805-832` — the `--isolate` fork+execv child to mirror.
- yawgpu `specs/tracking/shader-compile-cache-block95.md` (yawgpu `490743e`) — the
  cross-backend bisection that identified the harness as the culprit.
- `docs/FINDINGS.md` F-146 — the finding entry recording this defect.
