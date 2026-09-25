# P-8b — Audit ported tests for untracked WebGPU objects that are never released

> Follow-up to [`tracked-bundle-encoders-queryset.md`](tracked-bundle-encoders-queryset.md) (P-8a).
> See [`reference/workflow.md`](reference/workflow.md).

## Goal

Every WebGPU object a ported test creates is released once the (sub)case finishes — either through a
`GpuTest` `*Tracked` helper or by an explicit release on the normal (non-throwing) path — so no test
accumulates GPU objects across subcases.

## Background

P-8a found `draw.spec.cpp` leaking one render-bundle encoder (plus the buffers and pipeline it
recorded) per subcase: ~6 GB on yawgpu over one test. `src/webgpu/**/*.cpp` still has ~290 raw
creation calls in 62 files (`wgpuDeviceCreate*`, `wgpuCommandEncoderBegin*Pass`,
`wgpuCommandEncoderFinish`, `wgpuRenderBundleEncoderFinish`, `wgpuTextureCreateView`,
`wgpuDeviceGetQueue`). Many are already released correctly; some may not be.

Available tracked helpers on `GpuTest` (`include/cts/gpu.h`): buffers, samplers, textures, texture
views, shader modules, bind-group layouts, bind groups, pipeline layouts, render/compute pipelines,
command encoders, compute/render pass encoders, command buffers (`finishTracked`), render-bundle
encoders, render bundles (`finishRenderBundleTracked`), query sets.

## Scope

**In:**
- Inspect every raw creation call listed above in `src/webgpu/**/*.cpp` (spec files and shared
  helpers such as `texture_utils.cpp`). For each handle, decide:
  1. **Already released** on the normal path (explicit `wgpu*Release`, or ownership handed to an
     RAII owner / device-scoped cache) → leave it.
  2. **Never released** (or released only on some normal paths) → fix, preferring the matching
     `*Tracked` helper; otherwise add the explicit release. Never both (no double release).
  3. **Intentional** — the test deliberately exercises lifetime/release semantics (e.g. a test that
     releases an object early, uses an object after its parent is released, or tests `destroy`)
     → leave the explicit handling exactly as is.
- Do not change any test's API call sequence, parameters, or expectations — only where/how handles
  are released. Moving a release to fixture `finalize()` (via a tracked helper) is fine unless the
  test relies on the object being released earlier (then keep the explicit release).
- `wgpuDeviceGetQueue` returns a new reference: release it (or use `t.queue()` if equivalent).

**Out (non-goals):**
- Leaks on exception/failure paths (a failing subcase may leak; not in scope).
- `src/common/` (harness) changes, new helpers, yawgpu behavior.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeeds with no warnings; MSVC
      `/W4 /WX` hygiene.
- [ ] `build-yawgpu/cts_unittests` exits 0; `--list-cases 'webgpu:*'` byte-identical.
- [ ] `REPORT.md` contains a table of every file touched (call site → classification → fix), plus
      the list of category-3 (intentional) sites left alone with a one-line reason each.
- [ ] (Claude, GPU) `webgpu:api,*` and the texture-builtin/shader execution areas on yawgpu and Dawn:
      pass/fail/skip identical to the pre-change binaries (JSON identical).
- [ ] (Claude) full `webgpu:*` `--workers 6` yawgpu run: worker footprint re-measured and compared
      with the 2026-09-26 baseline (~1.4–1.5 GB per worker).
- [ ] Changes confined to `src/webgpu/**/*.cpp`.
