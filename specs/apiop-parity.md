# `api/operation` parity with upstream `d116bcc`

> See [`reference/workflow.md`](reference/workflow.md) and
> [`../docs/05-porting-guide.md`](../docs/05-porting-guide.md).

## Goal

Every portable `api/operation` test enumerates upstream's cases and subcases and has a real body, and
the area passes on the Dawn oracle.

## Inventory (2026-11-20)

[`data/apiop-parity-d116bcc.tsv`](data/apiop-parity-d116bcc.tsv) compares every upstream test with the
native one (columns: file, test, upstream cases/subcases, native cases/subcases, status, missing cases,
missing parameter values). Upstream was counted in node with the `setup-ts-in-node.js` loader; native
with `cts --list` / `--list-cases`, normalising native spellings.

| | Upstream | Native |
|---|---|---|
| Tests | 264 | 229 |
| Cases | 10,695 | 7,088 |
| Subcases | 353,477 | 333,666 |

165 tests match. The gap is 33 FEWER, 35 MISSING and 17 REGISTERED-UNIMPLEMENTED tests: about 3,613
portable cases and 19,790 portable subcases. 14 tests are `.unimplemented()` upstream and stay stubs.

**Not portable (stay as registered stubs or absent; listed in `docs/COVERAGE.md`):**
`buffers/map_ArrayBuffer:postMessage`, `buffers/map_detach:while_mapped` (ArrayBuffer transfer/detach),
`adapter/info:same_object`, `device/lost:{not_lost_on_gc,same_object}`,
`labels:wrappers_do_not_share_labels`, `uncapturederror:onuncapturederror_order_wrt_addEventListener`
(JS object identity, GC, EventTarget), `adapter/requestDevice:limit,out_of_range` (WebIDL
`[EnforceRange]` TypeError).

## Batches

Ordered structural-first (see `reference/workflow.md`, "Vertical-first"). Each batch is one handoff.
Sizes are portable cases / subcases to add.

| Batch | Files and tests | Size |
|---|---|---|
| **O1** draw, vertex, state | `command_buffer/programmable/state_tracking` (5 missing tests + `encoderType` axis of `bind_group_indices`); `vertex_state/index_format` (4 missing incl. `primitive_restart`); `rendering/draw:vertex_attributes,basic`; `rendering/basic:large_draw`; `compute/basic:large_dispatch`; `adapter/info:device_matches_adapter` (skip where `wgpuDeviceGetAdapterInfo` is absent) | ~120 / 190 |
| **O2** queries, buffer init | `command_buffer/queries/occlusionQuery` (OcclusionQueryTest matrix, 7 missing, 3 widened); `queries/timestampQuery` (4 missing, `resolve_unused_slots` shape); `resource_init/buffer` (10 missing + subcase axes) | ~280 / 320 |
| **O3** memory sync | `memory_sync/buffer/single_buffer` rw/wr/ww full op × context × boundary matrix; `memory_sync/texture/same_subresource` TextureSyncTestHelper matrix + 2 `single_pass` tests; `memory_sync/texture/readonly_depth_stencil` formats | ~435 cases |
| **O4** texture zero-init | `resource_init/texture_zero:uninitialized_texture_is_zero` generic `check_texture` (CopyToBuffer, CopyToTexture, DepthTest, StencilTest, Sample × 1d/2d/3d × formats); may split by read method | ~401 / 6,890 |
| **O5** blending, stencil | `rendering/color_target_state:blending,{GPUBlendComponent,formats,clamping}`; `rendering/stencil:stencil_accumulation` | ~640 / 3,580 |
| **O6** sampling, storage textures | `sampling/filter_mode` (linear, mipmap, nearest format axis); `storage_texture/{read_only,read_write}` format × stage × dimension | ~1,090 / 1,730 |
| **O7** render-target formats | `render_pipeline/pipeline_output_targets` ×3, `render_pass/storeOp`, `render_pass/resolve`, `rendering/3d_texture_slices` (+1 missing), `rendering/depth_clip_clamp`, `render_pipeline/culling_tests` depth/stencil subcases | ~750 / 5,900 |

## Rules for every batch

- Parameters, case order and subcase counts equal upstream; check with the node loader and record
  per-test counts in the report. A native value spelling that differs from upstream must be one the
  suite already uses elsewhere.
- Real bodies only. Allowed skips: upstream's own (feature, limit, format support), and a C-API
  capability that a backend lacks, reported as a skip with the reason.
- Readback buffers start zeroed; expected values come from upstream's computation.
- Helpers shared across files go next to the existing ones (e.g. `src/webgpu/` utilities) and pipelines
  that do not depend on the case are cached per device.
- `--list-cases 'webgpu:*'` changes only in the batch's files.

## Acceptance criteria (per batch)

- [ ] `cmake --build build-dawn --target cts cts_unittests -j 8` succeeds without warnings; `build-yawgpu`
      and `build-wgpu` compile; `build-dawn/cts_unittests` exits 0.
- [ ] Every test in the batch has upstream's case and subcase counts.
- [ ] Dawn/Metal: the batch's files `fail=0 crash=0`, or each failure triaged as a Dawn/Metal finding.
- [ ] `docs/COVERAGE.md` rows and `listing.json` updated (Claude).

## Done when

All seven batches are committed, the inventory rerun shows only MATCH, UPSTREAM-UNIMPLEMENTED and the
non-portable tests above, and a full Dawn sweep is clean apart from known findings.
