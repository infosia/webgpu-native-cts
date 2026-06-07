# UPSTREAM — Pinned Revisions

This file pins the exact external revisions this suite is ported against, so coverage and test
identity can be tracked deterministically. Update it whenever we re-baseline against a newer
upstream CTS or bump a backend / the canonical headers.

## Pinned upstream CTS

The source of truth for what we port. Test names, query identities, and case/subcase structure
target this revision.

| Field | Value |
|-------|-------|
| Project | gpuweb/cts (WebGPU CTS) |
| Repo | https://github.com/gpuweb/cts.git |
| Pinned commit | `b507bd117e53db86f2fb52d0d858d3ae7d684a85` |
| Commit date | 2026-05-28 |
| Subject | Add validation coverage for `const SHIFT override` (#4649) |
| Pinned on | 2026-05-30 |

> The pinned revision above (`b507bd1`) is canonical for this project. Port against a local
> `gpuweb/cts` checkout at that commit (set its location wherever you cloned it; no fixed path is
> assumed).

## Pinned canonical WebGPU headers

The C API all three backends implement. Tests are written against this header.

| Field | Value |
|-------|-------|
| Project | webgpu-native/webgpu-headers |
| Pinned commit | `7d3186c3dd2c708703524027b46b8703534ab3cc` |
| Commit date | 2026-03-19 |
| Source | wgpu-native submodule `ffi/webgpu-headers` (`7d3186c`) |

This is the revision vendored under `third_party/webgpu-headers/` (planned). Each backend ships its
own copy of the canonical header (wgpu-native and yawgpu both under `ffi/webgpu-headers/webgpu.h`;
Dawn regenerates its own from its generator). We treat the webgpu-native/webgpu-headers revision
above as the reference for struct/enum/function shapes and reconcile any per-backend drift in the
backend shim (see [03-webgpu-c-abstraction §6](03-webgpu-c-abstraction.md)).

## Backends under test

Recorded for reproducibility of conformance results. Not a hard pin — CI may test newer builds —
but the versions a given results set was produced against should be noted alongside that result
set (e.g. in an `--expectations` file header).

| Backend | Repo | Version observed (local) | License | Notes |
|---------|------|--------------------------|---------|-------|
| wgpu-native | github.com/gfx-rs/wgpu-native | `v29.0.0.0-8-g9176708` | MIT / Apache-2.0 | harness bring-up reference |
| yawgpu | [github.com/infosia/yawgpu](https://github.com/infosia/yawgpu) | `3c847acbc4febd6a13ab480c6e961d4222c0554b` (2026-06-04) | MIT / Apache-2.0 | **primary conformance subject**; Metal/Vulkan; vendor header `yawgpu.h`. At this revision yawgpu **passes every ported test through T27** — Metal re-verified at this revision (2026-06-04): full `api,operation` `pass=169958 skip=5 fail=0 crash=0` and `api,validation` `fail=0 crash=0` (real-GPU Metal: `api,validation` `pass=4332 skip=383 fail=0 crash=0`; `api,operation` — `command_buffer,{clearBuffer,copyBufferToBuffer,basic,image_copy,copyTextureToTexture}` Dawn-equal (`image_copy` `pass=138408 fail=0` color + depth/stencil, color `copyTextureToTexture` `pass=30910 skip=6 fail=0`, depth/stencil `copy_depth_stencil` `pass=216 fail=0`), `queue,writeBuffer` 17/17, `onSubmittedWorkDone` 5/5). The two latest findings — **F-029** (a cross-test Vulkan device-resource leak in `image_copy`) and **F-030** (an intermittent `MAP_READ` readback race the F-029 fix un-masked) — were found on Windows/Vulkan and fixed in `1e67300`/`1297b7e`; **re-verified on Metal** (af9ac5c-era): a combined 7-file `api,operation` run in one process is `pass=32598 fail=0` (no cross-test poison), and `image_copy:*` is deterministically `pass=137256 fail=0` over repeated runs (incl. `mip_levels:*` ×5). The color `image_copy` port (T24b) surfaced two findings — **F-025** (`queueWriteTexture` writes zeros) and **F-026** (`copyBufferToTexture`/`copyTextureToBuffer` mishandle non-default buffer layout + mip levels) — both **fixed** in `1e6c70b` (HAL texture dimension/array/mip support + queueWriteTexture upload); the full `image_copy` run is now `pass=137256 fail=0` (Dawn-equal), and `expectations/yawgpu.txt` has **no** expected failures (findings surfaced, not masked). It additionally runs the 8 `immediate_data_size` cases Dawn skips (yawgpu `maxImmediateSize=64`). Earlier yawgpu findings F-005/006/008/009/010/011/014/016/018/020/022/023/024/025/026/029/030/031 are all fixed; the T26 depth/stencil `copyTextureToTexture` port surfaced **F-031** (the **depth aspect** of `copyTextureToTexture`; stencil passed), **fixed** in `f3afc31` (depth support in yawgpu's regular real-backend render path + multi-layer depth copies) — re-test `copy_depth_stencil` `pass=216 fail=0` (Dawn-equal, up from `pass=36 fail=180`). The T27 `image_copy` depth/stencil port surfaced **F-032** (yawgpu zeroed the depth aspect of `copyTextureToBuffer` and the stencil aspect of packed depth+stencil formats; `pass=288 fail=864`), **fixed** in `c8f15d5`/`af9ac5c` (depth/stencil aspect buffer-copy support + packed-aspect buffer-size validation) — re-test `image_copy` depth/stencil `pass=1152 fail=0`, full `image_copy pass=138408 fail=0`. **yawgpu has no open findings on Metal or Vulkan through T27.** (Later vertical ports: the T30 `rendering/draw` port surfaced **F-034** (no indexed/indirect draw execution), **resolved** in `36a6b66`; the T31 `rendering/color_target_state` port surfaced **F-035** — yawgpu ignored `GPUColorTargetState` `blend` + `writeMask`, **resolved** in `74f5ef2` (re-test `pass=23 fail=0` on Metal **and** Vulkan/MoltenVK), cross-HAL Metal == Vulkan/MoltenVK; the T32 `rendering/depth` port surfaced **F-037** — a **Metal-HAL-only** non-deterministic flake (`fail≈33–44/130`, varying; every case passes in isolation) while Dawn/wgpu-native-Metal/yawgpu-Vulkan-MoltenVK were all clean `pass=130`, **resolved** in `186cd54` (Metal HAL now emits `[[point_size]]` for point-list pipelines; re-test `pass=130 fail=0` across 11 runs); the T33 `rendering/stencil` port surfaced **F-038** — yawgpu **mishandled stencil ops/compare/masks** (`pass=97 fail=91` deterministic vs Dawn/wgpu `pass=188`), **cross-HAL** (Metal == Vulkan/MoltenVK byte-identical → shared stencil translation), **resolved** in `40f5d7f` (dynamic stencil reference now threaded to the HAL; re-test `pass=188 fail=0` on both HALs); the T35 `memory_sync/buffer/single_buffer` port surfaced **F-039** — `two_dispatches_in_the_same_compute_pass` read back `0` instead of `2` under batch/`--isolate` (passed in isolation), **cross-HAL** (Metal == Vulkan/MoltenVK), **resolved** in `89f25df` (compute dispatch is now a per-dispatch usage scope; re-test `pass=25 fail=0` across all run modes); the T36 `render_pass/resolve` port surfaced **F-040** — yawgpu's **multisample resolve did not write the resolve target** (all 12 `render_pass_resolve` subcases failed, `storeop2` passed), **cross-HAL** (Metal == Vulkan/MoltenVK), **resolved** in `bc8c280`+`3303058` (MSAA render pipelines + multisample resolve + multi-color-attachment support; re-test `pass=12 fail=0` on both HALs); the T37 `storage_texture/read_only` port surfaced **F-041** — yawgpu's **read-only storage-texture `textureLoad` read back zero** (all 3 `basic` cases failed; the compute storage-buffer path worked), **cross-HAL** (Metal == Vulkan/MoltenVK), **resolved** in `2e4edb7` (wired storage-texture bindings + MSL runtime-array buffer sizes; re-test `pass=3 fail=0` on both HALs); the T39 `memory_sync` `two_draws_*` port surfaced **F-042** — yawgpu's **render-stage (fragment) storage write** from a `point-list` draw read back `0` (all 5 cases failed; the compute storage write worked), **cross-HAL** (Metal == Vulkan/MoltenVK), **resolved** in `042902b`+`eadc2f6` (render usage scope allows write+write across draws + render-bundle execution; re-test `pass=5 fail=0` on both HALs); the T43 `rendering/3d_texture_slices` port surfaced **F-043** — yawgpu **ignored render-pass `depthSlice`** and always rendered to slice 0 of a 3D texture (the 3 `depthSlice=1` cases failed; Dawn/wgpu pass all 6), **cross-HAL** (Metal == Vulkan/MoltenVK byte-identical), **resolved** in `c6935f7` (the attachment's `depthSlice` is now threaded into the 3D render-target view; re-test `pass=6 fail=0` on both HALs); the T46 `vertex_state/correctness` port surfaced **F-044** — yawgpu decodes only `float32x4`, the other 8 representative vertex formats (`float16/uint/sint/unorm/snorm/packed`) read back **zero** in the shader (`pass=1 fail=8`; Dawn/wgpu pass all 9), **cross-HAL**, **OPEN**; the T45 `rendering/depth_clip_clamp` port surfaced **F-045** — `frag_depth` is **not clamped to the viewport depth range** before the depth test, failing on **both yawgpu (cross-HAL) and wgpu-native** while Dawn passes, **OPEN**; the T47–T50 `render_pipeline` area ports surfaced **F-046** — yawgpu **mishandles face culling / `front_facing` winding** (`culling_tests`, cross-HAL `pass=2 fail=10`) — and **F-047** — yawgpu **ignores pipeline-overridable `override` constants** (read as zero; `overrides`, cross-HAL `pass=1 fail=5`), both **OPEN** (Dawn/wgpu pass); the T51 `render_pass/clear_value` port surfaced **F-048** — the **stencil reference value is not masked to the stencil aspect's bit width** before the compare, failing on **both yawgpu (cross-HAL) and wgpu-native** (`pass=24 fail=6`) while Dawn passes; the T54 `command_buffer/render/render_bundle` port surfaced **F-049** — yawgpu **render-bundle execution mishandles the viewport rect / bundle draw-args / repeated-blended replay** (only `basic` passes; `pass=1 fail=3`, cross-HAL; Dawn/wgpu pass all 4); the T58 `command_buffer/queries/occlusionQuery` port surfaced **F-050** — yawgpu's **occlusion query returns zero even when samples pass** (cross-HAL; Dawn/wgpu pass), both **OPEN** — all surfaced, not masked — see [FINDINGS](FINDINGS.md).) Both depth/stencil findings are now fixed on the Vulkan HAL too, confirmed on **native Windows/NVIDIA RTX 5060 Ti, 2026-06-04**: F-031 `copy_depth_stencil` `pass=216 fail=0` (`cac328a`) and F-032 `image_copy` depth/stencil `pass=1152 fail=0` (`3c847ac`, up from a confirmed native `pass=352 fail=800` real-HAL-gap, byte-identical to MoltenVK); the full ported suite on native Vulkan is green — all 7596 ported cases pass or skip (`pass=7208 skip=388 fail=0`, a per-**case** count; the per-test `pass=…` numbers above are per-**subcase**). The **GLES** HAL is the only untested follow-up (not a known defect). The one Mac-only artifact — F-033 color `copyTextureToTexture` under MoltenVK — is a **confirmed** MoltenVK translation limitation, absent on native Vulkan. F-015 (view-usage subset), F-027 and F-028 (3D multi-slice copy/readback) are wgpu-native-only. See [FINDINGS](FINDINGS.md) |
| Dawn | dawn.googlesource.com/dawn | `802f147f1fb7ab972f87f3e9f95098e0f4b5077b` (2026-05-15) | BSD-3-Clause / Apache-2.0 | C++ reference impl |

## Re-baselining process

When moving to a newer upstream CTS revision:

1. Update the **Pinned upstream CTS** commit/date/subject above.
2. Regenerate the upstream listing (in the upstream checkout: `npx grunt generate-listings` or the
   project's documented command) and diff it against our `src/webgpu/listing.json` to find:
   - new/removed/renamed files and tests,
   - changed case counts (params changes upstream).
3. Update [COVERAGE.md](COVERAGE.md) with newly-available work and any tests that disappeared.
4. Port or re-port affected files; re-run `cts --list` to confirm case-count parity.
5. Bump the canonical headers pin if upstream moved to a newer `webgpu.h` that changes the API.

A helper (`tools/coverage`, planned) automates step 2's diff — see
[05-porting-guide §7](05-porting-guide.md).

## History

| Date | Change |
|------|--------|
| 2026-05-30 | Initial pin: CTS `b507bd1`, headers `7d3186c`. |
