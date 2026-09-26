# P-11 — All-features device: don't lose every optional feature when one can't be granted

> Found while triaging F-154 (`docs/FINDINGS.md`). See [`reference/workflow.md`](reference/workflow.md).

## Goal

`getAllFeaturesMaxLimitsDevice()` (`src/common/harness.cpp`) obtains a device with every
*requestable* adapter feature, instead of silently dropping to a texture-only feature set when a
single advertised feature can't be granted; and when any fallback happens, it says so on stderr.

## Background (verified 2026-09-26 with a standalone probe against wgpu-native `9176708`)

- wgpu-native's adapter advertises 41 features, including `WGPUNativeFeature_Immediates` and
  `maxImmediateSize = 4096`, but also `WGPUNativeFeature_RayQuery` (`0x0003001C`) and
  `WGPUNativeFeature_CooperativeMatrix` (`0x0003003B`), which map to wgpu's
  `EXPERIMENTAL_*` features. wgpu-native hard-codes `experimental_features: disabled()`
  (`src/conv.rs:483`), so requesting them always fails: `Some experimental features,
  EXPERIMENTAL_RAY_QUERY | EXPERIMENTAL_COOPERATIVE_MATRIX, were requested, but experimental features
  are not enabled`.
- The harness requests all advertised features; on failure it retries with only 8 texture features
  (`kTextureFeatures`), keeps max limits, sets `allFeaturesDeviceUsedFallback` and reports nothing.
  On wgpu-native **every** `AllFeaturesMaxLimitsGpuTest` has therefore run on a texture-only device
  with max limits — e.g. immediates tests don't skip (limit is 4096) but shaders fail with
  `Capability Capabilities(IMMEDIATES) is not supported`.
- Requesting the 39 features left after removing those two succeeds.

## Scope

**In:**
- Request sequence in `getAllFeaturesMaxLimitsDevice()`:
  1. All advertised features (unchanged).
  2. **New:** if (1) fails, retry with all advertised features minus a backend-specific
     "advertised but never requestable" set. For `CTS_BACKEND_WGPU` that set is
     `WGPUNativeFeature_RayQuery` and `WGPUNativeFeature_CooperativeMatrix` (include `<wgpu.h>`
     under `CTS_BACKEND_WGPU` as `harness.cpp` already does since `d676144`); for other backends
     the set is empty and step 2 is skipped.
  3. The existing texture-only fallback, unchanged, as the last resort.
- Whenever step 2 or 3 is used, print **one** line to `std::cerr` per device creation, e.g.
  `harness: all-features device request failed (<message first line>); retried with <N> features`
  (wording free, but it must name which step succeeded and the feature count). Keep
  `allFeaturesDeviceUsedFallback = true` only for step 3 (the texture-only fallback), as today.
- A comment on the wgpu-native exclusion set pointing at F-154.

**Out:** changing limits handling; the plain `device()` (non-all-features) path; any test code.

## Acceptance criteria

- [ ] `cmake --build build-{wgpu,yawgpu} --target cts cts_unittests -j 8` succeed with no warnings.
- [ ] `build-yawgpu/cts_unittests` exits 0; yawgpu `--list-cases 'webgpu:*'` byte-identical.
- [ ] MSVC `/W4 /WX` hygiene.
- [ ] (Claude, GPU) yawgpu and Dawn: `webgpu:api,*` JSON identical to before and no fallback line on
      stderr. wgpu-native: no fallback line appears; immediates files re-run and F-154 updated.
- [ ] Changes confined to `src/common/harness.cpp`.
