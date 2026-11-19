# Fix the port defects found by the `8b086b8` test review

> See [`reference/workflow.md`](reference/workflow.md).

## Goal

Ported tests exercise the same inputs and API paths as upstream `gpuweb/cts` `d116bcc`, and the
runner's query selection and `--baseline` comparison behave as documented.

## Background

A review of `8b086b8` compared every ported spec file with upstream `d116bcc`. All 3,084 test names
exist upstream, but some tests enumerate inputs that collapse to identical GPU work, skip the failure
path, or take the synchronous API where upstream takes the asynchronous one. Two runner defects were
also found. Claude confirmed each item below in the source before writing this spec.

`<cts>` below is the local upstream checkout at `d116bcc`.

## Work items

### T1 — Runner

1. **Filter cases before expanding subcases.** `collectCases()` (`src/common/runner.cpp`, near
   `sampledExpandedCases`) expands every case and its subcases, then applies the case filter. Selecting
   one `textureGather:sampled_array_2d_coords` case expands 9,450 cases and 151,200 subcases. Apply
   the case-parameter filter to the case-level parameters first, and expand subcases only for selected
   cases. `--sample-formats` must keep selecting the same cases. Upstream reference:
   `<cts>/src/common/internal/test_group.ts` (around line 415).
2. **Aggregate baseline rows like current-run rows.** `loadBaseline()` keeps the last row per query.
   `mergeCurrentEffective()` keeps the worst one: a bad status is never replaced by a good one. Use the
   same rule in `loadBaseline()`, sharing one helper.
3. Unit tests in `src/unittests/main.cpp`: a baseline JSONL with `fail` then `pass` for one query
   loads as `fail`, and so does `pass` then `fail`. A case query selects exactly that case. If case
   selection is testable without a GPU, assert that the subcase expander runs only for the selected
   case.

### T2 — Image-copy alignment

1. `kColorTextureFormats` (`src/webgpu/texture_format.h`) currently equals `kRegularTextureFormats`.
   Upstream `kColorTextureFormats` is regular **plus** compressed formats
   (`<cts>/src/webgpu/format_info.ts` around line 1594). Make the native constant match. Every user
   should then match upstream:
   - `api/validation/image_copy/texture_related.spec.cpp`
   - `api/validation/image_copy/buffer_texture_copies.spec.cpp`
   - `api/validation/render_pipeline/fragment_state.spec.cpp`
   - `api/operation/command_buffer/image_copy.spec.cpp` (it has a local workaround with the comment
     "Upstream's kColorTextureFormats"; switch it to the shared constant if equivalent)
   - `src/unittests/main.cpp`, whose `== 43` count assertion becomes the new count
   Each test's `.filter`/feature gates must then handle compressed formats as upstream does, for
   example a skip when the format's feature is missing.
2. Port the upstream alignment expanders from `<cts>/src/webgpu/api/validation/image_copy/image_copy.ts`
   (around lines 192-260): `valuesToTestDivisibilityBy(n)` yields `0..2n` and `3n`, and the
   coordinate and offset expanders use alignment 1 for depth/stencil formats. Replace the native
   `valueToCoordinateExpander` (`texture_related.spec.cpp`, around line 108) and any other native
   expander that diverges.
3. After the change, no `origin_alignment` or `size_alignment` subcase repeats another's parameters,
   and the compressed-format subcases include unaligned coordinates.

### T3 — Asynchronous pipeline creation

1. Add harness helpers on `GpuTest` (`include/cts/gpu.h`, `src/common/harness.cpp`) that create a
   render or compute pipeline through `wgpuDeviceCreateRenderPipelineAsync` /
   `wgpuDeviceCreateComputePipelineAsync`, wait for the callback with the harness's existing event
   pumping, and return the pipeline and the `WGPUCreatePipelineAsyncStatus`. Track the returned
   pipeline the same way the `*Tracked` helpers do.
2. Add the equivalent of upstream `doCreateRenderPipelineTest` / `doCreateComputePipelineTest`
   (`<cts>/src/webgpu/api/validation/validation_test_utils.ts` around line 402): for `isAsync=false`,
   the existing `expectValidationError` path; for `isAsync=true`, expect status `Success` when
   creation should succeed and `ValidationError` when it should fail, and no uncaptured validation
   error in either case. Where upstream checks for a different rejection (for example an internal
   error), match upstream.
3. Route every test whose `isAsync`/`async` parameter is ignored today through these helpers. Find
   them with `grep -rl '"isAsync"\|"async"' src/webgpu` (44 files at `8b086b8`) and check each against
   its upstream file. Remove the "isAsync is ignored" comments. `api/operation/labels.spec.cpp` calls
   the sync API for `createComputePipelineAsync` / `createRenderPipelineAsync`; switch it to the async
   helpers.
4. Parameters and case counts stay unchanged.

### T4 — `textureGather` format predicate

`isPossiblyFilterableAsTextureF32()` in
`shader/execution/expression/call/builtin/texture_sampling/texture_utils.cpp` admits depth formats and
rejects `*32float`. Upstream `isTextureFormatPossiblyFilterableAsTextureF32()`
(`<cts>/src/webgpu/format_info.ts` around line 2649) is
`info.color?.type === 'float' || is32Float(format)`. Match it, and keep or add the runtime
`float32-filterable` feature check that upstream `textureGather.spec.ts` (around line 100) performs.
Check every other caller of the native predicate for the same divergence.

### T5 — Undefined behaviour on boundary inputs

1. `numberToString()` (`shader/validation/expression/binary/const_override.h` around line 146) casts a
   double to `long long` before checking its magnitude. Check the magnitude first.
2. The abstract-int `dot` validator (`shader/validation/expression/call/builtin/dot.spec.cpp` around
   line 64) evaluates a signed product after detecting overflow. Stop before the multiplication once
   overflow is known.
3. Search the shader-validation helpers for the same two patterns (float-to-integer casts before a
   range check, signed arithmetic after an overflow check) and fix any other instance.

### T6 — `bgl_visibility_mismatch`

`api/validation/encoding/programmable/pipeline_bind_group_compat.spec.cpp` (around line 523): restore
upstream's parameters (`<cts>/.../pipeline_bind_group_compat.spec.ts` around line 699), including zero
bind-group visibility and the per-encoder pipeline-layout visibility sets (vertex | fragment for render
encoders). Create only the pipeline the encoder type uses. Upstream enumerates 832 subcases; native
must match.

### T7 — Repeated test work

1. **`atan2:values`**: the shared range helper (`shader/validation/expression/call/builtin/
   const_override_builtin.h` around line 425) deduplicates by bit pattern. Upstream deduplicates with a
   JavaScript `Set` (SameValueZero: `+0` and `-0` are one value, and all NaNs are one value). Match it,
   and check that other users of the helper match upstream's counts.
2. **`bitcast` alias cases**: `shader/execution/expression/call/builtin/bitcast.spec.cpp` (around line
   449) generates identical WGSL for `alias=true` and `false`. Port upstream's alias declaration
   (`<cts>/.../bitcast.spec.ts` around line 51).
3. **`requestDevice:stale`**: `api/operation/adapter/requestDevice.spec.cpp` (around line 587) waits
   after each request. Port the two upstream await flags: when a flag is false, issue the next
   `wgpuAdapterRequestDevice` before waiting on the previous one's future, so all ten subcases are
   distinct request sequences.
4. **`createTexture` default and smallest sizes: out of scope.** Upstream varies the length of the
   `size` sequence. `WGPUExtent3D` always has three fields, so the native spellings are the same
   descriptor. Keep the cases for query parity with upstream; Claude records the divergence in
   `docs/UPSTREAM.md`.

### T8 — Texture-sampling parity left open by T4 (batch C)

Found while implementing T4.

1. **Depth formats in the `textureSample*` color tests.** Native drops the five depth formats from 20
   tests (`textureSample`, `textureSampleBias`, `textureSampleGrad`, `textureSampleLevel`:
   `sampled_*` / `arrayed_*` / `lodClamp` variants) with its own `isSampledColorTextureFormatParam`
   filter. Upstream enumerates them with `filt="nearest"` and samples them as `texture_*<f32>` over
   the depth aspect. Remove the native filter so the parameters match upstream, and give
   `executeCase()` the depth initialisation and expected-value path the gather path already has
   (`executeGatherCase()`). Upstream counts per test are in the batch A report (for example
   `textureSample:sampled_2d_coords` 2646 cases / 5292 subcases).
2. **Gather sampler filter.** `executeGatherCase()` always binds a nearest sampler. Gather results do
   not depend on the filter, but upstream binds a sampler with the case's `filt`, which also checks
   that a filtering sampler binds with `*32float` under `float32-filterable`. Bind the requested filter.

### T9 — Per-texel depth data in the texture-sampling helpers (batch D)

Found while reviewing T8. `texture_sampling/texture_utils.cpp` fills every depth texture by clearing
each mip level and layer to one value (`depthValue(mip, layer)` through `initializeDepthTexture()`),
and the software model reads that same constant. Every texel of a subresource is therefore equal, so
no depth sampling path tests texel addressing, wrapping, filtering footprint or gather texel order:
`textureSampleCompare*`, `textureGatherCompare`, `textureGather` and `textureSample*` on depth
formats, and any other user of `initializeDepthTexture()`.

Upstream fills depth textures with random per-texel data
(`createTextureWithRandomDataAndGetTexelsForEachAspect()` in
`<cts>/src/webgpu/shader/execution/expression/call/builtin/texture_utils.ts`, around line 3363):

- the data is random depth32float texels per mip level (`createRandomTexelViewMipmap`, which takes the
  same random-texel options as the color path);
- the texture is filled by rendering those values (`frag_depth`), because depth formats cannot be
  written by copies;
- for formats whose stored depth differs from the generated value (unencodable formats, and
  `depth16unorm` quantization) the expected texels are read back from the GPU after filling, so the
  software model uses the stored values;
- a combined depth-stencil format also gets random stencil texels.

Port this so the software model samples per-texel depth values through the same addressing, wrapping
and filtering code the color path uses. Remove `depthValue()` and the per-subresource clear once
nothing uses them. Parameters and case lists stay unchanged.

### T10 — Port the upstream sample-point generators (batch E)

Found while reviewing T9. Native `generateCalls()` in `texture_sampling/texture_utils.cpp` is a
native design, not a port of upstream's `generateTextureBuiltinInputs{1D,2D,3D}` /
`generateSamplePointsCube` (`<cts>/.../call/builtin/texture_utils.ts`, around lines 4000-4600). The
test parameters match upstream, but the calls each case makes do not:

| Argument | Upstream | Native |
|---|---|---|
| `depthRef` | 0, 0.5 or 1 (`makeRandValue(...)/2`), so `equal`/`not-equal` are tested | 8 fixed values strictly inside (0, 1); `equal` is always false |
| coords | hashed per call from the `hashInputs`, per `method` (`texel-centre`, `spiral`, `cube-edges`, …), including outside [0, 1] for address modes | own spiral / texel-centre formulas |
| `ddx`/`ddy` | `makeGradient(7)` / `makeGradient(8)` in every axis | x only; `ddy` is always 0 |
| bias | `makeBiasAndDerivativeMult` (including the extreme-bias branches) | 6 fixed values |
| offset | `makeIntHashValueRepeatable(-8, 8, i, 3 + j)` per axis | `offsetForCallIndex(i)` |
| component | hashed in [0, numComponents) | check native |
| mip level, sample index, array index | `makeRangeValue(...)` | `i % n` |

Port the upstream generators and their hash helpers (`makeRandValue`, `makeRangeValue`,
`makeIntHashValue*`, `hashU32`, which T9 already ported as `upstreamHashU32()`) so every call argument
equals upstream's for the same parameters, and use them in every texture builtin that upstream drives
with them (textureSample*, textureGather*, textureLoad where applicable). Keep the native gather 0.75
snap only if upstream has the equivalent; otherwise port upstream's handling of footprint
ambiguity. Tolerances and the software model stay; if a test now fails on Dawn because of a model or
tolerance gap that upstream handles, port upstream's handling rather than altering the inputs.

### T11 — Per-texel colour data in cube textures (batch F)

Found while implementing T10. `executeCase()` fills cube and cube-array colour textures with one value
per mip level (`constantForCube` → `makeTextureData(..., constant)` in `texture_utils.cpp`), so colour
cube sampling cannot detect a wrong face, a wrong texel, or wrong wrapping across face edges. Upstream
fills every texture, cubes included, with random texels (`createTextureWithRandomDataAndGetTexels`),
and its software model handles cube faces and edges per texel. Fill cube textures with per-texel data
the same way as the other dimensions, and make the model sample cube faces per texel (T9 already did
this for depth cubes through `wrapFaceCoordToCubeFaceAtEdgeBoundaries`; reuse it). If Dawn then fails
cube-edge cases, compare with upstream's model (`softwareTextureReadMipLevel` / cube corner handling)
before calling it a finding.

### T12 — Compressed formats in `textureLoad:sampled_3d` (batch F)

`textureLoadFormatCompatibleWith3D()` calls the shared `textureFormatAndDimensionPossiblyCompatible`
and then also excludes BC and ASTC formats. Upstream's `sampled_3d` uses only the shared predicate,
so it enumerates BC and ASTC 3D textures (136 records were missing from the T10 comparison). Drop the
extra exclusion and make the textureLoad 3D fill and expected-value path handle compressed 3D
textures (block layout per depth slice, `materializeCompressedTexels` for 3D).

## Scope

**In:** T1-T12 above, with unit tests for T1.

**Out:**
- Removing upstream cases or renaming tests. Query names and parameters stay identical to upstream,
  except where an item restores upstream's own parameters (T2, T6, T7.1).
- `docs/` and `src/webgpu/listing.json` (Claude regenerates the listing and updates docs).
- GPU CTS runs (Claude runs them).

## Acceptance criteria

- [ ] `cmake --build build-dawn --target cts cts_unittests -j 8` succeeds with no new warnings, and
      the yawgpu and wgpu-native builds (`build-yawgpu`, `build-wgpu`) also compile.
- [ ] `build-dawn/cts_unittests` exits 0, including the new T1 tests.
- [ ] T1: `cts --list-cases` output for `webgpu:*` is byte-identical before and after the T1 change
      alone. Listing one `textureGather:sampled_array_2d_coords` case query does not expand the
      other 9,449 cases (shown by unit test or by a measured drop in time/peak RSS, reported in
      `REPORT.md`).
- [ ] T2: `origin_alignment` and `size_alignment` include compressed formats, and no two subcases of
      one case share all parameter values. REPORT.md gives old and new case and subcase counts for
      every test that uses `kColorTextureFormats`, and how each new count was checked against upstream.
- [ ] T3: `grep -rn 'isAsync is ignored\|async.*ignored' src/webgpu` prints nothing, and every test with
      an `isAsync`/`async` parameter calls the async entry point when it is true. Case counts unchanged.
- [ ] T4: the `textureGather` tests enumerate `r32float`, `rg32float`, `rgba32float` and no depth
      formats in the f32-filterable paths; REPORT.md gives old and new subcase counts.
- [ ] T5: the boundary inputs from the review (largest finite f32 in `numberToString()`, an
      overflowing abstract-int `dot`) run under `-fsanitize=undefined` without a report, or a unit
      test covers them.
- [ ] T6: `bgl_visibility_mismatch` enumerates 832 subcases.
- [ ] T7: `atan2:values` subcase count drops by 940; `bitcast` alias cases produce WGSL with an alias
      declaration; `requestDevice:stale` issues overlapping requests when an await flag is false.
- [ ] T8: the 20 `textureSample*` tests enumerate exactly upstream's case and subcase counts, and
      `textureGather` binds a sampler with the case's filter.
- [ ] T9: `depthValue(` no longer appears in `src/webgpu`; within one mip level of a depth texture
      used by these tests at least two texels hold different values (shown by a unit test or an
      assertion in the fill helper); `--list-cases 'webgpu:*'` is unchanged.
- [ ] T10: for a sample of cases per builtin (at least one per `method` and per builtin), the
      generated call arguments equal upstream's, shown by a script comparing against upstream's
      generator run in node; `--list-cases 'webgpu:*'` is unchanged.
- [ ] T11: no cube or cube-array texture in the texture builtin tests is filled with a single value
      per mip level; `--list-cases 'webgpu:*'` is unchanged by T11.
- [ ] T12: `textureLoad:sampled_3d` enumerates exactly upstream's cases and subcases.
- [ ] Apart from T2, T4, T6, T7.1, T8 and T12, `cts --list-cases 'webgpu:*'` is unchanged; REPORT.md lists
      every test whose case list changed.

## Verification

Coding agent: builds, `cts_unittests`, `cts --list-cases` / `--list` diffs. No GPU runs.

Claude: rebuild `build-dawn`, run each changed test's query on Dawn/Metal (sandbox disabled,
`--workers 6`), triage any failure as port bug or Dawn finding, then regenerate `listing.json` and
update `docs/UPSTREAM.md` / `docs/COVERAGE.md`.

## References

- `REPORT-tests.md` (review of `8b086b8`, git-ignored) — the source of these items.
- `docs/UPSTREAM.md` — divergence records.
- `specs/format-sampling-mode.md` — `--sample-formats` behaviour T1 must preserve.
