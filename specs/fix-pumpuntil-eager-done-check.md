# fix-pumpuntil-eager-done-check — don't sleep after the wait condition is already met

## Goal

Make `pumpUntil` (`src/common/webgpu/sync.cpp`) return as soon as `done()` becomes true,
instead of paying one unconditional 1 ms sleep per call even when the callback completed
synchronously.

## Background

`webgpu:api,validation,error_scope:current_scope:*` (2 cases) times out on every backend
(the 2026-07-04 sweep's only 2 "crashes"). Investigation on the yawgpu side (see yawgpu
`specs/tracking/cts-full-sweep-0704-native-vulkan.md`, Finding 3a) re-attributed the hang
to this harness:

- yawgpu's `wgpuDevicePopErrorScope` completes the future synchronously; the callback
  fires on the first `wgpuInstanceProcessEvents` inside the same `popErrorScopeSync` call.
- `pumpUntil`'s loop body is `processEvents(); sleep(1ms);` — the `done()` re-check only
  happens at the top of the next iteration, so every already-complete wait still costs
  ~1 ms.
- `current_scope` is the only error_scope test with `stackDepth=100000`: ~100,001
  sequential `popErrorScopeSync` calls × ~1 ms ≈ 100 s ≫ the 20-30 s case timeout →
  reported as a hang/crash.

This affects every sync wrapper in the file (`requestAdapterSync`, `requestDeviceSync`,
`popErrorScopeSync`, `bufferMapSync`, `processEventsUntil`) and any hot loop above them —
the fix is a general latency win, not just for error_scope.

## Scope

**In:**
- In `pumpUntil` (src/common/webgpu/sync.cpp:92-100): after `wgpuInstanceProcessEvents`,
  re-check `done()` and exit the loop without sleeping when it is satisfied. Equivalent
  shapes are fine (e.g. `if (done()) break;` before the sleep, or restructuring to a
  `for(;;)` with explicit checks) as long as:
  - `done()` true on entry still returns true without calling processEvents or sleeping
    (preserve current behavior).
  - The timeout path still returns `done()`'s final value after the deadline.
  - The 1 ms sleep still happens between polls when the condition is NOT yet met (do not
    busy-spin).

**Out (non-goals):**
- No signature/header changes (`sync.h` untouched).
- No changes to the callers, callback modes, or the 5 s internal timeouts.
- No adaptive/backoff polling scheme — keep the flat 1 ms interval.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu-release --target cts -j 1` succeeds (serial build — hard rule).
- [ ] `build-yawgpu-release/cts_unittests` still exits 0 (rebuild with `-j 1` if present).
- [ ] Diff touches only `src/common/webgpu/sync.cpp`.
- [ ] Claude-run (not the coding agent): on native ANV,
      `cts --isolate --workers 1 --case-timeout-ms 20000 'webgpu:api,validation,error_scope:*'`
      goes from `pass=15 crash=2` to `pass=17 crash=0` (the 2 `current_scope` cases now
      complete in well under the timeout).

## Verification

Coding agent: build serially, run `cts_unittests`, do NOT run GPU CTS. Claude verifies the
error_scope run afterwards.

## Verification result (2026-07-04, post-implementation)

All acceptance criteria met. Native ANV, `--isolate --workers 1 --case-timeout-ms 20000`,
`webgpu:api,validation,error_scope:*`: **pass=17 crash=0** (was pass=15 crash=2), whole
file completes in ~1.9 s wall clock (each `current_scope` case previously burned the full
20 s timeout). Serial build and `cts_unittests` exit 0 confirmed by the coding agent;
diff is 3 added lines in `pumpUntil` only. This closes finding 3a of yawgpu's
`specs/tracking/cts-full-sweep-0704-native-vulkan.md` (harness-side re-attribution
confirmed correct — no yawgpu change was needed).
