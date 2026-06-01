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
`expectations/yawgpu.txt` now has no expected-failure lines. The same `pass=2594 skip=16 fail=0 crash=0`
result is confirmed on **Windows/Vulkan** (NVIDIA), via `--features vulkan` and the `--isolate`
`CreateProcess` path.

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
[F-010](#f-010--yawgpus-newly-enabled-compressed--feature-gated-formats-have-validation-gaps) — that
this suite had surfaced **as of `92db062`** is now **resolved** (see each entry's Status). This is the
full intended cycle: the suite reports a divergence, it is fixed in yawgpu, and the fix is confirmed on
real hardware. Tests ported **after** that milestone continue to find new ones — `createView` (T9)
surfaced [F-011](#f-011--yawgpu-createview-view-dimension-gaps-2d-multilayer-cube-cube-array-square)
(yawgpu) and [F-012](#f-012--wgpu-native-rejects-createview-on-a-destroyed-texture) (wgpu-native). yawgpu
fixed F-011 in `41e007b`. The next slice (T10, `createView` `array_layers`/`mip_levels`) then surfaced
[F-013](#f-013--wgpu-native-aborts-on-createview-layerlevel-range-validation) (wgpu-native) and
[F-014](#f-014--yawgpu-under-validates-3d-texture-view-array-layer-ranges) (yawgpu). The **final**
createView slice (T11 — the three `texture_view_usage` tests) **completes `createView` 10/10** and
surfaced [F-015](#f-015--wgpu-native-does-not-enforce-the-createview-view-usage-subset-rule)
(wgpu-native does not enforce the view-usage subset rule); **yawgpu passes all of T11** (it correctly
enforces the subset rule, identical to Dawn), so T11 added **no** yawgpu finding. **yawgpu then fixed
F-014 in `baa78cb`**. The first `createBindGroupLayout` slice (T13 — the `visibility` group, which
builds the BGL binding-entry taxonomy) then surfaced
[F-016](#f-016--yawgpu-rejects-read-write-storage-textures-on-read-write-capable-formats) (yawgpu rejects
read-write storage textures on the core `r32*` read-write formats) and
[F-017](#f-017--wgpu-native-aborts-on-storage-texture-bindgrouplayout-entries) (wgpu-native aborts on
storage-texture BGL entries). **yawgpu fixed F-016 in `4292f76`** — so once again **yawgpu passes every
ported `api,validation` test** (`pass=4131 skip=200 fail=0 crash=0`, identical to Dawn) with no
expected-failure lines, confirmed over the full 4331-case surface on **both** real-GPU Metal and
Windows/Vulkan (NVIDIA RTX 5060 Ti) — the same `pass=4131 skip=200` on each. The second
`createBindGroupLayout` slice (T14 — `storage_texture` + `multisampled`) then surfaced
[F-018](#f-018--yawgpu-over-restricts-bindgrouplayout-storage-texture-bindings) (yawgpu over-restricts
BGL storage-texture bindings — 1D view dimension + `rgba8snorm` format) and
[F-019](#f-019--wgpu-native-aborts-on-an-undefined-view-dimension-in-a-bindgrouplayout-entry)
(wgpu-native aborts on an undefined BGL view dimension). **yawgpu fixed F-018 in `925520a`**. The
createPipelineLayout T18 slice then surfaced
[F-020](#f-020--yawgpu-rejects-null-bind-group-layout-slots-in-createpipelinelayout) (yawgpu doesn't yet
implement null bind-group-layout slots) and
[F-021](#f-021--wgpu-native-aborts-on-null-bind-group-layout-slots-in-createpipelinelayout) (wgpu-native
aborts on them). **yawgpu fixed F-020 in `f75fc0a`** — so again **yawgpu passes every ported
`api,validation` test** (`pass=4307 skip=377 fail=0 crash=0`; it runs the 8 `immediate_data_size` cases
Dawn skips). The shader/pipeline/pass-foundation slice (T21 — the two `…_pipeline_with_null` tests,
which complete `createPipelineLayout`) then surfaced
[F-022](#f-022--yawgpu-does-not-defer-minbindingsize-validation-rejects-minbindingsize--0-at-pipeline-creation)
(yawgpu rejects `minBindingSize = 0` at pipeline creation instead of deferring) and extended F-021
(wgpu-native aborts on null BGL in pipeline creation/use). **yawgpu fixed F-022 in `798fc6a`** — so
again **yawgpu passes every ported `api,validation` test** (`pass=4332 skip=383 fail=0 crash=0`; it runs
the 8 `immediate_data_size` cases Dawn skips). Opening **`api/operation` (Phase 4, T22)** then surfaced
the first **execution** finding,
[F-023](#f-023--yawgpu-aborts-on-a-0-size-clearbuffer--copybuffertobuffer-un-ended-metal-blit-encoder)
(yawgpu aborts on a 0-size buffer clear/copy — an un-ended Metal blit encoder). The cycle continues.
**Resolved yawgpu findings: F-005/006/008/009/010/011/014/016/018/020/022. Open yawgpu findings: F-023
(api/operation). yawgpu still passes every ported `api,validation` test. Open wgpu-native: F-001–F-004,
F-007, F-012, F-013, F-015, F-017, F-019, F-021.**

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
- **Update (T22 — api/operation).** The operation test `api,operation,command_buffer,clearBuffer:clear`
  also hits this `src/lib.rs:1294` panic — its `size=0` subcase (a valid no-op clear) makes wgpu-native
  treat the size as invalid and abort, where Dawn and yawgpu accept it. Recorded as a
  `api,operation,command_buffer,clearBuffer:clear:*` prefix.

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
- **Update (T19 — buffer/mapping completed).** The same divergence pervades the rest of the mapping
  surface: across `getMappedRange,*` and `unmap,state,*`, **Dawn and yawgpu pass all 26 cases (identical),
  while wgpu-native fails 13 + crashes 7** — a mix of aborts (`signal 6`, e.g. `getMappedRange,state,unmapped`)
  and uncaptured validation errors (e.g. `unmap,state,destroyed`). Recorded as 15 prefix lines in
  `expectations/wgpu-native.txt`. Same root class; **no yawgpu finding** — yawgpu matches Dawn on the whole
  completed mapping file.

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

## F-011 — yawgpu createView view-dimension gaps (2D-multilayer, cube, cube-array square)

- **Backend:** yawgpu (`92db062`). **Not** present in wgpu-native or Dawn.
- **Found by:** `webgpu:api,validation,createView:{dimension,cube_faces_square}` (Texture T9). **Dawn
  passes all 36 of these cases (the reference) and so does wgpu-native; yawgpu fails 12**, isolating the
  gaps to yawgpu.
- **Observed on yawgpu (three distinct view-dimension defects):**
  - **Rejects a `2D` view of a 2D texture with >1 array layer.** Creating a `2D` view (which defaults to
    `arrayLayerCount = 1`, viewing layer 0) of a 2D texture that has multiple layers should **succeed**;
    yawgpu raises a validation error (it does not apply the default-single-layer rule). (`dimension`
    `textureDimension=2d;viewDimension=2d`; every `cube_faces_square` `viewDimension=2d` control case.)
  - **Rejects `Cube` views outright.** A `Cube` view of a square 2D texture with 6 layers should
    **succeed**; yawgpu rejects it (cube view dimension appears unsupported). (`dimension`
    `…;viewDimension=cube`; `cube_faces_square` square `cube` cases `4×4`, `5×5`.)
  - **Does not enforce the square-face requirement for `CubeArray`.** A cube/cube-array view requires
    `width == height`; yawgpu **accepts** non-square `CubeArray` views that should be a validation error.
    (`cube_faces_square` non-square `cube-array` cases `4×5`, `4×8`, `8×4`.)
- **Expected (WebGPU):** a `2D` view of a multi-layer texture is valid (single-layer default); `Cube`
  views are supported; cube/cube-array faces must be square. Dawn enforces all of this.
- **Status:** **RESOLVED** on yawgpu `41e007b` (re-test 2026-05-31) — *"cts-findings: fix createView
  view-dimension gaps (F-011)"*. All 12 cases now pass (`xpass`); `dimension` and `cube_faces_square`
  are 21/15 green, and yawgpu again passes **every** ported `api,validation` test
  (`pass=2970 skip=16 fail=0 crash=0`). Removed from `expectations/yawgpu.txt`. wgpu-native and Dawn
  always passed.

---

## F-012 — wgpu-native rejects createView on a destroyed texture

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu or Dawn.
- **Found by:** `webgpu:api,validation,createView:texture_state` (Texture T9), which creates a view on a
  `valid` / `invalid` / `destroyed` texture. **yawgpu and Dawn pass (destroyed → success, invalid →
  error); wgpu-native fails** the destroyed case.
- **Observed on wgpu-native:** `createView` on a **destroyed** texture raises a validation error.
- **Expected (WebGPU):** `createView` on a *destroyed* texture **succeeds** (the view is created; using
  it later is the error); only an *invalid* (error) texture makes `createView` fail. Dawn and yawgpu do
  this.
- **Status:** open; tracked as a **wgpu-native defect** (3-way confirmed). Not masked; recorded in
  `expectations/wgpu-native.txt` as a `createView:texture_state:*` prefix line (the test is a single
  case); yawgpu and Dawn need no entries.

---

## F-013 — wgpu-native aborts on createView layer/level range validation

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu or Dawn. Same eager-panic
  class as [F-001](#f-001--wgpu-native-aborts-on-an-invalid-buffer-usage-bit)/[F-007](#f-007--wgpu-native-aborts-on-bogus-and-transient-texture-usage-bits).
- **Found by:** `webgpu:api,validation,createView:{array_layers,mip_levels}` (Texture T10), which vary the
  view's `baseArrayLayer`/`arrayLayerCount`/`baseMipLevel`/`mipLevelCount` across in- and out-of-range
  values. **Dawn passes all 18 cases (the reference) and yawgpu nearly so; wgpu-native crashes all 18**.
- **Observed on wgpu-native:** an out-of-range mip/array view range makes `createView` **panic and abort**
  the process instead of returning a validation error. Under `--isolate` every `array_layers`/`mip_levels`
  case crashes (each case's subcases include an out-of-range value that triggers the abort).
- **Expected (WebGPU):** an out-of-range view (`baseMipLevel + mipLevelCount > texture levels`, a wrong
  per-dimension `arrayLayerCount`, etc.) is a **validation error**, never a process abort. Dawn and yawgpu
  do this.
- **Status:** open; tracked as a **wgpu-native defect** (3-way confirmed). Not masked; recorded in
  `expectations/wgpu-native.txt` as `createView:array_layers:*` + `createView:mip_levels:*` prefix lines
  (the whole tests crash); yawgpu and Dawn need no entries.

---

## F-014 — yawgpu under-validates 3D-texture view array-layer ranges

- **Backend:** yawgpu (found on `41e007b`). **Not** present in wgpu-native (which aborts, F-013) or Dawn.
- **Found by:** `webgpu:api,validation,createView:array_layers` (Texture T10). **Dawn passes all 9 cases
  (the reference); yawgpu fails 2** — the 3D-texture cases (`textureDimension=3d`,
  `viewDimension=undefined` and `=3d`).
- **Observed on yawgpu:** for a **3D** texture (which has exactly one array layer), yawgpu **accepts**
  views with an out-of-range `baseArrayLayer`/`arrayLayerCount` (e.g. `arrayLayerCount != 1` or
  `baseArrayLayer + arrayLayerCount > 1`) that should be a validation error. (yawgpu validates the 1D/2D
  array-layer cases correctly — only the 3D view path under-validates.)
- **Expected (WebGPU):** a 3D-texture view must have `arrayLayerCount == 1` and stay within the single
  layer; an out-of-range layer range is a validation error. Dawn enforces this.
- **Status:** **RESOLVED** on yawgpu `baa78cb` (re-test 2026-05-31) — *"cts-findings: validate
  3D-texture view array-layer ranges (F-014)"*. yawgpu now enforces `arrayLayerCount == 1` and the layer
  range for 3D-texture views; both `array_layers` 3D cases pass and the full ported suite is clean
  (`pass=3777 skip=200 fail=0 crash=0`). The two lines were removed from `expectations/yawgpu.txt`.

---

## F-015 — wgpu-native does not enforce the createView view-usage subset rule

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu or Dawn (both enforce it).
  Unlike [F-013](#f-013--wgpu-native-aborts-on-createview-layerlevel-range-validation) this is a
  **missing-validation** gap, not an abort.
- **Found by:** `webgpu:api,validation,createView:texture_view_usage` (Texture T11), which sets the view's
  `usage` to each texture-usage bit and checks the subset rule. **Dawn passes 391 / skips 61 (the
  reference) and yawgpu is identical (clean); wgpu-native fails 324 of 452** (`pass=16 skip=112 fail=324`
  under `--isolate`).
- **Observed on wgpu-native:** when a view requests a `usage` bit the texture does **not** have,
  `createView` returns **no validation error** — all 324 failures are *"expected validation error, got
  none."* wgpu-native does not validate that the view usage is a subset of the texture usage.
- **Expected (WebGPU):** a texture view's `usage` must be a subset of the texture's usage; a superset is a
  validation error. Dawn and yawgpu both enforce this.
- **Status:** open; tracked as a **wgpu-native defect** (3-way confirmed). Not masked; recorded in
  `expectations/wgpu-native.txt` as a `createView:texture_view_usage:*` prefix line (yields ~16 `xpass`
  for the cases that need no error — acceptable for the bring-up reference). yawgpu and Dawn need no
  entries.

> **Scope note (TRANSIENT_ATTACHMENT).** T11's three `texture_view_usage` tests include one
> `TRANSIENT_ATTACHMENT` case. `TRANSIENT_ATTACHMENT` is a non-standard native extension; upstream gates
> every transient case behind `skipIfTransientAttachmentNotSupported` (skipped in standard environments).
> This port treats it as **out of conformance scope** — `skipIfTransientAttachmentNotSupported()` skips
> it on all backends — so it is not asserted cross-backend. (It is why the otherwise-clean run shows one
> `skip` in `texture_view_usage_of_multiple_usages`.)

---

## F-016 — yawgpu rejects read-write storage textures on read-write-capable formats

- **Backend:** yawgpu (`baa78cb`). **Not** present in Dawn (accepts) or wgpu-native (which aborts, F-017).
- **Found by:** `webgpu:api,validation,createBindGroupLayout:{visibility,visibility,VERTEX_shader_stage_storage_texture_access}`
  (BGL T13). A `storageTexture` BGL entry with `access: 'read-write'` and a read-write-capable format
  (`r32float` in `visibility`, `r32uint` in the access test). **Dawn accepts all 8 cases (the reference);
  yawgpu fails 4 of each** (the 4 visibilities/stages without VERTEX, where the entry should be valid).
- **Observed on yawgpu:** creating the BGL raises *"storage texture binding format must support
  read-write storage access"* for `r32float`/`r32uint` — formats that **do** support read-write storage.
  yawgpu's read-write-capable format set is missing the core `r32uint`/`r32sint`/`r32float`.
- **Expected (WebGPU):** `r32uint`, `r32sint`, `r32float` support `read-write` (and `read-only`) storage
  access with no feature gate (`kTextureFormatInfo[f].color.readWriteStorage`). Dawn enforces this.
- **Status:** **RESOLVED** on yawgpu `4292f76` (re-test 2026-06-01) — *"cts-findings: r32uint/r32sint/
  r32float support read-write storage (F-016)"*. yawgpu now accepts read-write storage textures on the
  core `r32*` formats; all 16 cases pass and the full ported suite is clean again
  (`pass=4131 skip=200 fail=0 crash=0`, identical to Dawn). The 8 lines were removed from
  `expectations/yawgpu.txt`.

---

## F-017 — wgpu-native aborts on storage-texture BindGroupLayout entries

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu or Dawn. Same eager-panic
  class as [F-001](#f-001--wgpu-native-aborts-on-an-invalid-buffer-usage-bit)/[F-007](#f-007--wgpu-native-aborts-on-bogus-and-transient-texture-usage-bits)/[F-013](#f-013--wgpu-native-aborts-on-createview-layerlevel-range-validation).
- **Found by:** `webgpu:api,validation,createBindGroupLayout:{visibility,visibility,VERTEX_shader_stage_storage_texture_access}`
  (BGL T13). **Dawn passes all 8 cases of each (the reference); wgpu-native crashes all 8** under
  `--isolate`.
- **Observed on wgpu-native:** a `storageTexture` BGL entry makes `createBindGroupLayout` **panic and
  abort** (`src/conv.rs` storage-texture conversion) instead of returning a validation error. Every
  `visibility` / storage-access case includes a storage-texture entry, so the whole tests crash.
- **Expected (WebGPU):** an invalid storage-texture binding is a **validation error**, never a process
  abort; a valid one succeeds. Dawn and yawgpu do not abort.
- **Status:** open; tracked as a **wgpu-native defect** (3-way confirmed). Not masked; recorded in
  `expectations/wgpu-native.txt` as `createBindGroupLayout:visibility:*` +
  `createBindGroupLayout:visibility,VERTEX_shader_stage_storage_texture_access:*` prefix lines.
  (`visibility,VERTEX_shader_stage_buffer_type` has no storage entry and passes on all three.)
- **Update (T14):** the BGL `storage_texture,formats` test (storage-texture entries for every format ×
  access) likewise crashes every non-skipped wgpu-native case (126/126), confirming the same defect;
  recorded as a `createBindGroupLayout:storage_texture,formats:*` prefix.

---

## F-018 — yawgpu over-restricts BindGroupLayout storage-texture bindings

- **Backend:** yawgpu (`4292f76`). **Not** present in Dawn (accepts both) or wgpu-native (which aborts,
  F-017/F-019). Two distinct over-restrictions, both surfaced by BGL T14:
  - **1D view dimension** — `storage_texture,layout_dimension:viewDimension=1d`: yawgpu raises *"storage
    texture bindings must not use 1D view dimension"*. **Dawn accepts** (1D is a valid storage view
    dimension — only `cube`/`cube-array` are disallowed; WGSL has `texture_storage_1d`).
  - **`rgba8snorm` format** — `storage_texture,formats:format=rgba8snorm` with `write-only` and
    `read-only` access: yawgpu raises *"storage texture binding format must support storage usage"*.
    `rgba8snorm` is a **core** writable/readable storage format (no feature gate); **Dawn accepts** it,
    and it is the only base-storage format yawgpu rejects. (Same `rgba8snorm`-storage root as the
    texture-path [F-009](#f-009--yawgpu-over-restricts-render-attachment-dimension-and-under-validates-storage-usage),
    which was fixed for `createTexture` but not for the BGL `storageTexture` path.)
- **Found by:** `webgpu:api,validation,createBindGroupLayout:{storage_texture,layout_dimension,storage_texture,formats}`
  (BGL T14). **Dawn is the reference** — it passes all of `layout_dimension` (7/7) and accepts `rgba8snorm`
  storage; yawgpu fails 3 cases (1 × 1D + 2 × rgba8snorm write-only/read-only). Both backends skip the
  same 177 feature-gated `formats` cases, so this is a behavioural divergence, not a feature difference.
- **Expected (WebGPU):** a `1d` storage-texture view dimension is valid; `rgba8snorm` supports write-only
  and read-only storage with no feature. Dawn enforces both.
- **Status:** **RESOLVED** on yawgpu `925520a` (re-test 2026-06-01) — *"cts-findings: BGL storage-texture
  1D view dim + rgba8snorm base storage (F-018)"*. yawgpu now accepts the 1D storage view dimension and
  `rgba8snorm` storage on the BGL path; all 3 cases pass and the full ported suite is clean again
  (`pass=4271 skip=377 fail=0 crash=0`, identical to Dawn). The 3 lines were removed from
  `expectations/yawgpu.txt`.

---

## F-019 — wgpu-native aborts on an undefined view dimension in a BindGroupLayout entry

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu or Dawn. Same eager-panic
  class as [F-017](#f-017--wgpu-native-aborts-on-storage-texture-bindgrouplayout-entries) (`src/conv.rs:1669`).
- **Found by:** `webgpu:api,validation,createBindGroupLayout:{multisampled_validation,storage_texture,layout_dimension}`
  (BGL T14), the `viewDimension=undefined` cases. **Dawn passes both (the reference); wgpu-native crashes**
  (1 each).
- **Observed on wgpu-native:** a BGL `texture`/`storageTexture` entry with an **omitted** `viewDimension`
  makes `createBindGroupLayout` **panic and abort** (`src/conv.rs:1669`) instead of applying the default.
  (Defined view dimensions do **not** crash — wgpu-native passes the other 6 `layout_dimension` cases.)
- **Expected (WebGPU):** an omitted `viewDimension` defaults (to `2d`), never aborts. Dawn and yawgpu
  default it correctly.
- **Status:** open; tracked as a **wgpu-native defect** (3-way confirmed). Not masked; recorded in
  `expectations/wgpu-native.txt` as the two `…:viewDimension=_undef_` exact lines.
- **Update (T16):** the same `src/conv.rs:1669` panic site is hit by `max_resources_per_stage,*` for
  **sampler / sampled-texture / storage-texture** `maxedEntry` cases (8 per test × 2 tests) — wgpu-native
  cannot build a near-limit count of those binding types, while it handles the buffer cases. **Dawn and
  yawgpu pass all 11 of each (yawgpu correctly enforces the per-stage limits).** So `conv.rs:1669` is a
  broader BGL-entry-conversion abort than just the undefined view dimension; recorded as 16 exact
  `max_resources_per_stage,{in_bind_group_layout,in_pipeline_layout}:maxedEntry=…` lines.

---

## F-020 — yawgpu rejects null bind-group-layout slots in createPipelineLayout

- **Backend:** yawgpu (`925520a`). **Not** present in Dawn (accepts) or wgpu-native (which aborts, F-021).
  This is the **"null bind group layouts"** feature — a pipeline layout may have null (unused) BGL slots.
- **Found by:** `webgpu:api,validation,createPipelineLayout:bind_group_layouts,null_bind_group_layouts`
  (createPipelineLayout T18). The test builds a pipeline layout with exactly one `null`/`undefined`/`empty`
  slot among 1–4 BGLs and asserts it is **valid**. **Dawn passes (the reference); yawgpu fails** the
  `null`/`undefined` subcases (it accepts the `empty`-BGL subcases).
- **Observed on yawgpu:** `createPipelineLayout` with a `NULL` `WGPUBindGroupLayout` element raises
  *"pipeline layout bindGroupLayouts elements must not be null"* — yawgpu does not implement null BGL
  slots. (In C, the test's `Null` and `Undefined` param values both map to a `NULL` handle.)
- **Expected (WebGPU):** a null bind-group-layout slot in a pipeline layout is valid (the slot is unused).
  Dawn enforces this.
- **Status:** **RESOLVED** on yawgpu `f75fc0a` (re-test 2026-06-01) — *"cts-findings: implement null
  bind-group-layout slots in createPipelineLayout (F-020)"*. yawgpu now accepts null/undefined BGL slots;
  all 30 subcases pass and the full ported suite is clean again (`pass=4307 skip=377 fail=0 crash=0`).
  The line was removed from `expectations/yawgpu.txt`.

---

## F-021 — wgpu-native aborts on null bind-group-layout slots in createPipelineLayout

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`). **Not** present in yawgpu (which rejects gracefully,
  F-020) or Dawn. Eager-panic class (`src/conv.rs:506`), same family as
  [F-001](#f-001--wgpu-native-aborts-on-an-invalid-buffer-usage-bit)/[F-017](#f-017--wgpu-native-aborts-on-storage-texture-bindgrouplayout-entries)/[F-019](#f-019--wgpu-native-aborts-on-an-undefined-view-dimension-in-a-bindgrouplayout-entry).
- **Found by:** `webgpu:api,validation,createPipelineLayout:bind_group_layouts,null_bind_group_layouts`
  (createPipelineLayout T18). **Dawn passes (the reference); wgpu-native crashes** the whole test.
- **Observed on wgpu-native:** a `NULL` `WGPUBindGroupLayout` element makes `createPipelineLayout`
  **panic and abort** (`src/conv.rs:506`) instead of accepting it (or returning a validation error).
- **Expected (WebGPU):** a null BGL slot is valid, never a process abort. Dawn accepts it.
- **Status:** open; tracked as a **wgpu-native defect** (3-way confirmed). Not masked; recorded in
  `expectations/wgpu-native.txt` as a `createPipelineLayout:bind_group_layouts,null_bind_group_layouts:*`
  prefix line.
- **Update (T21).** The same `src/conv.rs:506` abort fires when a null BGL slot flows into **pipeline
  creation/use** — `createPipelineLayout:{create,set}_pipeline_with_null_bind_group_layouts` crash
  wgpu-native (Dawn passes both). Recorded as two more `:*` prefix lines.

> **Note (immediate data).** T18's `immediate_data_size` test runs **only on yawgpu** — yawgpu reports
> `maxImmediateSize=64` (immediate data supported) while Dawn and wgpu-native report `0` and skip. yawgpu
> passes all 8 cases (it validates the `% 4` / `<= maxImmediateSize` rules correctly), so it is **not** a
> finding — yawgpu is simply ahead of Dawn/wgpu-native on this feature, with no cross-backend oracle here.

---

## F-022 — yawgpu does not defer `minBindingSize` validation (rejects `minBindingSize = 0` at pipeline creation)

- **Backend:** yawgpu (`f75fc0a`). **Not** present in Dawn (defers, accepts) or wgpu-native (which aborts
  on the null BGL first, F-021).
- **Found by:** `webgpu:api,validation,createPipelineLayout:{create_pipeline_with_null_bind_group_layouts,
  set_pipeline_with_null_bind_group_layouts}` (T21). The BGLs use `buffer{type:uniform}` with
  `minBindingSize` unset (= **0**, the default); the shader declares `var<uniform> input : u32` (4 bytes).
  **Dawn passes both tests (the reference); yawgpu fails both.**
- **Observed on yawgpu:** `createRenderPipeline`/`createComputePipeline` raises *"compute pipeline layout
  buffer minBindingSize is too small"* — yawgpu compares the BGL's `minBindingSize` (0) against the
  shader's required size (4) and rejects at **pipeline-creation** time. (`set_pipeline` then fails at
  submit — *"queue submit cannot use an error command buffer"* — a downstream consequence of the failed
  pipeline.)
- **Expected (WebGPU):** `minBindingSize = 0` means *unspecified* — the size check is **deferred to bind
  time** (the bound buffer range must be large enough), and pipeline creation must **not** reject it. Dawn
  implements this; our BGL deliberately leaves `minBindingSize` at its `INIT` default of 0.
- **Status:** **RESOLVED** on yawgpu `798fc6a` (re-test 2026-06-01) — *"cts-findings: defer
  minBindingSize=0 to bind time in createPipelineLayout compat (F-022)"*. yawgpu now defers the
  `minBindingSize=0` check to bind time; both tests pass and the full ported suite is clean again
  (`pass=4332 skip=383 fail=0 crash=0`). The 2 lines were removed from `expectations/yawgpu.txt`.

---

## F-023 — yawgpu aborts on a 0-size clearBuffer / copyBufferToBuffer (un-ended Metal blit encoder)

- **Backend:** yawgpu (`798fc6a`, Metal). **Not** present in Dawn (handles 0-size ops). wgpu-native hits a
  *different* abort on the clearBuffer case (F-002); it passes the 0-size copy.
- **Found by:** the first `api/operation` tests (T22) —
  `api,operation,command_buffer,clearBuffer:clear` (its `size=0` subcase) and
  `api,operation,command_buffer,copyBufferToBuffer:single` (its `copySize=0` subcases). **Dawn passes all
  5 operation cases (the reference); yawgpu aborts the two tests that contain a 0-size op** and passes
  `state_transitions`/`copy_order` (which have no 0-size op).
- **Observed on yawgpu:** a **0-byte** `clearBuffer`/`copyBufferToBuffer` (a valid no-op) makes the Metal
  validation layer abort with *"-[_MTLCommandEncoder dealloc]: failed assertion `Command encoder released
  without endEncoding'"* — yawgpu's Metal backend creates a blit command encoder for the no-op and
  releases it without `endEncoding`. (The bug is in execution, not validation; the readback infra itself
  is sound — Dawn and the non-zero yawgpu cases pass.)
- **Expected (WebGPU):** a 0-size buffer clear/copy is a **valid no-op**, never a process abort. Dawn
  executes it cleanly.
- **Status:** open; tracked as a **yawgpu defect** (3-way confirmed). Not masked; recorded in
  `expectations/yawgpu.txt` as `api,operation,command_buffer,{clearBuffer:clear,copyBufferToBuffer:single}:*`
  prefix lines; wgpu-native and Dawn need no entries (wgpu-native's clearBuffer abort is F-002).

---

_Add new findings as `F-00N` with the same fields._
