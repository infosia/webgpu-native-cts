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
| yawgpu | [github.com/infosia/yawgpu](https://github.com/infosia/yawgpu) | `af9ac5c5da7ed96dee0c7fa4e6c4d47927f54490` (2026-06-04) | MIT / Apache-2.0 | **primary conformance subject**; Metal/Vulkan; vendor header `yawgpu.h`. At this revision yawgpu **passes every ported test through T27** (real-GPU Metal: `api,validation` `pass=4332 skip=383 fail=0 crash=0`; `api,operation` — `command_buffer,{clearBuffer,copyBufferToBuffer,basic,image_copy,copyTextureToTexture}` Dawn-equal (`image_copy` `pass=138408 fail=0` color + depth/stencil, color `copyTextureToTexture` `pass=30910 skip=6 fail=0`, depth/stencil `copy_depth_stencil` `pass=216 fail=0`), `queue,writeBuffer` 17/17, `onSubmittedWorkDone` 5/5). The two latest findings — **F-029** (a cross-test Vulkan device-resource leak in `image_copy`) and **F-030** (an intermittent `MAP_READ` readback race the F-029 fix un-masked) — were found on Windows/Vulkan and fixed in `1e67300`/`1297b7e`; **re-verified on Metal at this pin**: a combined 7-file `api,operation` run in one process is `pass=32598 fail=0` (no cross-test poison), and `image_copy:*` is deterministically `pass=137256 fail=0` over repeated runs (incl. `mip_levels:*` ×5). The color `image_copy` port (T24b) surfaced two findings — **F-025** (`queueWriteTexture` writes zeros) and **F-026** (`copyBufferToTexture`/`copyTextureToBuffer` mishandle non-default buffer layout + mip levels) — both **fixed** in `1e6c70b` (HAL texture dimension/array/mip support + queueWriteTexture upload); the full `image_copy` run is now `pass=137256 fail=0` (Dawn-equal), and `expectations/yawgpu.txt` has **no** expected failures (findings surfaced, not masked). It additionally runs the 8 `immediate_data_size` cases Dawn skips (yawgpu `maxImmediateSize=64`). Earlier yawgpu findings F-005/006/008/009/010/011/014/016/018/020/022/023/024/025/026/029/030/031 are all fixed; the T26 depth/stencil `copyTextureToTexture` port surfaced **F-031** (the **depth aspect** of `copyTextureToTexture`; stencil passed), **fixed** in `f3afc31` (depth support in yawgpu's regular real-backend render path + multi-layer depth copies) — re-test `copy_depth_stencil` `pass=216 fail=0` (Dawn-equal, up from `pass=36 fail=180`). The T27 `image_copy` depth/stencil port surfaced **F-032** (yawgpu zeroed the depth aspect of `copyTextureToBuffer` and the stencil aspect of packed depth+stencil formats; `pass=288 fail=864`), **fixed** in `c8f15d5`/`af9ac5c` (depth/stencil aspect buffer-copy support + packed-aspect buffer-size validation) — re-test `image_copy` depth/stencil `pass=1152 fail=0`, full `image_copy pass=138408 fail=0`. **yawgpu has no open Metal findings** (follow-up: port the F-031 depth render path to the Vulkan/GLES HALs). F-015 (view-usage subset), F-027 and F-028 (3D multi-slice copy/readback) are wgpu-native-only. See [FINDINGS](FINDINGS.md) |
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
