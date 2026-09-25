# P-8a — Tracked render-bundle encoders / query sets; fix the `draw.spec.cpp` bundle-encoder leak

> Found by the 2026-09-26 worker-memory investigation (see
> [`runtime-memory-perf-plan.md`](runtime-memory-perf-plan.md) Progress). See
> [`reference/workflow.md`](reference/workflow.md).

## Goal

The GPU fixture can create and automatically release render-bundle encoders and query sets, and
`draw.spec.cpp` no longer leaks one render-bundle encoder (plus the buffers and pipeline it recorded)
per subcase.

## Background (measured, 2026-09-26)

`webgpu:api,validation,encoding,cmds,render,draw:vertex_buffer_OOB:*` (15,120 subcases) grows
linearly to ~6.1 GB on yawgpu/Metal and ~5.5 GB on yawgpu/Vulkan (MoltenVK); Dawn ~0.33 GB max RSS.
`makeContext()` in `src/webgpu/api/validation/encoding/cmds/render/draw.spec.cpp` creates
`ctx.bundle = wgpuDeviceCreateRenderBundleEncoder(...)` and never releases it; the encoder keeps
every buffer/pipeline it recorded alive. `GpuTest` has `*Tracked` helpers for most object types
(released in `GpuTest::finalize()`, `src/common/harness.cpp`) but none for render-bundle encoders,
render bundles, or query sets.

## Scope

**In:**
- `include/cts/gpu.h` / `src/common/harness.cpp` — new `GpuTest` members, following the existing
  `*Tracked` pattern (vector member + release in `finalize()` + clear):
  ```cpp
  /// Creates a render-bundle encoder and tracks it for release.
  WGPURenderBundleEncoder createRenderBundleEncoderTracked(const WGPURenderBundleEncoderDescriptor& desc);
  /// Finishes `encoder` into a render bundle and tracks the bundle for release (may be an error bundle).
  WGPURenderBundle finishRenderBundleTracked(WGPURenderBundleEncoder encoder,
                                             const WGPURenderBundleDescriptor* desc = nullptr);
  /// Creates a query set and tracks it for release.
  WGPUQuerySet createQuerySetTracked(const WGPUQuerySetDescriptor& desc);
  ```
  Release order in `finalize()`: render bundles and render-bundle encoders together with the other
  encoders (before command encoders/command buffers are released is fine; they are independent),
  query sets alongside the other resources. Null results are not tracked. A handle the caller also
  releases explicitly would double-release — callers must not do both.
- `src/webgpu/api/validation/encoding/cmds/render/draw.spec.cpp`: `makeContext()` uses
  `createRenderBundleEncoderTracked`; `finishContext()` uses `finishRenderBundleTracked` (drop its
  explicit `wgpuRenderBundleRelease`). The test at the `firstBundle`/`secondBundle` site already
  releases correctly — leave it or convert it consistently (no double release).
- Behavior of every test is otherwise unchanged (same calls, same validation expectations).

**Out (non-goals):**
- Auditing other spec files for untracked objects (P-8b, next task).
- Any yawgpu-side deferred-release behavior.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeeds with no warnings; MSVC
      `/W4 /WX` hygiene (no `__builtin_*`, no variable named `g`, no shadowing, no braced-init
      macro in a ternary).
- [ ] `build-yawgpu/cts_unittests` exits 0.
- [ ] `--list-cases 'webgpu:*'` byte-identical to before.
- [ ] (Claude, GPU) `draw.spec.cpp` file on yawgpu and Dawn: same pass/fail/skip as before;
      `vertex_buffer_OOB` max RSS on yawgpu drops from ~6.1 GB to the Dawn range (< 0.5 GB).
- [ ] Changes confined to `include/cts/gpu.h`, `src/common/harness.cpp`,
      `src/webgpu/api/validation/encoding/cmds/render/draw.spec.cpp`.
