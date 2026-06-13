# Audit-fix — faithful `api/operation/render_pipeline/sample_mask` port (query identity + full matrix)

The audit (REPORT.md) flagged `render_pipeline/sample_mask.spec.cpp` as a non-faithful slice: it registers
6 invented scenario tests (`all_full`, `raster_subset`, `sample_mask_subset`, `frag_mask_subset`,
`and_of_all`, `none`) instead of upstream's two tests, and only covers a `sampleCount=4` / flat-shading
sub-slice of `fragment_output_mask` with no `alpha_to_coverage_mask`. Re-port it faithfully under the
upstream query identity and full param matrix. Pin `b507bd117e53db86f2fb52d0d858d3ae7d684a85`. Upstream:
`../webgpu-cts/src/webgpu/api/operation/render_pipeline/sample_mask.spec.ts` (808 lines).

REPLACE the current file's invented tests with the two upstream tests:

## `fragment_output_mask`

`.params`: `interpolated ∈ {false, true}` × `sampleCount ∈ {1, 4}` × `rasterizationMask` (every value in
`[0, (1<<sampleCount)-1]` — i.e. `0..1` for sampleCount=1, `0..15` for sampleCount=4) ×
`sampleMask ∈ {0, 0b0001, 0b0010, 0b0100, 0b1000, 0b0101, 0b1010, 0b1111, 0b11110}` (9 values; the pipeline
`multisample.mask`) × `fragmentShaderOutputMask` (same 9 values; emitted from the fragment shader via
`@builtin(sample_mask)`). Mirror upstream's exact value lists.

Semantics to reproduce (read upstream `getExpectedData` / the per-sample loop precisely):
- A full-viewport quad is drawn into a `sampleCount`-sample color (`rgba8unorm`) + depth24plus-stencil8
  target at `1×1`. The fragment shader writes a constant color and a `@builtin(sample_mask)` output of
  `fragmentShaderOutputMask`; the pipeline sets `multisample.count=sampleCount`, `multisample.mask=
  sampleMask`. `rasterizationMask` is applied by the test's draw setup (upstream uses it as the coverage of
  the rendered geometry — reproduce its mechanism exactly, NOT an invented approximation).
- The **final per-sample coverage** = `rasterizationMask & sampleMask & fragmentShaderOutputMask`
  (sampleCount=1 collapses appropriately). For each of the `sampleCount` samples, the sample is written iff
  its bit is set in the final mask. Resolve / read each sample's color, depth, and stencil and compare:
  written samples carry the drawn color/depth/stencil; unwritten samples keep the clear values.
- The existing local file already builds the `sampleCount=4` per-sample color+depth+stencil readback — reuse
  that infra and generalize it to `sampleCount ∈ {1,4}` and the `interpolated` (interp vs flat fragment
  entry point) + full mask matrix. Keep the WGSL entry-point naming convention upstream uses
  (`fmain__fragment_output_mask__{interp,flat}`).

## `alpha_to_coverage_mask`

`.params`: `interpolated ∈ {false,true}` × `sampleCount ∈ {4}` × `rasterizationMask` (`0..15`) ×
`alpha1 ∈ {0.0, 0.5, 1.0}`. The pipeline enables `multisample.alphaToCoverageEnabled=true`; the fragment
shader outputs alpha = `alpha1`. Alpha-to-coverage is **implementation-defined** in exactly which samples
it covers, so upstream does NOT assert an exact per-sample mask — it asserts the **monotonic / bounded**
properties upstream checks (read `alpha_to_coverage_mask`'s assertions carefully and reproduce them: e.g.
the number of covered samples is consistent with alpha, alpha=0 covers none, alpha=1 covers all-rasterized,
all covered samples share the same color, and the covered set is a subset of `rasterizationMask`). Do NOT
invent stricter assertions than upstream. If a sub-property is genuinely untestable natively, mark only
that narrow path and explain — but the test name + params must stay faithful.

## Rules

- Query root `api,operation,render_pipeline,sample_mask`; the two upstream test names + exact params.
- Fixture `AllFeaturesMaxLimitsGpuTest` (as now). Feature-gate / runtime-skip nothing that upstream runs
  unconditionally; `depth24plus-stencil8` is core.
- Enums NOT 1-based; fragment output is `vec4f` (rgba8unorm). Per-sample readback via the multisample
  resolve / per-sample-load path the local file already uses.
- MSVC `/W4 /WX`: no `__builtin_*`, no local var named `g` (use `testGroup`), no `WGPU_*_INIT` as a ternary
  operand, no `+` on adjacent C-string literals, no unused lambda captures. English only; keep the
  upstream-path header line (update the "Deferred:" note since nothing is deferred now, or remove it).
- Faithful bodies; no invented scenario tests. This REPLACES the 6 local tests entirely.

## Build / self-check (codex)

- `cmake --build build-dawn --target cts -j8`, `cmake --build build --target cts -j8`,
  `cmake --build build-yawgpu --target cts -j8` clean; `cmake --build build --target gen_listings -j8`;
  `./build/cts --list 'webgpu:api,operation,render_pipeline,sample_mask:*'` shows exactly
  `fragment_output_mask` + `alpha_to_coverage_mask` with the full param matrices.
- Do NOT run GPU; do NOT touch listing.json or docs. The orchestrator runs Dawn/yawgpu/wgpu verification.
- Report: the two tests' registered case/subcase counts, how rasterizationMask + the per-sample expected
  values are computed, the a2c assertions reproduced, and the build + `--list` output.
