# Audit-fix — faithful `api/operation/texture_view/write` port (query identity + full `format` matrix)

The audit flagged `texture_view/write.spec.cpp` as a non-faithful slice: it promoted the upstream `format`
test's `method` param into three invented TEST names (`storage-write-compute`, `storage-write-fragment`,
`render-pass-store`) and covered only `rgba8unorm` / `sampleCount=1` / `viewUsageMethod=inherit`. Re-port
faithfully under the upstream query identity. Pin `b507bd117e53db86f2fb52d0d858d3ae7d684a85`. Upstream:
`<upstream-cts-checkout>/src/webgpu/api/operation/texture_view/write.spec.ts` (421 lines).

REPLACE the three invented tests with the upstream identities:

## `format` (single test, full param matrix)

`.params`: `method ∈ kTextureViewWriteMethods` {`storage-write-compute`, `storage-write-fragment`,
`render-pass-store`, `render-pass-resolve`} × `format ∈ kRegularTextureFormats` × `sampleCount ∈ {1,4}` ×
`viewUsageMethod ∈ kTextureViewUsageMethods` {`inherit`, `minimal`}, with upstream's exact `.filter`:
- skip `format === 'rgb10a2uint'` entirely (upstream TODO [2]);
- `storage-write-{compute,fragment}` and `render-pass-resolve` → `sampleCount === 1` only;
- `render-pass-store` → `sampleCount > 1` excluded (upstream TODO [1]).

Body (mirror upstream exactly):
- `skipIfTextureFormatNotSupported(format)`; if `sampleCount>1` `skipIfTextureFormatNotMultisampled`; per
  method: storage → `skipIfTextureFormatNotUsableWithStorageAccessMode('write-only', …)`, render-pass-store
  → `skipIfTextureFormatNotUsableAsRenderAttachment`, render-pass-resolve → `…NotUsableAsRenderAttachment`
  + `skipIfTextureFormatNotResolvable`. (The compat `storage-write-fragment` / maxStorageBuffersInFragment
  guard is a no-op on a native non-compat device — keep it as a comment.)
- usage = `COPY_SRC | (storage ? STORAGE_BINDING : RENDER_ATTACHMENT)`; create a `kTextureSize²` texture at
  `sampleCount`; create a view with usage from `getTextureViewUsage(viewUsageMethod, textureUsageForMethod)`
  (`inherit` → 0 / inherit the texture usage; `minimal` → exactly `textureUsageForMethod`).
- `writeTextureAndGetExpectedTexelView(t, method, view, format, sampleCount)` writes a known per-texel color
  pattern through the view via the method and returns the expected `TexelView`; then
  `expectTexelViewComparisonIsOkInTexture(t, {texture}, expectedTexelView, [kTextureSize, kTextureSize])`.

## `dimension` and `aspect` — `.unimplemented()`

Upstream declares both but marks them `.unimplemented()`. Register them `.unimplemented()` natively too
(query identity) with the upstream reason.

## Helpers to ADD (reuse existing infra)

- `kTextureViewWriteMethods` / `kTextureViewUsageMethods` (small enums).
- `getTextureViewUsage(viewUsageMethod, textureUsage)` — `inherit` returns 0 (let createView inherit),
  `minimal` returns exactly `textureUsage`.
- `writeTextureAndGetExpectedTexelView(...)` — generalize the local file's existing rgba8unorm write paths
  to all `kRegularTextureFormats` using the existing `TexelView` infra (`util/texture_ok.h`); the storage
  methods write via a `texture_storage_2d<format, write>` compute/fragment shader, render-pass-store via a
  fragment shader to a color attachment, render-pass-resolve via an MSAA resolve. Encode the expected texel
  values per format (reuse whatever per-format texel encoding `texture_ok.h` / the existing comparison path
  provides; do not invent a parallel encoder).
- `skipIfTextureFormatNotMultisampled` / `skipIfTextureFormatNotResolvable` (small `GpuTest` guards if not
  present — reuse `texture_format.h` multisample/resolve predicates; the resolvable set mirrors the V4
  attachment-compatibility resolve flags).

## Rules

- Query root `api,operation,texture_view,write`; the upstream `format` (single test) + `dimension`/`aspect`
  `.unimplemented()`. Fixture as the current file (`AllFeaturesMaxLimitsGpuTest`).
- Feature-gated formats runtime-skip (the `skipIf*` guards). Fragment/storage output types match the format
  base type (`vec4<f32/u32/i32>`).
- MSVC `/W4 /WX`: no `__builtin_*`, no local var named `g` (use `testGroup`), no `WGPU_*_INIT` as a ternary
  operand, no `+` on adjacent C-string literals, no unused lambda captures. English only; keep the
  upstream-path header line (drop the stale "Deferred:" note — `format` is now full).
- Faithful bodies; this REPLACES the 3 invented tests. Only `dimension`/`aspect` are `.unimplemented()`.

## Build / self-check (codex)

- `cmake --build build-dawn --target cts -j8`, `cmake --build build --target cts -j8`,
  `cmake --build build-yawgpu --target cts -j8` clean; `cmake --build build --target gen_listings -j8`;
  `./build/cts --list 'webgpu:api,operation,texture_view,write:*'` shows `format` (full matrix) +
  `dimension` + `aspect` (unimplemented).
- Do NOT run GPU; do NOT touch listing.json or docs. The orchestrator runs Dawn/yawgpu/wgpu verification.
- Report: `format`'s registered case/subcase count, the helpers added, how the expected TexelView is
  computed per format/method, and the build + `--list` output.
