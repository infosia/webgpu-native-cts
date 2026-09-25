# P-4a — Opt-in statistics for the texture-builtin pipeline caches

> First half of the step-3 cache-policy follow-up in
> [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md). Diagnostic only; the policy change
> (clearing / capacity bound) is decided from these numbers in a later task.

## Goal

With `CTS_CACHE_STATS=1` in the environment, each device-scoped texture-builtin cache reports its
entry count and hit/miss totals on stderr when it is destroyed; without the variable, behavior and
output are unchanged.

## Background

After P-3 the six caches in `texture_utils.cpp` (`SamplingPipelineCache`, `GatherPipelineCache`,
`MetadataPipelineCache`, `TextureLoadPipelineCache`, `TextureStorePipelineCache`,
`MipMixWeightsCache`) are `DeviceScopedObject`s, destroyed on device teardown and at exit. A single
sequential `--sample-formats` process over the 15 texture-builtin files grows from ~250 MB to ~900 MB
physical footprint. To choose between "clear at spec-file boundaries" and "bounded capacity" we need,
per cache: how many entries accumulate, and how many lookups hit.

## Scope

**In:**
- Each of the six cache structs gets `uint64_t hits`, `uint64_t misses` counters, incremented in its
  lookup function (hit = key found; miss = entry created). Entry count = sum of inner-map sizes at
  destruction time.
- In each cache's destructor, if the environment variable `CTS_CACHE_STATS` is set to a non-empty
  value other than `0`, print exactly one line to `std::cerr`:
  `cache-stats\t<CacheName>\tentries=<n>\thits=<h>\tmisses=<m>`
  where `<CacheName>` is the struct name. Print nothing when both hits and misses are 0.
- Read the variable once (cache the result in a function-local static). Use the same MSVC C4996
  suppression pattern as `src/common/webgpu/backend_yawgpu.cpp` around `std::getenv`.
- No change to stdout, result lines, JSON output, or exit codes.

**Out (non-goals):**
- Any eviction/clearing/capacity policy.
- Stats for anything outside `texture_utils.cpp`.
- Documenting the variable in `docs/` (Claude does that if it is kept).

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeeds with no warnings; MSVC
      `/W4 /WX` hygiene (no `__builtin_*`, no `g`, no shadowing, no braced-init macro in a ternary;
      `std::getenv` wrapped in the C4996 push/pop).
- [ ] `build-yawgpu/cts_unittests` exits 0.
- [ ] Changes confined to `texture_utils.cpp`.
- [ ] (Claude, GPU) without the variable, stdout/JSON of a textureSample run is identical to the
      pre-change binary and stderr has no `cache-stats` lines; with `CTS_CACHE_STATS=1`, stderr has
      one line per used cache.

## References

- [`device-scoped-pipeline-caches.md`](device-scoped-pipeline-caches.md) — the caches (P-3).
