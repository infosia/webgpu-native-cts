# P-4b — Release device-scoped caches at spec-file boundaries

> Cache-policy half of step 3 in [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md),
> decided from the P-4a numbers ([`pipeline-cache-stats.md`](pipeline-cache-stats.md)).

## Goal

When the runner moves from one spec file's cases to another file's, it destroys all device-scoped
objects (the texture-builtin pipeline caches) without tearing down the device, so a long process no
longer retains every earlier file's pipelines — with no change to results.

## Rationale (measured, yawgpu/Metal, `--sample-formats`, 15 texture-builtin files)

- Cross-file reuse is zero: the per-file miss totals equal the combined-run misses for every cache
  (keys embed the builtin's WGSL). Clearing at a file boundary therefore adds no recompiles.
- In-file hit rates are very high (e.g. sampling 115,020 hits / 398 misses), so caching within a
  file stays.
- One process over the 15 files grows ~250 MB → ~900 MB physical footprint.

## Scope

**In:**
- `include/cts/gpu.h` / `src/common/harness.cpp`: new public
  `void releaseDeviceScopedObjects();` — destroys all device-scoped objects (same order as today's
  `clearDeviceScopedObjects()`), leaving devices, adapters, instance, and the device-lost flag
  untouched. Reuse the existing `DeviceCache::clearDeviceScopedObjects()`.
- `src/common/runner.cpp` `runCase()`: remember the file of the previous case run in this process
  (file-local state); when a case's `file` differs from the previous non-empty one, call
  `releaseDeviceScopedObjects()` **before** running it. The first case in a process does not
  trigger a release. This covers every execution path that goes through `runCase()` (sequential,
  shard workers, isolate, crash-list).
- GPU-free unit test: via `deviceScoped<T>()` counting construct/destruct — after
  `releaseDeviceScopedObjects()` the object is destroyed and the next `deviceScoped<T>()` creates a
  new one. (Testing the `runCase()` trigger itself is not required.)

**Out (non-goals):**
- Capacity bounds / LRU.
- Device recycling policy (unchanged).
- Anything keyed by test rather than file.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeeds with no warnings; MSVC
      `/W4 /WX` hygiene.
- [ ] `build-yawgpu/cts_unittests` exits 0 with the new test.
- [ ] Changes confined to `include/cts/gpu.h`, `src/common/harness.cpp`, `src/common/runner.cpp`,
      `src/unittests/main.cpp`.
- [ ] (Claude, GPU) all 15 texture-builtin files in one sequential `--sample-formats` process on
      yawgpu: JSON identical to the pre-change binary; with `CTS_CACHE_STATS=1` the summed misses per
      cache equal the P-4a combined run (no extra compiles); physical footprint sampled over the run
      is recorded and compared with the ~900 MB pre-change peak.
- [ ] (Claude, GPU) buffers + textureSample `--workers 6` JSON identical to pre-change.

## References

- [`device-scoped-pipeline-caches.md`](device-scoped-pipeline-caches.md), [`pipeline-cache-stats.md`](pipeline-cache-stats.md).
