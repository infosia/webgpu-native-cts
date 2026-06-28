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
| yawgpu | [github.com/infosia/yawgpu](https://github.com/infosia/yawgpu) | `05bf865` (2026-06-28; Vulkan fixes `bd21cfb`) | MIT / Apache-2.0 | **primary conformance subject**; Metal and Vulkan HALs. **WGSL frontend = Tint** (Dawn's shader compiler), so shader compile/validation is Dawn-equivalent. **Passes the entire ported suite `fail=0 crash=0` on native Metal _and_ native Vulkan** (NVIDIA RTX 5060 Ti) — Metal sweep `api/*` 450,926, `shader/execution` 725,445, `shader/validation` 500,375 (per-subcase). The only expected failures are the spec-in-flux Vulkan cases in `expectations/yawgpu-vulkan.txt` (sample_mask/position semantics, MoltenVK-only artifacts); Metal has none (`expectations/yawgpu.txt`). Vendor header `yawgpu.h` exists but its extension surface is **not yet exercised** by the suite. See [FINDINGS](FINDINGS.md). |
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
