# Spec — Fix four CTS port-oracle bugs (textureLoad / textureStore / trig validation)

## Background

A full yawgpu/native-Vulkan sweep (Windows, NVIDIA RTX 5060 Ti, 2026-06-28) produced
2,190 subcase failures. A **Dawn cross-check** (the reference WebGPU implementation,
`build-dawn/Release/cts.exe`) showed that **1,952 of those fail on Dawn with identical
counts**. Since the real Dawn implementation is correct, identical failure proves the
fault is in the **port's expected-value/expected-validity oracle** (the C++ reimplemented
test logic), NOT in any GPU backend. These are CTS *port* bugs.

This spec covers the four confirmed oracle bugs. Each fix is in **backend-independent
test-harness code**. The remaining 237 fails (robust_access:linear_memory; textureStore
bgra8unorm) are genuine yawgpu defects — **out of scope here**.

## Hard constraints (read first)

- **Backend-independent only.** NEVER branch on the backend (no `if (vulkan)`, no
  `#ifdef`). The oracle must compute the value/validity that real WebGPU produces,
  per spec, for all backends. A correct fix makes Vulkan AND Dawn AND Metal pass.
- **Upstream-faithful.** Match how upstream CTS (the pinned commits in `docs/UPSTREAM.md`
  and the `// Ported from ... @ <sha>` header on each file) handles the case. Prefer the
  upstream tolerance/semantics over an ad-hoc "make it pass" hack.
- **Metal-safety.** Metal cannot be run on this Windows host. Safety is guaranteed by the
  two rules above (the fixes correct the test, not a backend). Do not introduce anything
  that could differ per backend.
- Do not touch yawgpu, naga, Dawn, or wgpu-native code. Only the CTS port's test/harness.

---

## Fix 1 (C5) — `absBigInt(INT64_MIN)` overflow in trig validation

**File:** `src/webgpu/shader/validation/expression/call/builtin/const_override_builtin.h:865`
```cpp
inline int64_t absBigInt(int64_t v) { return v < 0 ? -v : v; }
```
**Bug:** Upstream uses JS `BigInt` (arbitrary precision). The port narrowed it to `int64_t`.
For `v == INT64_MIN` (the abstract-int min, `(-9223372036854775807 - 1)` = -2^63), `-v`
overflows (UB) and stays negative, so callers' `absBigInt(v) <= 1` / `< 1` wrongly return
`true` → the test expects a *valid* shader, but `acos/asin/atanh` of -2^63 is correctly
rejected as an out-of-range const-eval error by Dawn and yawgpu. → 12 fails
(acos ×4, asin ×4, atanh ×4).

**Fix:** Return the true magnitude without signed overflow, e.g. compute in `uint64_t`:
```cpp
inline uint64_t absBigInt(int64_t v) {
    return v < 0 ? (~static_cast<uint64_t>(v) + 1u) : static_cast<uint64_t>(v);
}
```
(`uint64_t` magnitude of `INT64_MIN` = 2^63, so `<= 1` / `< 1` correctly yields `false`.)
Callers compare against `1` — unsigned comparison is correct for all values. Verify the
three call sites (`acos.spec.cpp:48`, `asin.spec.cpp:47`, `atanh.spec.cpp:47`) still
compile (they compare the result to an int literal; `uint64_t <= 1` is fine).

**Acceptance:** `webgpu:shader,validation,expression,call,builtin,{acos,asin,atanh}:values:*`
→ `fail=0` on **both** yawgpu Vulkan and Dawn.

---

## Fix 2 (C1) — sRGB `textureLoad` comparison tolerance too tight

**File:** `src/webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.cpp:2796-2798`
```cpp
if (kind == LoadReturnKind::Float && baseFormat(format) != format && component < 3u) {
    return std::fabs(floatFromBits(expected) - floatFromBits(got)) <= 0.00025f;
}
```
**Bug:** This sRGB-only branch (triggered because `baseFormat()` strips `-srgb`) uses a
near-exact tolerance `0.00025f`. The hardware sRGB→linear unit is only required to carry
≥8 fractional bits of precision (it quantizes to multiples of ~1/256). For sample byte
249 the oracle's double-precision decode gives `0.9473065` but the GPU returns `0.9453125`
(= 242/256); the error ≈ `0.001994` exceeds `0.00025f` by ~8×. → 240 fails on the 2
uncompressed sRGB formats (`rgba8unorm-srgb`, `bgra8unorm-srgb`) across
sampled_1d/2d/3d/arrayed.

**Fix:** Widen the sRGB tolerance to the hardware-permitted bound. `1/256 ≈ 0.0039f`
covers the observed `0.001994` (note `1/512` would NOT). Use the upstream sRGB tolerance
if discernible; otherwise `1.0f / 256.0f`. Keep it scoped to this sRGB branch only — do
NOT loosen the non-sRGB or depth paths.

**Acceptance:** `webgpu:shader,execution,expression,call,builtin,textureLoad:sampled_2d:*`
(and sampled_1d / sampled_3d / arrayed) → `fail=0` on **both** yawgpu Vulkan and Dawn.

---

## Fix 3 (C2) — storage `textureLoad` coordinates ignore `baseMipLevel`

**File:** `src/webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.cpp:2812`
```cpp
const uint32_t level = c.useLevel ? (i % c.mipLevelCount) : 0u;
const WGPUExtent3D mipSize = physicalMipSize(c.baseSize, c.textureDimension, level);
```
**Bug:** Storage-texture load cases set `useLevel=false`, so coordinates are generated
against the **mip-0 size** (8×8). But the storage *view* targets `baseMipLevel`
(`viewDesc.baseMipLevel`). For `storage_textures_2d`/`_2d_array` the cases use
`mipLevelCount=3` with `baseMipLevel ∈ {0,1}`; when `baseMipLevel=1` the viewed mip is
4×4, so coords (0..7) go **out of bounds**. WGSL `textureLoad` on OOB storage coords
returns 0 (what GPU/Dawn return), but the oracle reads `mips[baseMipLevel]` via `texelAt`,
which **clamps** the coord and returns a real texel → mismatch. → 1,680 fails
(storage_2d 240 + storage_2d_array 1440), the `baseMipLevel=1` half, all formats.

**Fix:** For storage-texture load cases, generate coordinates against the **viewed mip**
(`baseMipLevel`) so loads stay in-bounds, e.g.:
```cpp
const uint32_t level = c.useLevel ? (i % c.mipLevelCount)
                                  : (c.isStorageTexture ? c.baseMipLevel : 0u);
```
Use the actual field names on `TextureLoadCase` for "is a storage-texture case" and
"base mip level of the view" (confirm by reading the struct + how storage cases are
built around `texture_utils.cpp:5385-5398` / `:3275`). The non-storage (`else`/sampled)
path must remain `0u` — do not regress sampled/1d/3d which currently pass. This matches
upstream, which exercises in-bounds storage loads.

**Acceptance:**
`webgpu:shader,execution,expression,call,builtin,textureLoad:storage_textures_2d:*` and
`:storage_textures_2d_array:*` → `fail=0` on **both** yawgpu Vulkan and Dawn; and
`storage_textures_1d` / `storage_textures_3d` / all sampled sub-tests remain `fail=0`
(no regression).

---

## Fix 4 (C3) — rgb10a2unorm `textureStore` tie-rounding vs exact-byte compare

This is the test mis-attributed as **F-128** (a yawgpu Vulkan HAL bug). It is a port
oracle bug: Dawn fails identically; Metal "passes" only because the Apple GPU rounds the
exact-half tie the same way the oracle does.

**Files:**
- Value set: `src/.../texture_sampling/texture_utils.cpp:4039-4041` —
  `rgb10a2unorm` store inputs `{-0.1, 0, 0.5, 1.0, 1.1}`.
- Encode: `src/webgpu/util/texel_data.cpp:42-46` `floatAsNormalizedInteger` uses
  `std::llround(value * scale)`.
- Compare: storage-store result compared **byte-exact** (`texture_utils.cpp:5681-5689`).

**Bug:** `0.5 × 1023 = 511.5` is an exact 10-bit quantization tie. `std::llround` rounds
half **away from zero** → 512 (`0x200`); the GPU (Dawn on Vulkan/D3D, yawgpu on Vulkan)
stores 511 (`0x1FF`). Both neighbors are spec-permitted for an exact-half value, but the
**byte-exact comparison** rejects 511. → 20 fails (only `rgb10a2unorm`; every other unorm
store array was deliberately chosen to avoid `0.5` ties).

**Fix (preferred — upstream-faithful):** For normalized (unorm/snorm) store formats,
compare with **±1 in the encoded integer per component** (decode the packed result and
expected, allow a 1-ULP difference) instead of byte-exact, since the spec permits either
neighbor at an exact half. Scope the tolerance to normalized formats; keep exact compare
for integer/uint/sint formats and for the bit-exact float formats.
**Fix (acceptable fallback):** replace the lone tie value `0.5` in the `rgb10a2unorm`
array (`:4040`) with a non-tie value, consistent with how the 8/16-bit unorm arrays
(`:4006`, none of which tie) were chosen. Prefer the tolerance fix; only fall back if the
tolerance path is impractical in the store-compare code.

Do NOT touch the `bgra8unorm` part of this test — that is a separate, real yawgpu defect
(Dawn passes it).

**Acceptance:**
`webgpu:shader,execution,expression,call,builtin,textureStore:texel_formats:*` →
`rgb10a2unorm` cases pass on **both** yawgpu Vulkan and Dawn (`bgra8unorm` may still fail
on yawgpu Vulkan — that is the separate real defect, expected). No regression on other
formats.

---

## Global acceptance criteria

1. Rebuild Release (`cmake --build build-yawgpu --config Release`) clean.
2. Re-run each fix's acceptance query on **yawgpu Vulkan** (`CTS_YAWGPU_BACKEND=vulkan
   build-yawgpu/Release/cts.exe`) — targeted fails drop to 0 (except the noted bgra8unorm
   real defect).
3. Re-run the same queries on **Dawn** (`build-dawn/Release/cts.exe`) — same fails drop to
   0. (This is the cross-backend proof the oracle is now correct.)
4. No new failures introduced in the touched files' tests (run the full
   `textureLoad`, `textureStore`, and `{acos,asin,atanh}` test files and compare).
5. Each edit carries a brief comment citing the root cause and the upstream behaviour.

## Verification commands (reviewer/Claude runs these; do not commit outputs)
```
B=build-yawgpu/Release/cts.exe ; D=build-dawn/Release/cts.exe
for E in "$B" "$D"; do CTS_YAWGPU_BACKEND=vulkan "$E" --workers 8 \
  'webgpu:shader,validation,expression,call,builtin,acos:values:*' \
  'webgpu:shader,execution,expression,call,builtin,textureLoad:sampled_2d:*' \
  'webgpu:shader,execution,expression,call,builtin,textureLoad:storage_textures_2d_array:*' \
  'webgpu:shader,execution,expression,call,builtin,textureStore:texel_formats:*' ; done
```
