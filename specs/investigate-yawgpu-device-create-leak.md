# F-135 — yawgpu Vulkan HAL: device-creation resource leak under per-process churn

> **RESOLVED 2026-06-23 — this spec's premise was WRONG and is kept only for the record.** The leak was
> NOT in yawgpu's HAL. Root cause was the CTS harness: `src/common/runner.cpp` did not call
> `fixture->finalize()` on `SkipTestCase`, so limit fixtures leaked their device handles when skipping
> after acquiring a device. Fixed in `src/common/runner.cpp` (commit `4cacc03`); yawgpu was exonerated.
> See `docs/FINDINGS.md` F-135 for the full record. Everything below is the (superseded) original framing.

> Investigation + finding-documentation task. The root-cause **fix is in yawgpu's HAL device/instance
> lifecycle** (the separate yawgpu repo). This CTS-side task pinpoints the trigger, records the finding
> (done: F-135 in `docs/FINDINGS.md`), and decides how the suite carries the affected families so a
> single-process run no longer self-inflicts collateral. Claude plans/specs/runs/commits; the yawgpu HAL
> fix is out-of-repo.

## Goal

Establish, with a minimal reproduction, that yawgpu's native-Vulkan HAL **leaks device-creation resources
when `instance+adapter+device` are created and destroyed repeatedly in one process**, so that after ~150
creations `wgpuRequestDevice` fails (`HAL device creation failed: vulkan`) or the backend access-violates
(`0xC0000005`). Hand the yawgpu team a tight repro; on the CTS side, contain the affected families.

## Background — evidence (2026-06-23, Windows 11, yawgpu native Vulkan; cts.exe `85e7d9b`, yawgpu.dll Jun 21)

Surfaced while verifying the phaseH3 device-recycle (`32c1b34`) on Vulkan.

1. **Whole-area signal.** Full `api,validation` (39,349 cases): single-process recycle@500
   `--workers 8` = `fail=13 crash=9`; `--isolate --workers 8` (true value, per-case process) =
   `fail=5 crash=0`. The 9 crashes (`0xC0000005`) are all in `capability_checks,limits` /
   `state,device_lost` / `encoding,cmds,render,indirect_draw`; isolate shows those areas
   **11,677 records, fail/crash=0** → the 9 crashes are collateral, not genuine.

2. **Tight repro (single file, 700 cases, ≤1k).**
   `webgpu:api,validation,capability_checks,limits,maxStorageBuffersPerShaderStage:*`
   - `CTS_DEVICE_RECYCLE_INTERVAL=0 cts … --workers 1` → **fail=318**, every one
     `requestDevice failed: HAL device creation failed: vulkan`; first failure at result ~#151, then
     success/failure intermixed (leak hovering at the device-creation ceiling, not a hard cliff).
   - `cts … --isolate --workers 1` → **fail=0**.

3. **Layer localized.** The affected tests own their **own** WebGPU objects and churn create/destroy
   every case — they never touch the harness cache device:
   - `capability_checks,limits,*`: `LimitTest` fixture creates `instance→adapter→device(s)` per case
     (`src/webgpu/api/validation/capability_checks/limits/limit_utils.h:330-432`,
     `requestDeviceWithLimits`).
   - `state,device_lost,destroy`: `ctx.instance = createInstance()` + explicit
     `wgpuDeviceDestroy(ctx.device)` (`src/webgpu/api/validation/state/device_lost/destroy.spec.cpp:104,176,532,…`).
   Therefore the phaseH3 harness recycle (which rebuilds only the cached instance/adapter/device) is the
   **wrong layer** and cannot mitigate this — at any `CTS_DEVICE_RECYCLE_INTERVAL`. Per-case `--isolate`
   is the only harness-level containment (fresh process ⇒ no accumulation).

4. **yawgpu-specific.** `HAL device creation failed: vulkan` is yawgpu's own HAL device-creation path.
   wgpu-native uses a different wgpu-core HAL (not shared, unlike the naga findings F-133/F-134). The
   wgpu-native cross-check via this file is **inconclusive** — wgpu-native panics immediately
   (`src\lib.rs:2754: invalid error filter`, exit 127; a separate, unrelated wgpu-native bug) and never
   reaches the churn.

## Tasks

1. **(Optional but decisive) CTS-test-free minimal churn repro.** A standalone loop —
   `for N: createInstance → requestAdapter → requestDevice → release all` — run against yawgpu and
   wgpu-native, reporting the iteration at which `requestDevice` first fails / the process crashes.
   This proves the leak is pure device/instance churn, decoupled from CTS test logic and from the
   wgpu-native `invalid error filter` panic. If built in-repo it is a small new target → hand to the
   coding agent (out of scope for this spec's edits); a throwaway `cl.exe`-linked repro under the
   scratchpad is also acceptable and need not be committed.
2. **Hand-off to yawgpu.** File the leak against the yawgpu repo with the repro from §2/Task 1: the
   HAL leaks on repeated `wgpuRequestDevice`/device-destroy in one process (suspect: VkDevice / VkInstance
   / descriptor-pool / allocator handles not fully freed, or a static/thread-local not reset on teardown).
3. **CTS suite carry.** Decide and document how the suite runs the device-churning families so a
   single-process run is not self-poisoned:
   - default to `--isolate` for findings (already authoritative), **or**
   - a selective `--crash-list` / family list covering `capability_checks,limits,*` and
     `state,device_lost,*` so only those run per-case-isolated while the rest stay single-process.
   Record the chosen carry in `docs/` (and `expectations/` only if a stable xfail is warranted — it is
   NOT, since isolate is clean).
4. **Do NOT tune `CTS_DEVICE_RECYCLE_INTERVAL`** for this — it is the wrong layer and cannot help
   (recorded so the dead-end is not re-tried).

## Acceptance criteria

- [ ] Finding F-135 recorded in `docs/FINDINGS.md` (done).
- [ ] Minimal repro reproduces `HAL device creation failed` (or `0xC0000005`) for yawgpu within ≤700
      single-process device creations, and `fail=0` under `--isolate` — re-verified on the Windows host.
- [ ] yawgpu-repo issue filed with the repro (out-of-repo; link recorded here when available).
- [ ] Suite-carry decision documented; affected families run isolated in the standard sweep recipe.
- [ ] No `expectations/` xfail added (isolate is clean); no spec/listing churn beyond this file + FINDINGS.

## Notes / references

- Logs (this session, scratchpad): `repro_1_singleproc.log` (yawgpu single-proc fail=318),
  `repro_2_isolate.log` (yawgpu isolate fail=0), `repro_wgpunative.log` (wgpu-native panic),
  `full_A_recycle.log` / `full_B_isolate.log` (whole-area recycle vs isolate).
- Related: F-126 (`specs/investigate-copy-dma-oob-freeze.md`) established **no userspace Vulkan-object
  leak** for `createView`/`api,operation` families on the *cached* device — consistent with this being a
  distinct **device-creation/teardown** leak on the *per-test-owned* device path, not a per-test resource leak.
- Harness: `src/common/harness.cpp` phaseH3 recycle (`32c1b34`) — cache-only; `setCurrentTest` recycle hook.
- Run granularity gotcha: plain `--workers` reports per-SUBCASE, `--isolate` per-CASE; compare fail/crash
  classes + per-area isolate cleanliness, not raw pass totals.
