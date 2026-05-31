# FINDINGS — per-backend conformance observations

Conformance differences surfaced by running the suite against the backends. This is an output of
the project (the point of a CTS). Each entry: what, which backend(s), the test that found it, and
status. Findings are reported, not silently worked around — we never weaken a test or mask a
backend defect to make it pass.

Backends and revisions are pinned in [UPSTREAM.md](UPSTREAM.md).

> **Crashing findings are now runnable.** Both F-001 and F-002 are process *aborts*. Since Phase 4
> they are contained by `--isolate` (per-case subprocess isolation) and marked expected in
> `expectations/wgpu-native.txt`, so a `cts --isolate --expectations expectations/wgpu-native.txt …`
> run on wgpu-native completes and exits 0. They remain **open backend defects** (still not masked).
>
> **3-way confirmation (Phase 2b).** Running the same C tests on all three backends shows
> **wgpu-native is the only one that aborts** on F-001/F-002 inputs; **yawgpu and Dawn both handle
> them gracefully** (validation errors, all subcases pass). This cross-implementation agreement
> isolates the defects to wgpu-native and is the differential value this suite provides.

---

## Re-test — yawgpu `92db062` (2026-05-31): **all yawgpu findings resolved**

yawgpu fixed every defect this suite surfaced, across two commits driven by these findings:
`2667b0a` (*"fix(core): createTexture format/usage conformance defects (external CTS
F-005/006/008/009/010)"*) and `92db062` (*"cts-findings: fix Depth24PlusStencil8 abort + RGBA8Snorm
tier1 storage + compressed size alignment"*). At `92db062`, yawgpu **passes every ported `api,validation`
test on real-GPU Metal**: `pass=2594 skip=16 fail=0 crash=0` — **0 failures, 0 crashes**, and
`expectations/yawgpu.txt` now has no expected-failure lines.

Resolution timeline (against the `55ac04d`-era baseline these entries were captured on):

| yawgpu | what changed | yawgpu fail/crash over `api,validation:*` |
|--------|--------------|-------------------------------------------:|
| `55ac04d` | baseline — findings captured | ~625 (+ hundreds of feature-skips) |
| `e39f57f` | added compression / tier1 / depth32fs8 features (skips → 16) | ~625 |
| `2667b0a` | fixed F-005 rejection, F-006, F-008, most of F-009 (420 `xpass`) | ~169 |
| `92db062` | fixed the Depth24PlusStencil8 abort (F-005), RGBA8Snorm storage (F-009), compressed `texture_size` alignment (F-010) | **0** |

Every yawgpu finding — [F-005](#f-005--yawgpu-mishandles-several-valid-uncompressed-texture-formats),
[F-006](#f-006--yawgpu-disagrees-on-which-texture-formats-are-multisampleable),
[F-008](#f-008--yawgpu-under-validates-transient-texture-usage-combinations),
[F-009](#f-009--yawgpu-over-restricts-render-attachment-dimension-and-under-validates-storage-usage),
[F-010](#f-010--yawgpus-newly-enabled-compressed--feature-gated-formats-have-validation-gaps) — is now
**resolved** (see each entry's Status). The open findings are all wgpu-native's (F-001–F-004, F-007).
This is the full intended cycle: the suite reports a divergence, it is fixed in yawgpu, and the fix is
confirmed on real hardware.

---

## F-001 — wgpu-native aborts on an invalid buffer-usage bit

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu.
- **Found by:** `webgpu:api,validation,buffer,create:usage:*` (Phase 3). The test feeds, among
  ~78 usage combinations, a bogus usage bit `kSomeBogusBufferUsage = 0x40000000`.
- **Observed:** wgpu-native **panics and aborts the process**:
  `thread '<unnamed>' panicked at src/lib.rs:1984:48: invalid buffer usage` →
  `fatal runtime error: ... aborting`. The panic crosses the C FFI boundary and kills the whole
  test process before the harness can observe a result.
- **Expected (WebGPU):** `createBuffer` with usage bits outside the valid set must raise a
  **validation error**, not abort. yawgpu does this correctly — it passes all 156 `usage` subcases.
- **Status:** open; tracked as a **wgpu-native defect**. We do **not** pre-screen/sanitize usage
  bits at the backend shim (that would hide the defect and falsely "pass" the test).
- **Harness implication:** a backend that *aborts* (rather than fails) cannot be triaged in-process
  via `--expectations` — the abort takes down the run. To run the rest of the suite on wgpu-native
  we need either a per-backend **crash skiplist** (exclude known-aborting cases, reported as
  `skip(known-crash:wgpu-native)`, never as pass) or **per-case subprocess isolation** (the robust
  general fix). See [07-roadmap](07-roadmap.md) (cross-cutting). Until then, avoid running
  `…buffer,create:usage:*` against wgpu-native; it runs fine on yawgpu.

---

## F-002 — wgpu-native aborts on an invalid clearBuffer size

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu.
- **Found by:** `webgpu:api,validation,encoding,cmds,clearBuffer:size_alignment:*` and
  `:out_of_bounds:*` (Phase 3d). Both feed `clearBuffer` sizes that are mis-aligned (e.g. 2, 5) or
  out of bounds (e.g. 36 on a 32-byte buffer).
- **Observed:** wgpu-native **panics and aborts** at the *encode* call —
  `panicked at src/lib.rs:1294:18: invalid size` inside `wgpuCommandEncoderClearBuffer` — before the
  harness can observe a `finish()`-time validation error. (`offset_alignment` and `overflow` do
  *not* abort; it is specifically invalid *size* that panics.)
- **Expected (WebGPU):** an invalid clear size/range must produce a **validation error** (surfaced at
  `commandEncoder.finish()`), not abort. yawgpu does this correctly — it passes all of
  `size_alignment` (7), `out_of_bounds` (8) and the other clearBuffer subcases (39 total).
- **Scope note:** this is **clearBuffer-specific** — `copyBufferToBuffer` with the same kinds of
  invalid sizes does *not* abort wgpu-native (it returns a validation error at `finish`, all 137
  subcases pass). So wgpu-native validates copy sizes gracefully but panics on clearBuffer sizes.
- **Status:** open; tracked as a **wgpu-native defect** (same class as [F-001](#f-001--wgpu-native-aborts-on-an-invalid-buffer-usage-bit)).
  Not masked. Avoid running `…clearBuffer:size_alignment:*` / `:out_of_bounds:*` against
  wgpu-native; they run fine on yawgpu. Reinforces the need for crash isolation (see
  [07-roadmap](07-roadmap.md)).

---

## F-003 — wgpu-native diverges on mapAsync validation (aborts + escapes error scope)

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu or Dawn.
- **Found by:** `webgpu:api,validation,buffer,mapping:mapAsync,*` (Phase 3f). **yawgpu and Dawn pass
  all four ported mapAsync tests with the exact same harness code; wgpu-native diverges on 3 of 4**,
  which is what isolates the behavior to the backend.
- **Observed on wgpu-native:**
  - `mapAsync,usage` — **aborts** (panic / `signal 6`) on an invalid map usage (mapping a buffer
    that lacks `MAP_READ`/`MAP_WRITE`), instead of returning a non-success `WGPUMapAsyncStatus`
    (same eager-panic class as F-001/F-002).
  - `mapAsync,state,mapped` and `mapAsync,state,mappedAtCreation` — mapping an already-mapped buffer
    produces an **uncaptured** device validation error (it escapes the `Validation` error scope the
    test pushes around the call), so the harness's uncaptured-error routing fails the case. yawgpu
    and Dawn keep that validation error **inside** the scope, so the same code passes.
- **Expected (WebGPU):** invalid mapAsync → a validation error observable by an active error scope
  (and a rejected map), never a process abort or an out-of-scope uncaptured error.
- **Status:** open; tracked as a **wgpu-native defect** (3-way confirmed). Not masked; recorded in
  `expectations/wgpu-native.txt` (the `usage` case as a contained crash, the two map-state cases as
  expected fails), so a `--isolate --expectations` run exits 0 on wgpu-native; yawgpu and Dawn need
  no entries.

---

## F-004 — wgpu-native aborts when a destroyed buffer reaches queue submit

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu or Dawn.
- **Found by:** `clearBuffer:buffer_state` and `copyBufferToBuffer:buffer_state` (Phase 3g), the
  `bufferState == destroyed` cases. **yawgpu and Dawn pass all 14 resource-state cases with the same
  harness code; wgpu-native aborts on the 4 destroyed-buffer cases**, isolating it to the backend.
- **Observed on wgpu-native:** recording a command that uses a *destroyed* buffer and then calling
  `wgpuQueueSubmit` **aborts** (panic), instead of producing a submit-time validation error. (Same
  eager-panic class as F-001/F-002; the encode + `finish()` succeed, the abort is at submit.)
  *Invalid/error buffers* (the `invalid` state, `getErrorBuffer`) did **not** crash any backend.
- **Expected (WebGPU):** submitting a command buffer that references a destroyed resource is a
  validation error at `queue.submit`, not a process abort. yawgpu and Dawn do this.
- **Status:** open; tracked as a **wgpu-native defect** (3-way confirmed). Not masked; the four cases
  are in `expectations/wgpu-native.txt`, so a `--isolate --expectations` run exits 0.

---

## F-005 — yawgpu mishandles several valid uncompressed texture formats

- **Backend:** yawgpu (`55ac04d`). **Not** present in wgpu-native or Dawn — this is the **first
  finding against yawgpu** (the primary conformance subject); F-001..F-004 were all wgpu-native.
- **Found by:** `webgpu:api,validation,createTexture:dimension_type_and_format_compatibility:*`
  (Texture T1). The test creates a 1×1 texture (`usage = TEXTURE_BINDING`) for every uncompressed
  format on every dimension and asserts a validation error **only** when the dimension/format pair is
  incompatible. **wgpu-native and Dawn pass all 168 non-skipped cases; yawgpu fails 32 and aborts 2**,
  isolating the behavior to yawgpu.
- **Observed on yawgpu (two sub-defects):**
  - **Rejects valid core color formats as if `Undefined`.** First seen (T1, no-feature device) for the
    8 always-core formats `R16Uint/Sint/Float`, `RG16Uint/Sint/Float`, `RGB10A2Uint`, `RGB10A2Unorm`
    (`WGPUTextureFormat` enum `7,8,9,19,20,21,29,30`). **Under T2's all-features device** (which enables
    `texture-formats-tier1`) the same bug also surfaces for the 4 tier1 formats `R16Unorm/Snorm`,
    `RG16Unorm/Snorm` (enum `5,6,17,18`) — **12 color formats total**. `createTexture` raises a
    validation error where the texture should be created successfully. The neighbouring 8-bit, 32-bit,
    RGBA16, and packed-float formats are accepted, so the gap is specific to these enum values, not a
    whole size class. The same rejection re-surfaces in
    `sampleCount,various_sampleCount_with_all_formats` (the format fails even at `sampleCount=1`).
  - **Aborts on `Depth24PlusStencil8`** (`enum 47`) when the dimension is *compatible*
    (`undefined`/`2d`): `createTexture` panics instead of succeeding → crashes in both
    `dimension_type…` and `sampleCount,various…`. For `1d`/`3d` yawgpu correctly rejects it
    (depth/stencil is incompatible with those dimensions) *before* reaching the crash path.
- **Expected (WebGPU):** all of these are valid formats; `createTexture` with `TEXTURE_BINDING` on a
  compatible dimension must **succeed** (no validation error, no abort). wgpu-native and Dawn do.
- **Also seen in (T3–T7):** the same rejections recur wherever these formats are created —
  `zero_size_and_usage` (e.g. `format=30`), `mipLevelCount,format` (`format=5,6,7,8,9,17,18,19,20,21,
  29,30`), `texture_size,*`, `texture_usage`, and `viewFormats` (where they poison every runnable
  feature-pair case, so yawgpu has **zero** passing `viewFormats` cases) — plus the `Depth24PlusStencil8`
  abort. The defect is in yawgpu's format decoding, independent of the specific createTexture test.
- **Not an ABI artifact:** the `WGPUTextureFormat` enum mapping is **byte-identical** between the
  wgpu-native and yawgpu `webgpu-headers/webgpu.h` (verified by diff), so the same `format=N` value
  denotes the same format on both — this is a genuine format-handling gap in yawgpu, not an
  enum/ABI mismatch in how the suite passes the value.
- **Status:** **RESOLVED** on yawgpu `92db062` (re-test 2026-05-31). `2667b0a` fixed the 12
  reject-as-`Undefined` color formats; `92db062` then fixed the `Depth24PlusStencil8` (`enum 47`) abort.
  All cases now pass; the lines were captured on `55ac04d` and removed from `expectations/yawgpu.txt`.
  wgpu-native and Dawn always passed.

---

## F-006 — yawgpu disagrees on which texture formats are multisampleable

- **Backend:** yawgpu (`55ac04d`). **Not** present in wgpu-native or Dawn.
- **Found by:** `webgpu:api,validation,createTexture:sampleCount,various_sampleCount_with_all_formats:*`
  (Texture T2), which creates each format at sample counts `{0,1,2,4,8,16,32,256}` and asserts a
  validation error unless the format is single-sampled or `sampleCount==4` on a multisampleable format.
  wgpu-native and Dawn pass all non-skipped cases; yawgpu diverges on **6 formats** (separate from the
  [F-005](#f-005--yawgpu-mishandles-several-valid-uncompressed-texture-formats) format-rejection bug,
  which also surfaces here).
- **Observed on yawgpu (two opposite errors):**
  - **Rejects multisampling on formats the spec marks multisampleable.** With the all-features device
    enabling `texture-formats-tier1` (and `rg11b10ufloat-renderable`), the tier1-blendable formats
    `R8Snorm`, `RG8Snorm`, `RGBA8Snorm` (enum `2,11,24`) and `RG11B10Ufloat` (enum `31`) are
    4×-multisampleable, so `createTexture(sampleCount=4)` should **succeed** — yawgpu raises a
    validation error.
  - **Accepts multisampling on formats that are not multisampleable.** `R32Uint`, `R32Sint` (enum
    `15,16`) are single-sample-only, so `createTexture(sampleCount=4)` must be a **validation error** —
    yawgpu creates the texture without error (too permissive).
- **Expected (WebGPU):** multisample support follows the format's capability (gated by
  `texture-formats-tier1` / `rg11b10ufloat-renderable` for the tier1 set); `R32Uint/Sint` are never
  multisampleable. wgpu-native and Dawn agree with the spec.
- **Status:** **resolved** on yawgpu `2667b0a` (re-test 2026-05-31). All 6 formats now validate
  multisampling per spec — the 36 `sampleCount` cases for this finding are `xpass` (the WIP `e39f57f`
  had fixed only `RG11B10Ufloat`; `2667b0a` fixed the tier1-blendable `R8/RG8/RGBA8Snorm` and the
  too-permissive `R32Uint/Sint` as well). Originally captured on `55ac04d`; drop these lines from
  `expectations/yawgpu.txt` when re-baselining. wgpu-native and Dawn always passed.

---

## F-007 — wgpu-native aborts on bogus and transient texture-usage bits

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu or Dawn. Same
  eager-panic class as [F-001](#f-001--wgpu-native-aborts-on-an-invalid-buffer-usage-bit) (which was the
  *buffer* analog).
- **Found by:** `webgpu:api,validation,createTexture:{usage,new_usages}` (Texture T5). **yawgpu and Dawn
  handle the same inputs without aborting; wgpu-native aborts on 16 cases**, isolating it to the backend.
- **Observed on wgpu-native — two abort triggers in `createTexture` usage handling:**
  - **Bogus usage bit** `kSomeBogusTextureUsage = 0x40000000` (the 8 `usage` cases where it appears):
    wgpu-native **panics** instead of returning a validation error — identical to F-001's bogus
    *buffer* usage.
  - **`TransientAttachment` (0x20)** in any combination (7 `usage` cases + the 1 `new_usages` case
    `usage = 0x30`): wgpu-native **panics**, including on the **valid** `RenderAttachment |
    TransientAttachment` combination that yawgpu and Dawn create successfully — so wgpu-native cannot
    create a transient-attachment texture at all here.
- **Expected (WebGPU):** an out-of-range usage bit, or an invalid transient combination, is a
  **validation error**; a valid `RENDER|TRANSIENT` texture should be **created**. Never a process abort.
- **Status:** open; tracked as a **wgpu-native defect** (3-way confirmed). Not masked; the 16 cases are
  in `expectations/wgpu-native.txt` (contained crashes), so a `--isolate --expectations` run over
  `createTexture:*` exits 0; yawgpu and Dawn need no entries.
- **Pervasive (T6):** `createTexture:texture_usage` exercises transient/storage usage on every
  format/dimension, so **every one of its 306 compatible cases crashes wgpu-native** (0 pass; the only
  non-crashes are 24 feature-skips). Dawn passes all 330. Because the whole test is unusable on
  wgpu-native, it is recorded with a single **prefix expectation**
  `…:texture_usage:*` rather than 306 lines (see the wildcard expectation support added alongside T6).

---

## F-008 — yawgpu under-validates transient texture-usage combinations

- **Backend:** yawgpu (`55ac04d`). **Not** present in wgpu-native or Dawn.
- **Found by:** `webgpu:api,validation,createTexture:usage` (Texture T5). wgpu-native (where these
  inputs abort, see F-007) aside, **Dawn rejects the invalid combinations and yawgpu does not** —
  isolating the gap to yawgpu's validation.
- **Observed on yawgpu:** the **6** invalid combinations that include `TransientAttachment` but are not
  exactly `RenderAttachment | TransientAttachment` — i.e. transient alone and transient combined with
  `CopySrc` / `CopyDst` / `TextureBinding` / `StorageBinding` — are **accepted without a validation
  error**. (yawgpu does correctly accept the *valid* `RENDER|TRANSIENT` combination.)
- **Expected (WebGPU):** `TransientAttachment` is only valid together with `RenderAttachment` (and no
  other usage); every other transient combination must raise a validation error, as Dawn does.
- **Status:** **resolved** on yawgpu `2667b0a` (re-test 2026-05-31) — the 6 invalid transient
  combinations now correctly raise a validation error (`xpass`). Originally captured on `55ac04d`; drop
  these lines from `expectations/yawgpu.txt` when re-baselining. wgpu-native and Dawn always passed.

---

## F-009 — yawgpu over-restricts render-attachment dimension and under-validates storage usage

- **Backend:** yawgpu (`55ac04d`). **Not** present in wgpu-native (which aborts these inputs, F-007) or
  Dawn.
- **Found by:** `webgpu:api,validation,createTexture:texture_usage` (Texture T6) — usage flags × every
  format × every dimension. **Dawn passes all 330 cases (the reference for the success model); yawgpu
  fails 111 + crashes 2**, isolating the gaps to yawgpu. (The 2 crashes are the
  [F-005](#f-005--yawgpu-mishandles-several-valid-uncompressed-texture-formats) `Depth24PlusStencil8`
  abort; many fails are the F-005 format rejections recurring. The two divergences below are the new,
  usage-specific ones.)
- **Observed on yawgpu (two usage-validation gaps):**
  - **Render-attachment on 3D textures is wrongly rejected.** Per spec, `RENDER_ATTACHMENT` is invalid
    only for **1D** textures (a 3D texture may be created with it and rendered to per-slice). yawgpu
    rejects `RENDER_ATTACHMENT` on **3D** textures (e.g. `R8Unorm` 3D), which Dawn accepts — the 3D
    dimension has ~18 more failing cases than the others (3D=42 vs ~24).
  - **Storage usage validation gaps.** yawgpu rejects `STORAGE_BINDING` on some tier1 storage-capable
    formats that the all-features fixture enables (and that Dawn accepts), and/or accepts/rejects
    storage on formats inconsistently with the spec's `isTextureFormatUsableAsWriteOnlyStorageTexture`.
- **Expected (WebGPU):** render-attachment is dimension-invalid only for 1D; storage-binding validity
  follows the format's storage capability (with `texture-formats-tier1` enabled on the all-features
  device). Dawn matches.
- **Status:** **RESOLVED** on yawgpu `92db062` (re-test 2026-05-31). `2667b0a` fixed the
  3D-render-attachment over-restriction; `92db062` then fixed the `RGBA8Snorm` (`enum 24`) tier1-storage
  rejection. All cases now pass; the lines were captured on `55ac04d` and removed from
  `expectations/yawgpu.txt`. wgpu-native and Dawn always passed.

---

## F-010 — yawgpu's newly-enabled compressed / feature-gated formats have validation gaps

- **Backend:** yawgpu (`e39f57f`, 2026-05-31 — **new** in this build; on the `55ac04d` baseline these
  formats were feature-skipped, so the gaps were not yet observable). **Not** present in wgpu-native or
  Dawn.
- **Found by:** re-running `api,validation:*` after yawgpu added the `texture-compression-bc/etc2/astc`,
  `depth32float-stencil8`, and `texture-formats-tier1` features (skips collapsed from several hundred to
  16). The newly-running cases surface validation gaps that Dawn handles correctly. The WIP `e39f57f`
  build showed ~190 such fails + 4 crashes (compressed `viewFormats`, ETC2-in-3D, etc.); **`2667b0a`
  fixed those and the crashes**, leaving the gap below.
- **Observed on yawgpu `2667b0a` — compressed `texture_size` block alignment is not validated:**
  `texture_size,2d_texture,compressed_format` (104) and `texture_size,3d_texture,compressed_format`
  (42) — **146** cases — yawgpu accepts compressed textures whose width/height is **not a multiple of the
  format block size** (or exceeds the dimension limit). Dawn rejects these.
- **Expected (WebGPU):** a compressed texture's width/height must be a multiple of its block dimensions
  and within the dimension limits. Dawn enforces this.
- **Status:** **RESOLVED** on yawgpu `92db062` (re-test 2026-05-31) — yawgpu now validates compressed
  texture block alignment and size limits; all 146 cases pass. This finding was surfaced *and* fixed
  within the re-test cycle once yawgpu enabled compression. wgpu-native (skips ETC2/ASTC differently) and
  Dawn always passed; removed from `expectations/yawgpu.txt`.

---

_Add new findings as `F-00N` with the same fields._
