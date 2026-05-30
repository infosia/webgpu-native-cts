# COVERAGE — Port Status

Tracks how much of the upstream CTS has been ported, per area and per file. Denominators are the
upstream `.spec.ts` file counts at the pinned revision (see [UPSTREAM.md](UPSTREAM.md)).

**Pinned upstream:** gpuweb/cts `b507bd1` (2026-05-28) — 683 `.spec.ts` files under `src/webgpu/`.

> This is a living document. Update it as files are ported (or deliberately skipped). The file
> table starts empty; only the area summary's denominators are pre-filled.

## Status legend

| Status | Meaning |
|--------|---------|
| **ported** | All (or all in-scope) tests in the file are ported and run on at least one backend |
| **partial** | Some tests ported; others skipped/unimplemented (list which) |
| **N/A** | No native analog (e.g. WebIDL `TypeError` coercion tests, web-platform-only); not portable |
| **deferred** | In scope eventually but intentionally postponed (e.g. shader execution) |
| **todo** | Not started (default) |

A test marked `.unimplemented()` in a ported file counts the file as **partial**.

## Area summary

| Area | Upstream files | Ported | Partial | N/A | Deferred | Todo |
|------|---------------:|-------:|--------:|----:|---------:|-----:|
| `api/validation` | 129 | 0 | 9 | 0 | 0 | 120 |
| `api/operation` | 72 | 0 | 0 | 0 | 0 | 72 |
| `shader/validation` | 207 | 0 | 0 | 0 | 207 | 0 |
| `shader/execution` | 239 | 0 | 0 | 0 | 239 | 0 |
| `compat` | 15 | 0 | 0 | 0 | 0 | 15 |
| `web_platform` | 13 | 0 | 0 | 13 | 0 | 0 |
| `idl` | 3 | 0 | 0 | 3 | 0 | 0 |
| other (root/examples/etc.) | 5 | 0 | 0 | 0 | 0 | 5 |
| **Total** | **683** | **0** | **9** | **16** | **446** | **212** |

Notes on the pre-classified rows:

- **`web_platform` (13) → N/A**: canvas, `importExternalTexture` from `HTMLVideoElement`/`VideoFrame`,
  workers — these depend on the web platform and have no native C-API analog. See
  [00-overview](00-overview.md) non-goals.
- **`idl` (3) → N/A**: WebIDL surface/constant tests; the C API has no IDL layer.
- **`shader/validation` (207) + `shader/execution` (239) → deferred**: shader validation is
  mid-term; shader execution is the largest deferred effort. See [07-roadmap](07-roadmap.md)
  phases 5–6. (Some `shader/validation` files may be reclassified to `todo` once that phase
  starts.)
- **`api/regression`, `shader/regression`**: 0 files at this revision.

These classifications are provisional; revisit per file when the area is worked.

## Phase 1 (vertical slice) — done

The slice ported one real file to prove the harness end-to-end on wgpu-native (see
[07-roadmap §Phase 1](07-roadmap.md)):

| Upstream file | Status | Tests ported | Notes |
|---------------|--------|--------------|-------|
| `api/validation/buffer/create.spec.ts` | partial | `limit`, `new_usages` | `size`/`usage`/`createBuffer_invalid_and_oom` unimplemented; uses `GpuTest` instead of `AllFeaturesMaxLimitsGPUTest` (no extra features needed for these two) |

## Full per-file table

Append rows as files are touched. Keep paths relative to upstream `src/webgpu/`. Only list files
that are no longer `todo` (the area summary covers the rest).

| File | Status | Tests ported / total | Skipped / N/A tests | Notes |
|------|--------|----------------------|---------------------|-------|
| `api/validation/buffer/create.spec.ts` | partial | 4 / 5 (`limit`, `new_usages`, `usage`, `createBuffer_invalid_and_oom`) | `size` (shouldThrow RangeError — no C analog) | green on **yawgpu** (all 4: `usage`=78 cases/156 subcases, `c_i_a_o`=8, `limit`=3, `new_usages`=10). On wgpu-native all pass **except `usage`, which aborts wgpu-native** (see [FINDINGS F-001](FINDINGS.md)). Fixture deviation `GpuTest` |
| `api/validation/buffer/destroy.spec.ts` | partial | 3 / 4 (`all_usages`, `twice`, `error_buffer`) | `while_mapped` (needs mapAsync map/unmap) | green on wgpu-native, **yawgpu & Dawn** (`all_usages`=10, `twice`=6, `error_buffer`=1; error buffers crash no backend); fixture deviation `GpuTest` |
| `api/validation/createSampler.spec.ts` | partial | 1 / 2 (`lodMinAndMaxClamp`) | `maxAnisotropy` (WebIDL number coercion `-1`/`undefined` — no C `uint16` analog) | green on wgpu-native **and yawgpu** (`lodMinAndMaxClamp`=1 case/49 subcases — both validate lod clamp ranges). First float-`Value` test; fixture deviation `GpuTest` |
| `api/validation/encoding/cmds/clearBuffer.spec.ts` | partial | 7 / 8 (`buffer_usage`, `default_args`, `size_alignment`, `offset_alignment`, `overflow`, `out_of_bounds`, `buffer_state`) | `buffer,device_mismatch` (second device) | green on **yawgpu & Dawn**. wgpu-native aborts on `size_alignment`/`out_of_bounds` ([F-002](FINDINGS.md)) and the `buffer_state` destroyed case ([F-004](FINDINGS.md)); contained via `--isolate`. Command-encoder + `undefined`-`Value` + resource-state; fixture deviation `GpuTest` |
| `api/validation/encoding/cmds/copyBufferToBuffer.spec.ts` | partial | 7 / 8 (`buffer_usage`, `copy_size_alignment`, `copy_offset_alignment`, `copy_overflow`, `copy_out_of_bounds`, `copy_within_same_buffer`, `buffer_state`) | `buffer,device_mismatch` (second device) | green on **yawgpu & Dawn**. wgpu-native passes the copy tests but aborts on the 3 `buffer_state` destroyed-submit cases ([F-004](FINDINGS.md)); contained via `--isolate`. Fixture deviation `GpuTest` |
| `api/validation/createBindGroupLayout.spec.ts` | partial | 2 ported (`duplicate_bindings`, `maximum_binding_limit`) | `visibility*` (validStages/per-stage storage limits), storage-texture/format/multisampled tests | green on **all three** backends (agree; no divergence). New BGL resource type; `b0`/`b1` deviation for the array param; fixture deviation `GpuTest` |
| `api/validation/createPipelineLayout.spec.ts` | partial | 1 ported (`number_of_bind_group_layouts_exceeds_the_maximum_value`) | `number_of_dynamic_buffers…`, `bind_group_layouts,device_mismatch`, others | green on **all three** backends (agree). New pipeline-layout resource type; fixture deviation `GpuTest` |
| `api/validation/buffer/mapping.spec.ts` | partial | 5 ported (`mapAsync,usage`, `mapAsync,state,{destroyed,mappedAtCreation,mapped}`, `mapAsync,invalidBuffer`) | `mapAsync,state,mappingPending` (JS timing); other mapping tests (getMappedRange/unmap/ranges) not yet ported | green on **yawgpu and Dawn**. wgpu-native diverges on 3 of the mapAsync tests ([FINDINGS F-003](FINDINGS.md)). `bufferMapSync` + `getErrorBuffer`; fixture deviation `GpuTest` |
| `api/validation/createTexture.spec.ts` | partial | 7 ported (`sample_count,1d_2d_array_3d`, `dimension_type_and_format_compatibility`, `sampleCount,various_sampleCount_with_all_formats`, `zero_size_and_usage`, `mipLevelCount,format`, `mipLevelCount,bound_check`, `mipLevelCount,bound_check,bigger_than_integer_bit_width`) | the other ~14 createTexture tests + `texture_size,*` (limit-variant size → T4) + `texture_usage`/`usage`/`new_usages`/`viewFormats*` (need renderable+usage capability tables → T5) + createView deferred | **Texture file T1–T3.** On `AllFeaturesMaxLimitsGpuTest` (all adapter features + max limits). `texture_format.h` holds all **101** formats (49 uncompressed + 52 compressed BC/ETC2/ASTC, count parity vs upstream) + `multisample`/class + `isBC/isASTC/isETC2/isCompressed` + tier1-blendable set + `maxMipLevelCount` + `textureFormatAndDimensionPossiblyCompatible`; helpers `createTextureTracked`, `skipIfTextureFormatNotSupported`, `textureDimensionAndFormatCompatibleForDevice` (+3D BC/ASTC sliced exception), `skipIfTextureFormatAndDimensionNotCompatible`, `isTextureFormatMultisampled`. Counts: `dimension_type…` 404, `sampleCount,various…` 202/8, `zero_size_and_usage` 13/78, `mipLevelCount,format` 330/4520, `mipLevelCount,bound_check` 2/21 — all green on **wgpu-native & Dawn** (adapter feature exposure differs: Dawn full matrix, wgpu-native skips tier1/depth32fs8). **yawgpu fails/crashes** on the 12 rejected-as-`Undefined` color formats + `Depth24PlusStencil8` ([F-005](FINDINGS.md)) and 6 multisample-capability formats ([F-006](FINDINGS.md)); triaged in `expectations/yawgpu.txt`. T3 also drove the **per-case subcase-expansion** harness fix (subcase filters can reference case params; see [02-harness](02-harness.md)) |

## Maintenance

- After porting a file: set its row here, update the **Area summary** counts, and confirm
  `cts --list 'webgpu:<file>:*'` matches the upstream case count.
- When re-baselining (see [UPSTREAM.md](UPSTREAM.md)): diff the new upstream listing against ours
  to find added/removed/renamed files and changed case counts; reflect them here.
- A planned `tools/coverage` step automates the listing diff and can emit a fresh area summary —
  see [05-porting-guide §7](05-porting-guide.md).
