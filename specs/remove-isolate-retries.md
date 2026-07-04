# remove-isolate-retries — drop the `--retries` isolation retry mode

## Problem

`--retries N` (phaseH3b) re-runs a Fail/Crash case in a fresh child process up
to N extra times under `--isolate`, reporting fail-then-pass as `flaky` (summary
column `flaky=`, JSONL `"attempts"`/`"flaky"` fields). At today's suite size it
is dead weight:

- yawgpu and Dawn are crash-free across the whole suite (2026-07-03 sweeps:
  ~2M subcases each, `crash=0`), so there is nothing to retry.
- wgpu-native's crashes are deterministic backend aborts, not flakes — a retry
  reproduces the same abort and just multiplies the already-slow `--isolate`
  wall-clock.
- It is opt-in (default 0) and nothing in the documented workflows passes it.

User decision: remove the mode entirely.

## Goal

`--retries` no longer exists: unknown option at the CLI, no retry machinery in
the runner, no `flaky`/`attempts` in any output.

## Scope

**In:**
- `src/common/runtime/main.cpp`: remove the `--retries` parse branch, its
  mention in the `Usage:` string, and the `printUsage` note line
  ("--retries accepts a non-negative integer..."). Remove the
  "`--retries` requires `--isolate`" warning in `runner.cpp` (~line 2249) and
  any `runOptions.retries = 0;` resets.
- `include/cts/test.h`: remove `RunOptions::retries`; remove `RetryOutcome` /
  `chooseRetryOutcome` declarations if declared there (they are exported —
  `cts_unittests` calls `cts::chooseRetryOutcome`).
- `src/common/runner.cpp`: remove `RetryOutcome`, `chooseRetryOutcome`,
  `shouldRetryIsolatedAttempt`, `runIsolatedChildWithRetries` (callers use
  `runIsolatedChild` directly), `finalizeRetryResult`, `isFlakyResult`.
  In `collectIsolatedParallelRuns`, collapse the retry bookkeeping: where
  `attemptsByCase[i]` could only ever hold one attempt with retries gone,
  store the single result directly (keep the "isolated worker produced no
  result" fallback). Keep everything else about the isolation pool
  (timeouts, `--emit-crash-list`, merge order) byte-identical in behavior.
- Output format: drop the `flaky=` column from the text summary line and the
  `"attempts"` / `"flaky"` fields from `--output` JSONL (and the JSON summary
  object's `"flaky"` count). Remove `SubcaseResult::attempts` if it exists
  solely for this.
- `src/unittests/main.cpp`: delete the `chooseRetryOutcome` test block
  (~lines 849–869) and fix the JSONL assertions (~lines 880–900) that expect
  `"attempts"`/flaky fields; adjust any summary-line assertions that expect
  `flaky=`.
- Docs: `docs/06-build-and-run.md` has no `--retries` table row; scan it (and
  `docs/03-*` harness docs if they mention retries/flaky) and remove any
  remaining mention of the retry mode / `flaky` status.

**Out (non-goals):**
- No other flag removals (the dead `--yawgpu-backend` / `--future-timeout-ms`
  flags and the docs phantom rows are a separate task).
- No behavior change to `--isolate`, `--case-timeout-ms`, `--crash-list`,
  `--emit-crash-list`, or worker-mode crash containment/respawn.
- `--baseline` must still read old `--output` JSONL files that contain the
  now-removed fields (it keys on query/status; unknown fields are ignored by
  the line parser — verify, don't restructure).

## Acceptance criteria

- [ ] `grep -rn "retries\|RetryOutcome\|chooseRetryOutcome\|isFlakyResult" src include` → no hits
      (except unrelated words if any).
- [ ] `cts --retries 2 <query>` exits non-zero with `unknown option: --retries`.
- [ ] Text summary line no longer contains `flaky=`; JSONL lines no longer
      contain `"attempts"`/`"flaky"`.
- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 1` and
      `cmake --build build-dawn --target cts -j 1` succeed; `cts_unittests`
      exits 0.
- [ ] GPU spot-check (Claude): `--isolate --workers 4` on a small query still
      passes and classifies identically to before.

## References

- `specs/phaseH3b-isolation-retry.md` — the feature being removed (historical phase spec, since purged from the repo; name kept for context only).
- `include/cts/test.h:365` (`RunOptions`, `retries` at :371);
  `src/common/runner.cpp` retry helpers ~lines 1279–1310, pool merge ~1393–1408,
  warning ~2249; `src/unittests/main.cpp` ~849–900.
