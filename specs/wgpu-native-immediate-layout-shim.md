# P-10 — Map the standard `immediateSize` onto wgpu-native's `WGPUPipelineLayoutExtras`

> Follow-up to [`wgpu-native-setimmediates-shim.md`](wgpu-native-setimmediates-shim.md) and
> F-154 cause 2 (`docs/FINDINGS.md`). See [`reference/workflow.md`](reference/workflow.md).

## Goal

On the wgpu-native backend, a pipeline layout created through the harness with a non-zero standard
`WGPUPipelineLayoutDescriptor.immediateSize` also carries that size in wgpu-native's extension
struct, so wgpu-native allocates the immediate range. This is an API mapping (like the
`SetImmediates` argument reordering), not a behavior workaround. yawgpu and Dawn are unaffected.

## Background

wgpu-native (`9176708`) reads a layout's immediate size only from the chained
`WGPUPipelineLayoutExtras { WGPUChainedStruct chain; uint32_t immediateDataSize; }`
(`sType = WGPUSType_PipelineLayoutExtras`, declared in `ffi/wgpu.h`; see `src/conv.rs:512`) and
ignores the standard `immediateSize` field. Every pipeline whose shader uses `var<immediate>` is
therefore invalid on wgpu-native. All layouts with a non-zero `immediateSize` in the ported tests
are created through `GpuTest::createPipelineLayoutTracked` (`src/common/harness.cpp`).

## Scope

**In:**
- `src/common/harness.cpp` `GpuTest::createPipelineLayoutTracked`: under `CTS_BACKEND_WGPU` only,
  when `desc.immediateSize != 0`, create the layout from a copy of `desc` whose chain has a
  `WGPUPipelineLayoutExtras` prepended (`extras.chain.next = desc.nextInChain`,
  `extras.chain.sType = WGPUSType_PipelineLayoutExtras`, `extras.immediateDataSize =
  desc.immediateSize`; copy.nextInChain = &extras.chain). The standard field stays set. Otherwise
  (other backends, or size 0) the call is exactly as today.
- Include wgpu-native's `wgpu.h` in `harness.cpp` only under `CTS_BACKEND_WGPU` (the include path
  already provides it since `cf2043e`).

**Out:** F-154 causes 1 and 3 (no workaround — they stay reported); other pipeline-layout call sites;
the expectations file.

## Acceptance criteria

- [ ] `cmake --build build-wgpu --target cts cts_unittests -j 8` and
      `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeed with no warnings.
- [ ] `build-yawgpu/cts_unittests` exits 0; yawgpu `--list-cases 'webgpu:*'` byte-identical.
- [ ] MSVC `/W4 /WX` hygiene.
- [ ] (Claude, GPU) yawgpu and Dawn: the immediates files plus `api,validation,createPipelineLayout`
      and `api,validation,pipeline,immediates` JSON identical to before. wgpu-native: re-run the
      immediates files and record the new counts in F-154.
- [ ] Changes confined to `src/common/harness.cpp`.
