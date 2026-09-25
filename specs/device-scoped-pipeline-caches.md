# P-3 — Device-scoped, RAII-owned texture-builtin pipeline caches

> Step 3 of [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md). Targets
> `src/common/harness.cpp`, `include/cts/gpu.h`, and
> `src/webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.cpp`
> (+ unit tests). See [`reference/workflow.md`](reference/workflow.md).

## Goal

The texture-builtin GPU pipeline caches own their handles (released exactly once) and live no
longer than the cached device they were created on — they are destroyed before the harness
releases its devices, both on device-loss recycle and at process exit — with no change to results.

## Background

`texture_utils.cpp` has five function-local static caches keyed by raw `WGPUDevice`:
`samplingPipelineForDevice` (`SamplingPipelineBundle`), `gatherPipelineForDevice`
(`SamplingPipelineBundle`), `metadataPipelineForDevice` (`MetadataPipelineBundle`),
`textureLoadPipelineForDevice` (`TextureLoadPipelineBundle`), `textureStorePipelineForDevice`
(`TextureStorePipelineBundle`), plus the CPU-side `mipMixWeightsForDevice` (`MipMixWeights`).
The bundles are plain structs of handles that are never released, and the maps never drop entries.
`harness.cpp` tears the cached devices down on genuine device loss (`recycleDevicesIfNeeded` →
`teardownDevices`); a recreated device can reuse the old handle value, so the stale entries can
be looked up for the new device (its comment calls this out as a use-after-free hazard). Static
destruction order between these caches and the harness `DeviceCache` is also unspecified.

## Scope

**In:**
- **Harness device-scoped storage** (`include/cts/gpu.h`, `src/common/harness.cpp`):
  ```cpp
  namespace cts {
  /// Base for objects whose lifetime is bound to the harness's cached devices.
  struct DeviceScopedObject {
      virtual ~DeviceScopedObject() = default;
  };
  /// Returns the object stored under `tag`, creating it with `make()` on first use.
  /// All stored objects are destroyed (in reverse creation order) *before* the cached
  /// devices/adapters/instance are released: on device-loss teardown and at exit.
  DeviceScopedObject& deviceScopedObject(
      const void* tag, const std::function<std::unique_ptr<DeviceScopedObject>()>& make);
  /// Typed convenience; one slot per T.
  template <typename T> T& deviceScoped();
  /// Test-only: runs the device teardown path (GPU-free when no device was created).
  void teardownCachedDevicesForTest();
  }
  ```
  The storage lives inside (or is owned by) the harness `DeviceCache`, so `~DeviceCache` and
  `teardownDevices()` both clear it **first**, then release handles as today. `deviceScoped<T>()`
  is an inline template whose tag is the address of a function-local static in the template, and
  `T` must derive from `DeviceScopedObject`.
- **RAII bundles** (`texture_utils.cpp`): the four bundle types become non-copyable, movable
  owners whose destructor releases each non-null handle once (pipelines → pipeline layout → bind
  group layouts → shader module). Move leaves the source with null handles. Adjust the
  `create*Bundle` functions / `emplace` sites so no copy happens (a copy must not compile).
- **Caches** (`texture_utils.cpp`): each of the six caches above becomes a `DeviceScopedObject`
  subclass obtained via `deviceScoped<...>()`; keep the inner keying (by `WGPUDevice`, then by
  shader key) and all existing key strings/lookup logic unchanged. Returned references stay valid
  for the duration of a case (teardown only happens at case start, in `setCurrentTest`).
- Update the comment on `recycleDevicesIfNeeded()` to reflect that device-keyed caches are now
  cleared on teardown (keep the "no periodic recycle" policy itself unchanged).
- GPU-free unit tests (`src/unittests/main.cpp`): a `DeviceScopedObject` subclass counting
  constructions/destructions — `deviceScoped<T>()` returns the same object on repeated calls;
  `teardownCachedDevicesForTest()` destroys it; the next `deviceScoped<T>()` creates a new one;
  two distinct `T`s get distinct slots and are destroyed in reverse creation order.

**Out (non-goals):**
- Cache capacity limits, eviction, or clearing at spec-file boundaries (decide after measurement).
- Any other cache or file (search found no other device-keyed static caches in `src/`).
- Changing when devices are recycled.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 1` succeeds with no warnings;
      MSVC `/W4 /WX` hygiene (no `__builtin_*`, no `g`, no shadowing, no braced-init macro in a
      ternary).
- [ ] `build-yawgpu/cts_unittests` exits 0 with the tests above.
- [ ] No bundle type is copy-constructible/assignable (`static_assert` in `texture_utils.cpp`).
- [ ] (Claude, GPU) all 15 texture-builtin files in one sequential `--sample-formats` process on
      yawgpu: `--output` JSON and log identical to the pre-change binary; process exits 0 (no
      crash in teardown/exit release).
- [ ] (Claude, GPU) the same run on Dawn and wgpu-native exits cleanly with the same pass/fail
      counts as the pre-change binary on that backend (spot check: at least `textureSample`,
      `textureLoad`, `textureStore`, `textureDimensions`).
- [ ] Changes confined to the three files above plus `src/unittests/main.cpp`.

## Verification

- Coding agent: build + `cts_unittests` + `--list-cases` (no GPU runs).
- Claude: GPU comparisons (sandbox disabled), footprint sampling over the combined run for the
  record (not a pass/fail criterion — caches still retain entries while the device lives).

## References

- [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md) — step 3.
- `src/common/harness.cpp` — `DeviceCache`, `teardownDevices`, `recycleDevicesIfNeeded`.
