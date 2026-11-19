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

## Scope

**In:** T1-T7 above, with unit tests for T1.

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
- [ ] Apart from T2, T4, T6 and T7.1, `cts --list-cases 'webgpu:*'` is unchanged; REPORT.md lists
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
