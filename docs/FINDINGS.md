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

## Re-test summary

Every defect this suite surfaced against **yawgpu** (the primary conformance subject) was fixed in yawgpu
and re-confirmed on real hardware — `expectations/yawgpu.txt` carries no expected-failure lines (surfaced,
never masked). The early validation/copy milestones (commit + result):

| milestone | yawgpu fix(es) | result after fix |
|-----------|----------------|------------------|
| `api,validation` baseline (`55ac04d`→`92db062`) | F-005/006/008/009/010 — `2667b0a`, `92db062` | `pass=2594 skip=16 fail=0 crash=0` |
| `createView` (T9–T11) | F-011 `41e007b`, F-014 `baa78cb` | clean (F-015 is wgpu-native-only) |
| `createBindGroupLayout` (T13–T16) | F-016 `4292f76`, F-018 `925520a` | `pass=4271 skip=377 fail=0` |
| `createPipelineLayout` (T18–T21) | F-020 `f75fc0a`, F-022 `798fc6a` | `pass=4332 skip=383 fail=0` |
| `api,operation` buffer/queue (T22–T23) | F-023 `e56f30a`, F-024 `c893eac` | `command_buffer,* pass=5` (Dawn-equal) |
| `image_copy` color (T24b) | F-025, F-026 — `1e6c70b` | `image_copy pass=137256 fail=0` |
| `image_copy` cross-test leak + readback race | F-029 (`1e67300`), F-030 (`1297b7e`) | full `image_copy` repeatably `pass=137256 fail=0`; cross-test poison gone |
| `copyTextureToTexture` depth/stencil (T26) | F-031 — depth render-path support (7 gaps) `f3afc31` | `copy_depth_stencil pass=216 fail=0` (Dawn-equal, from `pass=36 fail=180`) |
| `image_copy` depth/stencil (T27) | F-032 — depth/stencil aspect buffer copies `c8f15d5`,`af9ac5c` | `image_copy` d/s `pass=1152 fail=0` (Dawn-equal, from `pass=288 fail=864`); full `image_copy pass=138408 fail=0` |

**Resolved yawgpu findings:** F-005/006/008/009/010/011/014/016/018/020/022/023/024/025/026/029/030/031/032/034/035/037/038/039/040/041/042/043/044/045/046/047/048/049/050/051/053/054/055/057/058/059/060/061/062/063/064/065/066/067/068/069/072/073/074/076/077/079/080/081
— each keeps a compact record below. The 2026-06-11 yawgpu update (`f9a076e`…`f857f3f`) fixed the eleven
findings F-064–F-069, F-072–F-074, F-076, F-077, re-verified on Metal + MoltenVK (F-068 additionally
confirmed green on native Windows/Vulkan; its 125-case MoltenVK-only residual is a translation
limitation, same class as F-033/F-045/F-053).

**Open — yawgpu: none.** The 2026-06-11 regressions F-079/F-080/F-081 were fixed the same day (yawgpu
`4770131` + `9382206`) and re-verified: `api,validation` full sweep on Metal `pass=107608 fail=0`;
F-079/F-080 also green on MoltenVK. `external_texture` on Vulkan now fails honestly with "not supported
on the Vulkan backend" — the deliberate `fa97027` limitation (previously a false pass), documented under
F-081, not an open defect.

**Open — naga lineage / wgpu-native:** **F-078** (validator treats `let`-propagated indices as
const-expression OOB → all `robust_access` compute pipelines error; Tint correct; yawgpu's earlier
"green" was a false pass exposed by the F-065 uncaptured-error wiring — NOT a yawgpu regression),
**F-070** (reduced 2026-06-11: Metal residual is `struct_inner_align` 9 + matCx3 padding 16 +
`shadow:loop`; MoltenVK still fails ~54 `memory_layout` layout cases — SPIR-V backend lacks the fix),
**F-071** (wgpu-native `zero_init` 3930 + `robust_access` aborts — same naga root as F-078), **F-075**
(wgpu-native buffer mapping broadly broken). `texture_component_swizzle` remains Dawn-only oracle
(yawgpu/wgpu-native lack the feature).

The earlier `api/validation` bulk-port findings **F-060/F-061/F-062/F-063** (all cross-HAL; Dawn passed all) are
**all fixed and re-verified on both HALs** (Metal == Vulkan/MoltenVK, 2026-06-09): `external_texture` `pass=2
fail=0` (F-060, yawgpu `fa97027`), `resource_compatibility` `pass=123 fail=0` (F-061), `render_bundle` `pass=21
fail=0` (F-062), `inter_stage` `pass=26 fail=0` (F-063).
(The validation-
bulk findings **F-057 / F-058 / F-059 are all fixed and re-verified on both HALs**: `non_filterable_texture`
`pass=160`, `depth_stencil_state` `pass=1600`, `storage_texture,format` `pass=720`. Earlier session findings
F-051 / F-053 / F-054 / F-055 are also fixed — F-051/F-054/F-055 on both HALs, F-053 on Metal + native
Vulkan with a confirmed MoltenVK-only residual.)

Every resolved finding keeps a **short** record below (one-line what + the yawgpu fix commit); the full
diagnosis is in that commit and in this file's git history. The full ported suite is green on native
Windows/Vulkan (NVIDIA RTX 5060 Ti) — all 7596 ported cases pass or skip (`pass=7208 skip=388 fail=0`,
per-**case**; the per-test `pass=…` totals in the records are per-**subcase**). The **GLES** HAL is the only
untested follow-up.
**Open — wgpu-native only:** F-001–F-004, F-007, F-012, F-013, F-015, F-017, F-019, F-021, F-027,
F-028, F-036 (abort when a constant-factor blend draws without `setBlendConstant`; `color_target_state`,
T31), **F-052** (ignores the pipeline `multisample.mask` — `sample_mask`, T59), **F-056** (aborts on a
**mixed read-only/written** depth-stencil attachment that is also sampled — over-strict per-texture
usage-conflict validation; `memory_sync/texture/readonly_depth_stencil`, T74) (full detail retained).
*(Real-GPU verification runs with the Bash sandbox disabled — see the
F-023 note; under the macOS sandbox Metal enumerates no adapters and every case false-fails.)*
**Tooling / environment (not a backend conformance defect):** F-033 — color `copyTextureToTexture`
pixel mismatches when yawgpu's Vulkan HAL is run on **Mac via MoltenVK**; a **confirmed** MoltenVK
translation artifact — native Windows/Vulkan does **not** exhibit it (`pass=7208 skip=388 fail=0`, all
7596 cases), low priority. **F-045** (`frag_depth` not viewport-clamped) and **F-053** (multi-attachment
render to different slices of one 3D texture — an explicit `vkCreateImageView` 2D-view-on-3D-image
`[mvk-error]`) are the other two **confirmed MoltenVK-only** residuals: both pass on the Metal HAL and on
native Windows/Vulkan (user-confirmed), and fail only under MoltenVK's Vulkan→Metal translation.

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

- **Backend:** yawgpu (captured `55ac04d`). Not in wgpu-native/Dawn — the first finding against yawgpu.
- **What:** `createTexture:dimension_type_and_format_compatibility` (T1) — yawgpu rejected 12 valid
  color formats (`R16`/`RG16` Uint/Sint/Float/Unorm/Snorm + `RGB10A2Uint/Unorm`; enum
  5/6/7/8/9/17/18/19/20/21/29/30) as `Undefined`, and **aborted** on `Depth24PlusStencil8` (enum 47)
  at a compatible dimension. Recurred across T2–T7 wherever those formats are created. wgpu-native/Dawn
  pass all.
- **Resolved:** yawgpu `2667b0a` (format rejection) + `92db062` (D24S8 abort), re-test 2026-05-31 —
  all cases pass; lines removed from `expectations/yawgpu.txt`.

---

## F-006 — yawgpu disagrees on which texture formats are multisampleable

- **Backend:** yawgpu (`55ac04d`). Not in wgpu-native/Dawn.
- **What:** `createTexture:sampleCount,various_sampleCount_with_all_formats` (T2) — yawgpu rejected
  `sampleCount=4` on the multisampleable tier1-blendable formats `R8/RG8/RGBA8Snorm` + `RG11B10Ufloat`
  (enum 2/11/24/31), and wrongly *accepted* it on single-sample-only `R32Uint/Sint` (enum 15/16).
- **Resolved:** yawgpu `2667b0a`, re-test 2026-05-31 — all 6 formats validate multisampling per spec.
  wgpu-native/Dawn always passed.

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

- **Backend:** yawgpu (`55ac04d`). Not in wgpu-native/Dawn.
- **What:** `createTexture:usage` (T5) — yawgpu accepted without error the 6 invalid combinations
  containing `TransientAttachment` other than exactly `RenderAttachment | TransientAttachment`
  (transient alone, or with `CopySrc`/`CopyDst`/`TextureBinding`/`StorageBinding`). Dawn rejects them.
- **Resolved:** yawgpu `2667b0a`, re-test 2026-05-31 — the 6 invalid combinations now raise a
  validation error. wgpu-native/Dawn always passed.

---

## F-009 — yawgpu over-restricts render-attachment dimension and under-validates storage usage

- **Backend:** yawgpu (`55ac04d`). Not in wgpu-native (which aborts these inputs, F-007) or Dawn.
- **What:** `createTexture:texture_usage` (T6) — yawgpu wrongly rejected `RENDER_ATTACHMENT` on **3D**
  textures (spec: invalid only for 1D) and had `STORAGE_BINDING` validation gaps on tier1
  storage-capable formats. Dawn passes all 330.
- **Resolved:** yawgpu `2667b0a` (3D render-attachment) + `92db062` (`RGBA8Snorm` tier1 storage),
  re-test 2026-05-31 — all cases pass; lines removed from `expectations/yawgpu.txt`.

---

## F-010 — yawgpu's newly-enabled compressed / feature-gated formats have validation gaps

- **Backend:** yawgpu (`e39f57f` — new once yawgpu enabled `texture-compression-*` /
  `depth32float-stencil8` / `texture-formats-tier1`; feature-skipped on `55ac04d`). Not in
  wgpu-native/Dawn.
- **What:** `createTexture:texture_size,{2d,3d}_texture,compressed_format` (146 cases) — yawgpu
  accepted compressed textures whose width/height is not a multiple of the block size (or exceeds the
  dimension limit). Dawn rejects these.
- **Resolved:** yawgpu `92db062`, re-test 2026-05-31 — block-alignment and size limits now validated;
  all 146 pass. Surfaced *and* fixed within the re-test cycle. wgpu-native/Dawn always passed.

---

## F-011 — yawgpu createView view-dimension gaps (2D-multilayer, cube, cube-array square)

- **Backend:** yawgpu (`92db062`). Not in wgpu-native/Dawn.
- **What:** `createView:{dimension,cube_faces_square}` (T9, 12 cases) — yawgpu rejected a `2D` view of a
  multi-layer 2D texture (no single-layer default), rejected `Cube` views outright, and did not enforce
  the square-face requirement for `CubeArray`. Dawn/wgpu-native pass all 36.
- **Resolved:** yawgpu `41e007b` (*"fix createView view-dimension gaps (F-011)"*), re-test 2026-05-31 —
  all 12 pass (`pass=2970 skip=16 fail=0 crash=0`). Removed from `expectations/yawgpu.txt`.

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

- **Backend:** yawgpu (found on `41e007b`). Not in wgpu-native (which aborts, F-013) or Dawn.
- **What:** `createView:array_layers` (T10) — for a **3D** texture (one array layer) yawgpu accepted
  out-of-range `baseArrayLayer`/`arrayLayerCount` (≠1) that should be a validation error. The 1D/2D
  array cases were validated correctly; Dawn passes all 9.
- **Resolved:** yawgpu `baa78cb` (*"validate 3D-texture view array-layer ranges (F-014)"*), re-test
  2026-05-31 — both 3D cases pass (`pass=3777 skip=200 fail=0 crash=0`). Removed from
  `expectations/yawgpu.txt`.

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

- **Backend:** yawgpu (`baa78cb`). Not in Dawn (accepts) or wgpu-native (which aborts, F-017).
- **What:** `createBindGroupLayout:{visibility,visibility,VERTEX_shader_stage_storage_texture_access}`
  (T13) — a `storageTexture` BGL entry with `access:'read-write'` on `r32float`/`r32uint` was rejected;
  yawgpu's read-write-capable set was missing the core `r32uint/r32sint/r32float` (no feature gate). Dawn
  accepts all 8 of each.
- **Resolved:** yawgpu `4292f76` (*"r32uint/r32sint/r32float support read-write storage (F-016)"*),
  re-test 2026-06-01 — all 16 pass (`pass=4131 skip=200 fail=0 crash=0`, identical to Dawn). Removed from
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

- **Backend:** yawgpu (`4292f76`). Not in Dawn (accepts both) or wgpu-native (which aborts, F-017/F-019).
- **What:** `createBindGroupLayout:{storage_texture,layout_dimension / storage_texture,formats}` (T14) —
  yawgpu rejected a valid `1d` storage-texture view dimension, and rejected `rgba8snorm` (a core
  writable/readable storage format, no feature gate) for write-only/read-only access. Dawn accepts both
  (3 cases). (The `rgba8snorm`-storage root is the BGL-path remnant of [F-009](#f-009--yawgpu-over-restricts-render-attachment-dimension-and-under-validates-storage-usage).)
- **Resolved:** yawgpu `925520a` (*"BGL storage-texture 1D view dim + rgba8snorm base storage (F-018)"*),
  re-test 2026-06-01 — all 3 pass (`pass=4271 skip=377 fail=0 crash=0`). Removed from
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

- **Backend:** yawgpu (`925520a`). Not in Dawn (accepts) or wgpu-native (which aborts, F-021). This is the
  **"null bind group layouts"** feature — a pipeline layout may have null (unused) BGL slots.
- **What:** `createPipelineLayout:bind_group_layouts,null_bind_group_layouts` (T18) — a pipeline layout
  with exactly one `null`/`undefined` BGL slot (valid, unused) was rejected (*"bindGroupLayouts elements
  must not be null"*); yawgpu didn't implement null BGL slots (it accepted the `empty`-BGL subcases).
  Dawn passes.
- **Resolved:** yawgpu `f75fc0a` (*"implement null bind-group-layout slots (F-020)"*), re-test
  2026-06-01 — all 30 subcases pass (`pass=4307 skip=377 fail=0 crash=0`). Removed from
  `expectations/yawgpu.txt`.

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

- **Backend:** yawgpu (`f75fc0a`). Not in Dawn (defers, accepts) or wgpu-native (which aborts on the null
  BGL first, F-021).
- **What:** `createPipelineLayout:{create,set}_pipeline_with_null_bind_group_layouts` (T21) — with a BGL
  `buffer{type:uniform}` `minBindingSize` of 0 (unset) against a 4-byte shader uniform, yawgpu rejected at
  **pipeline creation** (*"minBindingSize is too small"*) instead of deferring the size check to bind time.
  Dawn passes both.
- **Resolved:** yawgpu `798fc6a` (*"defer minBindingSize=0 to bind time in createPipelineLayout compat
  (F-022)"*), re-test 2026-06-01 — both tests pass (`pass=4332 skip=383 fail=0 crash=0`). Removed from
  `expectations/yawgpu.txt`.

---

## F-023 — yawgpu aborts on a 0-size clearBuffer / copyBufferToBuffer (un-ended Metal blit encoder)

- **Backend:** yawgpu (`798fc6a`, Metal). Not in Dawn (handles 0-size ops). wgpu-native hits a *different*
  abort on the clearBuffer case (F-002); it passes the 0-size copy.
- **What:** `api,operation,command_buffer:{clearBuffer:clear (size=0), copyBufferToBuffer:single
  (copySize=0)}` (T22) — a 0-byte clear/copy (a valid no-op) aborted via *"Command encoder released
  without endEncoding"*: yawgpu's Metal backend made a blit command encoder for the no-op and released it
  without `endEncoding`. Dawn executes it cleanly.
- **Resolved:** yawgpu `e56f30a` (*"resolve WGPU_WHOLE_SIZE in clearBuffer — F-023 fully complete"*),
  staged: `8646e5f` ended the blit encoder (which then exposed that `clearBuffer` never zeroed),
  `a344faf` + `e56f30a` implemented the zero-fill and the start-of-range word. At `e56f30a`:
  `command_buffer,* pass=5 fail=0 crash=0` (Dawn-equal); `api,validation` unchanged. Removed from
  `expectations/yawgpu.txt`.
- **Diagnosis note (applies to all real-GPU findings):** this verification must run with the Bash sandbox
  **disabled**. Under the macOS sandbox, Metal `enumerate_adapters` returns no adapters and every case
  false-fails with *"failed to create WebGPU instance"* — a harness/environment artifact, not a backend
  defect.

---

## F-024 — yawgpu loses data in an rgba8uint texture-copy roundtrip (copyBufferToTexture → copyTextureToBuffer)

- **Backend:** yawgpu (`e56f30a`, Metal). Not in Dawn or wgpu-native — both pass.
- **What:** `api,operation,command_buffer/basic:{b2t2b,b2t2t2b}` (T23) — a `u32 0x01020304` uploaded into a
  `1×1×1` `rgba8uint` texture via `copyBufferToTexture` (`bytesPerRow=256`) and copied back read **all
  zeros** (`expected 4, got 0`). `b2t2b`/`b2t2t2b` failed identically, isolating it to the copy path (not
  `copyTextureToTexture`); buffer↔buffer paths were clean. Root cause: yawgpu's HAL texture-format set
  didn't include `rgba8uint`, so the copy was silently a no-op.
- **Resolved:** yawgpu `6580617` (add `rgba8uint`) → `c893eac` (*"expand texture-format coverage to all
  uncompressed color formats"*), re-test on real-GPU Metal — `command_buffer,basic pass=3` (Dawn-equal),
  rest of T23 unchanged, `api,validation` unchanged (`pass=4332 skip=383`). Removed from
  `expectations/yawgpu.txt`. 3-way confirmed throughout.

---

## F-025 — yawgpu `queueWriteTexture` writes zeros to color textures

- **Backend:** yawgpu (captured `c893eac`, Metal). Not in Dawn or wgpu-native — both pass.
- **What:** `api,operation,command_buffer/image_copy` (T24b) — every `WriteTexture`-init case
  (`initMethod=0`, upload via `wgpuQueueWriteTexture`) read back **all zeros** (`expected 0.00392157, got
  0`) across all 5 tests. With [F-026](#f-026--yawgpu-mishandles-non-default-buffer-layout-and-mip-levels-in-copybuffertotexture--copytexturetobuffer)
  the full yawgpu run was `pass=21860 fail=115396` (vs Dawn `137256/0`).
- **Resolved:** yawgpu `1e6c70b` (*"HAL texture dimension/array/mip support + queueWriteTexture upload
  (F-025, F-026)"*), re-test 2026-06-03 — the `WriteTexture` path now uploads correctly; full `image_copy`
  is `pass=137256 fail=0` (Dawn-equal). No `expectations/yawgpu.txt` lines were ever added (surfaced and
  fixed, not masked).

---

## F-026 — yawgpu mishandles non-default buffer layout (and mip levels) in `copyBufferToTexture` / `copyTextureToBuffer`

- **Backend:** yawgpu (captured `c893eac`, Metal). Not in Dawn or wgpu-native — both pass.
- **What:** `api,operation,command_buffer/image_copy` (T24b) — `CopyB2T`-init cases (`initMethod=1`) with a
  non-trivial buffer layout (`offset`/`bytesPerRow`/`rowsPerImage`) or a non-base mip level read back
  **wrong non-zero data** (`expected 0.00392157, got 0.705882`) — texels at the wrong linear offset, in the
  copy itself (both `FullCopyT2B` and `PartialCopyT2B`). Tightly-packed base-mip cases passed; Dawn passes
  all.
- **Resolved:** yawgpu `1e6c70b` (same commit as [F-025](#f-025--yawgpu-queuewritetexture-writes-zeros-to-color-textures)),
  re-test 2026-06-03 — `copyBufferToTexture`/`copyTextureToBuffer` now honour non-default layout and
  per-mip sub-resource size; full `image_copy` is `pass=137256 fail=0`. No expectations were added.

---

## F-027 — wgpu-native diverges on a 3D whole-subresource readback after a non-zero-origin copy (FullCopyT2B)

- **Backend:** wgpu-native (Metal). **Not** present in Dawn — Dawn passes the identical cases.
- **Found by:** `api/operation/command_buffer/image_copy` (T24b) — `origins_and_extents` on a **3D** texture
  with the **`FullCopyT2B`** check (`dimension=3;checkMethod=0`). The faithful upstream `FullCopyT2B` helper
  snapshots the whole mip subresource, overlays the uploaded sub-box on the CPU side, then **re-reads the
  whole subresource** (`copyTextureToBuffer` from origin `{0,0,0}` spanning every depth slice) and compares.
- **Observed on wgpu-native:** the whole-3D-subresource readback returns wrong values when the copy targets a
  **non-zero origin** inside a multi-slice 3D texture — `origins_and_extents:format=1;dimension=3;
  initMethod=1;checkMethod=0` → `fail=36/144`. The **same cases with `PartialCopyT2B`** (single targeted
  readback of the copied sub-box) **pass `144/0`**, and the other 3D `FullCopyT2B` tests pass on wgpu-native
  (`mip_levels` 3D Full `12/0`, `offsets_and_sizes` 3D Full `198/0`) — so the divergence is specific to the
  whole-3D-subresource re-read with a non-zero copy origin, not 3D copies in general.
- **Expected (WebGPU):** Dawn passes all of these (`origins_and_extents` 3D Full `144/0`; full Dawn
  `image_copy` `pass=137256 fail=0`). The whole-subresource readback must return the snapshot contents for the
  untouched slices and the uploaded data for the copied sub-box.
- **Scope / magnitude:** the full wgpu-native `image_copy` run is `pass=116772 skip=19152 fail=1332`. The
  `1332` failures are exactly **37 cases × 36 subcases** — all `origins_and_extents;dimension=3;initMethod=1
  (CopyB2T);checkMethod=0 (FullCopyT2B)`, one per 3D-compatible format (`format ∈ {1,7,8,9,10,11,12,13,…}`).
  WriteTexture-init (`initMethod=0`) 3D `FullCopyT2B` is **not** affected.
- **Not triaged in `expectations/wgpu-native.txt`:** each failing case is **partial** (36 of its 144 subcases
  fail; the other 108 pass), and this harness's expectations are **case-level** — all subcases of a case share
  the case query (`runner.cpp` runs each subcase under `c.query`), so an expectation line would flip the 108
  passing subcases to **xpass** noise. Left **surfaced/unmasked** (same stance as the yawgpu F-025/F-026
  findings) pending the wgpu-native fix, rather than masked imprecisely.
- **Note (anti-masking):** an earlier T24b draft hid this by reading the copied region *before* the
  whole-subresource snapshot; the faithful upstream order (snapshot whole → re-read whole) re-exposes it. Kept
  faithful and surfaced as a finding rather than worked around — Dawn is the oracle and passes.
- **Status:** **OPEN.** 3-way: Dawn passes; yawgpu now also passes these (`image_copy` `pass=137256 fail=0`
  since `1e6c70b`, see [F-025](#f-025--yawgpu-queuewritetexture-writes-zeros-to-color-textures)/[F-026](#f-026--yawgpu-mishandles-non-default-buffer-layout-and-mip-levels-in-copybuffertotexture--copytexturetobuffer));
  only wgpu-native shows this distinct 3D whole-subresource defect.

---

## F-028 — wgpu-native loses non-zero depth slices in a 3D `copyTextureToTexture` (reads back zero)

- **Backend:** wgpu-native (Metal). **Not** present in Dawn or yawgpu — both pass the identical cases.
- **Found by:** `api/operation/command_buffer/copyTextureToTexture:color_textures,non_compressed,array`
  (T25) — the **3D** cases (`dimension=3`). The test fills a multi-slice source via `writeTexture`,
  `copyTextureToTexture` into a multi-slice destination, reads the whole destination level back via
  `copyTextureToBuffer`, and compares (decoded TexelView, `maxFractionalDiff=0`).
- **Observed on wgpu-native:** every destination texel at **depth slice z≥1 reads back zero** —
  `pixel mismatch at 0,0,1 component 0: expected <data>, got 0` — i.e. only slice 0 receives the copy;
  the higher 3D slices stay zero. Deterministic, across all 3D-compatible color formats.
- **Scope / magnitude:** the full wgpu-native run is `pass=26236 skip=3942 fail=738`. The 738 failures are
  exactly **41 cases × 18 subcases**, all `color_textures,non_compressed,array;dimension=3`. The same
  test's **2D-array** cases (`dimension=2`, multi-*layer*) **pass 208/208**, and the `non_array` test
  passes — so the defect is specific to **3D depth slices**, not multi-layer copies in general.
- **Expected (WebGPU):** Dawn and yawgpu both pass all of these (`copyTextureToTexture:*`
  `pass=30910 skip=6 fail=0` on each). A 3D `copyTextureToTexture` must populate every copied depth
  slice, and the readback must return them.
- **Same family as [F-027](#f-027--wgpu-native-diverges-on-a-3d-whole-subresource-readback-after-a-non-zero-origin-copy-fullcopyt2b).**
  Both are wgpu-native 3D multi-slice copy/readback divergences surfaced by the texture-copy operation
  ports (F-027: `image_copy` whole-3D-subresource re-read; F-028: `copyTextureToTexture` 3D slices) —
  likely one gfx-rs 3D-texture defect.
- **Not triaged in `expectations/wgpu-native.txt`:** the failing cases are **partial** (18 of 208 subcases
  per case), and this harness's expectations are case-level (subcases share the case query), so a line
  would flip the 190 passing subcases to **xpass**. Left **surfaced/unmasked** (same stance as F-027).
- **Status:** **OPEN.** wgpu-native-only; Dawn + yawgpu clean.

---

## F-029 — yawgpu leaks Vulkan device resources across image_copy cases (later tests in the same process fail)

- **Backend:** yawgpu (`1e6c70b`, Vulkan / Windows, NVIDIA RTX 5060 Ti). **Cross-backend not yet
  confirmed** — only yawgpu was run; whether wgpu-native/Dawn show the same behavior on Windows/Vulkan
  is unverified. **Distinct from** the resolved correctness findings
  [F-025](#f-025--yawgpu-queuewritetexture-writes-zeros-to-color-textures)/[F-026](#f-026--yawgpu-mishandles-non-default-buffer-layout-and-mip-levels-in-copybuffertotexture--copytexturetobuffer):
  `image_copy` run **alone** still passes `137256/0`; this is a *cross-test* resource leak, not a
  copy-correctness defect.
- **Found by:** the whole-listing fast-mode run
  `cts --sample-formats --crash-list expectations/yawgpu.crash.txt <all file-level :* queries>`
  → `pass=944 skip=39 fail=504 crash=0` (2026-06-03; the listing was the 16 files before T25's
  `copyTextureToTexture` landed). The 504 failures collapse to one root cause.
- **Observed:** every test doing GPU execution/readback that runs *after* `image_copy` in the same
  process fails:
  - In the combined run — `pixel mismatch … expected X, got 0` (the 8 sampled `image_copy` formats)
    and `GPU buffer mismatch … got 0` (`clearBuffer` / `copyBufferToBuffer` operation): readback
    returns all **zeros**.
  - Isolated probe — `image_copy:*` then `clearBuffer:clear:*` in **one** process → clearBuffer fails
    `failed to request device: HAL device creation failed: vulkan` (all 25585 subcases). So once
    `image_copy` has run, yawgpu can no longer create a fresh Vulkan device.
- **Isolation (proves cross-test, not per-test):** run on its own, each is clean —
  `clearBuffer:clear` `pass=50/0`, `copyBufferToBuffer:single` `pass=340/0`, `writeBuffer` `pass=24/0`;
  `clearBuffer:*` + `copyBufferToBuffer:*` together (no `image_copy`) `pass=392/0`; and `image_copy`
  alone is `pass=137256/0` (F-025/F-026 record). Only the **sequence** `image_copy` → (any later test
  that creates a device) fails.
- **Diagnosis:** `image_copy` does not release device-level GPU resources per case (the device itself
  and/or `VkDeviceMemory` / textures), so the Vulkan physical device is exhausted and the next
  `requestDevice` fails during HAL device creation. `clearBuffer` + `copyBufferToBuffer` create
  comparably many devices without exhausting, so the leak is specific to the **image_copy / texture**
  path. (Each ported operation/image_copy test creates its own device.) The two combined-run signatures
  — `got 0` readback vs. outright device-creation failure — are both consistent with device-resource
  exhaustion (a partially-degraded device returns zeros; a fully-failed creation errors).
- **Why it surfaces now:** the all-green combined run recorded at `e56f30a` (2026-06-02, 12 file-level
  queries, `pass=4131/0`) did **not** include `image_copy` (T24b landed later, `c893eac`→`1e6c70b`).
  The current listing is the first combined run to include a *passing* `image_copy`, which is what
  exposes the leak. So this is a newly-surfaced defect, not a regression of previously-working behavior.
- **Isolation note:** `--crash-list` (empty here) does not help — these are **fails**, not aborts, so
  `crash=0` and nothing is forked. Per-case `--isolate` *would* sidestep the leak (each case in a fresh
  process) at the cost of speed.
- **Root cause (confirmed, Vulkan validation layers).** The diagnosis above was confirmed precisely:
  yawgpu's `submit_copies` returned without keeping the resources a submission references alive, so the
  per-case **staging buffer** (`queueWriteTexture`) and the **copy-target texture** were destroyed
  while still in use by an executing command buffer. With `VK_LAYER_KHRONOS_validation`, a single
  `image_copy` write case emits `VUID-vkDestroyBuffer-buffer-00922` and `VUID-vkDestroyImage-image-01000`
  ("…currently in use by VkCommandBuffer"), and at teardown `VUID-vkDestroyDevice-device-05137`
  (command buffers undestroyed — the retire ring failed to drain after the device was lost). Freeing the
  image's `DEVICE_LOCAL` `VkDeviceMemory` in-flight is what loses/exhausts the device on NVIDIA, which is
  why the texture path is fatal where the buffer-only path (same UB class, host-visible memory) survives.
- **Resolved:** yawgpu `1e67300` (*"retain in-flight Vulkan copy resources until fence signals (F-029)"*)
  — the Vulkan retire ring now retains the owning `Arc`s for every buffer/texture a submission references
  and drops them only after the fence signals. Re-test on real-GPU Windows/Vulkan (NVIDIA): validation
  layers emit **zero** of the three VUIDs; the cross-test poison is gone (`image_copy:undefined_params:*`
  then `clearBuffer:clear:*` in one process → `clearBuffer` `pass=50/0`, no device-creation failure);
  `image_copy` alone remains `137256/0`. No `expectations/yawgpu.txt` lines were ever added (surfaced,
  not masked).

---

## F-030 — yawgpu `MAP_READ` readback reads the buffer before the GPU copy completes (intermittent zeros)

- **Backend:** yawgpu (Vulkan / Windows, NVIDIA RTX 5060 Ti). Surfaced **after** the
  [F-029](#f-029--yawgpu-leaks-vulkan-device-resources-across-image_copy-cases-later-tests-in-the-same-process-fail)
  fix: F-029's catastrophic device loss had **masked** this entirely (whole files were failing
  `~6191/6192`). With F-029 fixed, a small residual remained.
- **Found by:** `api,operation,command_buffer,image_copy:*` on real-GPU Vulkan after the F-029 fix —
  ~0.4% of cases fail `pixel mismatch … expected X, got 0`, with a **nondeterministic** count that
  varies run to run (`image_copy:mip_levels:*` standalone gave fail counts `0, 1, 2, 0, 0` over five
  runs). The full `image_copy:*` landed at `pass≈136684 fail≈572` instead of `137256/0`.
- **Observed:** a `MAP_READ` of a buffer that was just filled by `copyTextureToBuffer` /
  `copyBufferToBuffer` occasionally reads back zeros — the host read raced ahead of the GPU copy.
- **Root cause:** the buffer-map readback had **no GPU-completion wait**. `mapAsync` set the pending
  map's outcome to `Success` eagerly, and `resolve_pending_map` read host-visible memory without
  waiting for the copy submission to finish (`submit_copies` is asynchronous — it registers a fence in
  the retire ring and returns). Small/fast copies usually won the race, hence the intermittency and the
  bias toward textures over the tiny buffer-copy tests. (Independent of F-029 — F-029 only un-masked it.)
- **Resolved:** yawgpu `1297b7e` (*"wait for GPU completion before MAP_READ readback (F-030)"*) — added
  `HalQueue::wait_idle` (Vulkan `queue_wait_idle`; Metal empty-commit+wait; GLES `glFinish`; Noop no-op)
  and `Queue`/`Device::wait_idle`; a read-map now idles the device queue before exposing data (including
  Vulkan persistently-mapped buffers). Re-test on real-GPU Windows/Vulkan: `image_copy:mip_levels:*` is
  deterministically `pass=6192 fail=0` across repeated runs and full `image_copy:*` is back to
  `pass=137256 fail=0`. No `expectations/yawgpu.txt` lines (surfaced, not masked).

---

## F-031 — yawgpu diverges on the depth aspect of `copyTextureToTexture` (copied depth fails an equality re-render)

- **Backend:** yawgpu (Metal + Vulkan). Not in Dawn/wgpu-native (both pass all 216).
- **What:** `copyTextureToTexture:copy_depth_stencil` (T26) — the copied **depth** aspect failed the
  `depthCompare:'equal'` re-render (no green) for all depth formats; stencil-only `Stencil8` passed
  (`pass=36 fail=180`). Root cause: yawgpu's regular real-backend render path had no depth support.
- **Resolved:** yawgpu `f3afc31` (Metal, 7 depth-render gaps) + `cac328a` (Vulkan HAL); re-test
  `copy_depth_stencil` `pass=216 fail=0` on Metal **and** native Windows/Vulkan. GLES untested.

---

## F-032 — yawgpu returns zeros for depth/stencil aspect buffer⇄texture copies (except plain Stencil8)

- **Backend:** yawgpu (Metal + Vulkan). Not in Dawn/wgpu-native (both pass all 1152).
- **What:** the T27 `image_copy` depth/stencil aspect ports — yawgpu returned **zeros** for depth-aspect
  `copyTextureToBuffer` (all depth formats) and stencil-aspect copies of packed depth+stencil formats;
  plain `Stencil8` passed (`pass=288 fail=864`). (Surfaced because the T27 readback buffers are
  zero-initialized — a no-write copy fails instead of being masked by a pre-filled buffer.)
- **Resolved:** yawgpu `c8f15d5`+`af9ac5c` (Metal) + `3c847ac` (Vulkan HAL); re-test `image_copy`
  depth/stencil `pass=1152 fail=0` on Metal **and** native Windows/Vulkan.

---

## F-033 — color `copyTextureToTexture` pixel mismatches on Mac via MoltenVK (confirmed MoltenVK artifact, not a yawgpu defect)

- **Environment finding, not a backend conformance defect.** Backend: yawgpu's **Vulkan** HAL **run on
  macOS through MoltenVK** (`CTS_YAWGPU_BACKEND=vulkan` on a `--features vulkan` build — see
  `docs/06-build-and-run.md`). **Not** present on yawgpu **Metal**, and **native Windows/Vulkan (NVIDIA)
  does not exhibit it** (the yawgpu developer re-checked native Windows/Vulkan directly: color
  `copyTextureToTexture` is clean there) — so this is a **MoltenVK (Vulkan→Metal) translation artifact**,
  not a yawgpu Vulkan-HAL bug.
- **Found by:** the fast-mode (`--sample-formats`) whole-suite measurement on yawgpu-Vulkan/MoltenVK
  (2026-06-03). `api,validation` is **fully clean** (`pass=9561 skip=816 fail=0`); the only color
  divergence is in `copyTextureToTexture`.
- **Observed:** `copyTextureToTexture` **color** (`color_textures,non_compressed,{non_array,array}` and
  `zero_sized`) — `pixel mismatch … expected 0, got 1` at scattered texels, across many color formats
  (RGBA8Unorm = `format=22`, and 23/24/25/27/30/40/41/…), ~half of the sampled color-T2T subcases
  (≈3116 fail; e.g. one `RGBA8Unorm` case sequentially `pass=58 fail=54` vs Metal `112/0`). Color T2T is
  a **byte-exact** copy (no format conversion), so an off-by-one byte points at the copy/blit path.
- **Tight scope (what isolates it to MoltenVK's texture-to-texture path):** on the same MoltenVK run,
  **buffer⇄texture** color copies are clean — `image_copy` color `pass=25824` (its only fails are the
  depth/stencil F-032 cases), and `copyBufferToBuffer`/`writeBuffer`/`clearBuffer`/`basic` have **0**
  fails. Only **texture→texture** color copy diverges.
- **Depth/stencil on this (2026-06-03 MoltenVK) run were the known findings, not F-033:** `copy_depth_stencil`
  `fail=180` ([F-031](#f-031--yawgpu-diverges-on-the-depth-aspect-of-copytexturetotexture-copied-depth-fails-an-equality-re-render))
  and `image_copy` depth/stencil `fail=864`
  ([F-032](#f-032--yawgpu-returns-zeros-for-depthstencil-aspect-buffertexture-copies-except-plain-stencil8))
  — **both since fixed on the Vulkan HAL** (`cac328a` / `3c847ac`; native Windows `216/0` and `1152/0`).
- **Status:** **CONFIRMED MoltenVK-only — not a yawgpu defect**, low priority. The predicted native check
  is now done: the full ported suite on **native Vulkan (Windows/NVIDIA, 2026-06-04)** — including all color
  `copyTextureToTexture` — is **clean** (`pass=7208 skip=388 fail=0`), so the color-T2T cases that mismatch
  under MoltenVK pass on native Vulkan, confirming this is a MoltenVK (Vulkan→Metal) translation limitation,
  not actionable for yawgpu. No `expectations/` lines added. (Recorded because the Mac→Vulkan-via-MoltenVK
  path is now a documented diagnostic — see `docs/06-build-and-run.md` — and this is its main known caveat.)
- **Re-confirmed on Mac/MoltenVK at the latest yawgpu (`3c847ac`, 2026-06-04):** color
  `copyTextureToTexture` still mismatches (full run `pass=16614 fail=14512` — `color_textures` `array`
  9400 + `non_array` 5076 + `zero_sized` 36, same `expected 0, got 1` signature), while F-031
  `copy_depth_stencil` (`216/0`) and F-032 `image_copy` depth/stencil (`1152/0`) now **pass under MoltenVK
  too**. So only the color-T2T translation gap remains under MoltenVK, and native Windows/Vulkan is clean —
  isolating it firmly to MoltenVK, not yawgpu.

---

## F-034 — yawgpu: a fragment storage write is lost on **indexed / indirect** draws

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass all 744).
- **What:** the T30 `rendering/draw` ports — a fragment `read_write` storage write read back `0` on
  **indexed / indirect** draws (`drawIndexed`/`drawIndirect`/`drawIndexedIndirect`); plain `draw` worked
  (`pass=340 fail=224`). Root cause: yawgpu didn't execute the indexed/indirect draw paths at all.
- **Resolved:** yawgpu `36a6b66` (implement indexed/indirect draw execution); re-test `rendering/draw`
  `pass=564 fail=0`.

---

## F-035 — yawgpu ignores `GPUColorTargetState` `blend` and `writeMask` (writes the raw fragment output) — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn (passes all 23).
- **What:** the T31 `rendering/color_target_state` ports — yawgpu wrote the raw fragment output to all
  channels, ignoring **both** `GPUColorTargetState.writeMask` and `.blend` (`pass=2 fail=21`; a case
  passed only when the expected value equaled the unmasked/unblended source).
- **Resolved:** yawgpu `74f5ef2` (apply color-target blend + writeMask + blend constant); re-test
  `color_target_state` `pass=23 fail=0` on both HALs.

---

## F-036 — wgpu-native aborts when a constant-factor blend draws without `setBlendConstant` (should default to `[0,0,0,0]`)

- **Backend:** wgpu-native (`v29.0.0.0-8-g9176708`, real-GPU Metal). **Not** present in Dawn or yawgpu
  (both run the cases — Dawn passes, yawgpu fails for the unrelated F-035 reason, neither crashes).
- **Found by:** the T31 `rendering/color_target_state` ports — `blend_constant,initial` and
  `blend_constant,not_inherited`, the two cases that intentionally **omit** `setBlendConstant` on a
  `srcFactor=constant` pipeline to verify the constant defaults to `[0,0,0,0]`. **Dawn passes both;
  wgpu-native aborts both** (`signal 6`; full run `pass=21 crash=2`).
- **Observed:** at `queueSubmit`, wgpu-native raises `Validation Error … In a draw command … Blend
  constant needs to be set`, then `fatal runtime error: failed to initiate panic … aborting` — a process
  abort, like F-001/F-002. `blend_constant,setting` (which calls `setBlendConstant`) and every
  `color_write_mask,*` case pass.
- **Expected (WebGPU):** the render-pass blend constant **defaults to `(0,0,0,0)`**; `setBlendConstant`
  is optional. Drawing a constant-factor blend without it is valid and must use `(0,0,0,0)`. Dawn and
  yawgpu do not require the call.
- **Two defects:** (a) wgpu-native wrongly **requires** `setBlendConstant` for constant-factor blends (a
  spec deviation), and (b) it surfaces this as a **process abort** rather than a catchable validation
  error (the F-001/F-002 abort class).
- **Status:** **OPEN** (wgpu-native, bring-up reference). Contained via `--isolate`; the 2 cases
  (`blend_constant,initial:*`, `blend_constant,not_inherited:*`) are marked expected in
  `expectations/wgpu-native.txt` so an `--isolate --expectations …` run stays green (still an open defect,
  not masked-away).

---

## F-037 — yawgpu Metal HAL: non-deterministic depth-attachment render/readback race

- **Backend:** yawgpu **Metal HAL only** (Dawn/wgpu-native/yawgpu-Vulkan all clean `130/130`).
- **What:** the T32 `rendering/depth` ports flaked non-deterministically on Metal (`fail≈33–44`, varying;
  `expected 1, got 0` — the drawn point intermittently not covered); every case passed in isolation. Root
  cause: yawgpu's Metal HAL didn't emit `[[point_size]]` for **`point-list`** pipelines (the depth tests
  are the suite's first point-list users), so point size was undefined. (Initial sync-race hypothesis was
  wrong.)
- **Resolved:** yawgpu `186cd54` (emit `[[point_size]]` for Metal point-list); re-test `pass=130 fail=0`
  across 11 runs. Metal-only fix.

---

## F-038 — yawgpu mishandles stencil operations, compare, and masks — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass all 188).
- **What:** the T33 `rendering/stencil` ports — yawgpu mishandled stencil ops/compare/masks (`pass=97
  fail=91`). Single root cause: the dynamic stencil reference (`setStencilReference`) wasn't threaded to
  the HAL, scrambling everything downstream.
- **Resolved:** yawgpu `40f5d7f` (thread dynamic stencil reference to the HAL); re-test `rendering/stencil`
  `pass=188 fail=0` on both HALs.

---

## F-039 — yawgpu: two dispatches in one compute pass lose their writes under batch execution — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass every mode).
- **What:** the T35 `memory_sync/single_buffer` `two_dispatches_in_the_same_compute_pass` read back `0`
  instead of `2` (both writes lost) **only under batch/`--isolate`** (passed in isolation; `pass=24 fail=1`).
  Root cause: yawgpu treated the whole compute pass as one usage scope instead of per-dispatch.
- **Resolved:** yawgpu `89f25df` (per-dispatch usage scope); re-test `single_buffer` `pass=25 fail=0` across
  all run modes on both HALs.

---

## F-040 — yawgpu: multisample resolve does not write the resolve target — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass all 14).
- **What:** the T36 `render_pass/resolve` port — yawgpu's MSAA resolve never wrote the `resolveTarget`
  (all 12 `render_pass_resolve` subcases `expected 1, got 0`; non-MSAA `storeop2` passed; `pass=2 fail=12`).
  Root cause: no MSAA render pipeline / multisample-resolve / multi-color-attachment support.
- **Resolved:** yawgpu `bc8c280`+`3303058`; re-test `render_pass/resolve` `pass=12 fail=0` on both HALs.

---

## F-041 — yawgpu: read-only storage-texture `textureLoad` reads back zero — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass all 3).
- **What:** the T37 `storage_texture/read_only` port — `textureLoad` on a `texture_storage_2d<…, read>`
  returned `0` (output buffer all zeros; `pass=0 fail=3`). The compute storage-**buffer** path worked.
  Root cause: storage-texture bindings weren't wired to the shader + the Metal HAL lacked MSL runtime-array
  buffer sizes.
- **Resolved:** yawgpu `2e4edb7`; re-test `read_only:basic` `pass=3 fail=0` on both HALs.

---

## F-042 — yawgpu: a render-stage (fragment) storage-buffer write from a point draw reads back zero — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass all 5).
- **What:** the T39 `memory_sync/single_buffer` `two_draws_*` ports — a fragment-stage `read_write` storage
  write from a point draw read back `0` (`pass=0 fail=5`, incl. the non-bundle subcase). The compute
  storage write worked. Root cause: the render usage scope rejected write+write across draws + render-bundle
  draws weren't executed.
- **Resolved:** yawgpu `042902b`+`eadc2f6`; re-test `two_draws_*` `pass=5 fail=0` on both HALs.

---

## F-043 — yawgpu: render-pass `depthSlice` is ignored — always renders to slice 0 of a 3D texture — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass all 6).
- **What:** the T43 `rendering/3d_texture_slices` port — yawgpu ignored
  `WGPURenderPassColorAttachment.depthSlice`, always rendering to slice 0 of a 3D texture (the 3
  `depthSlice=1` cases failed; `pass=3 fail=3`). Mip routing was fine. Root cause: `depthSlice` wasn't
  threaded into the 3D render-target view.
- **Resolved:** yawgpu `c6935f7`; re-test `one_color_attachment,mip_levels` `pass=6 fail=0` on both HALs.

---

## F-044 — yawgpu: non-`float32` vertex formats decode to zero in the shader — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass all 9).
- **What:** the T46 `vertex_state/correctness` port — yawgpu decoded only `float32x4`; the other 8
  representative formats (`float16/uint/sint/unorm/snorm/packed`) read back **all zeros** in the shader
  (`pass=1 fail=8`). Vertex-format *conversion* wasn't applied (only the 32-bit-float passthrough).
- **Resolved:** yawgpu `706087f` (full vertex-format set); re-test `vertex_format_to_shader_format_
  conversion` `pass=9 fail=0` on both HALs.

---

## F-045 — yawgpu and wgpu-native: `frag_depth` is not clamped to the viewport depth range before the depth test

- **Backend:** yawgpu (Metal + native Vulkan) **and wgpu-native**. Not in Dawn (passes).
- **What:** the T45 `rendering/depth_clip_clamp` `depth_test_input_clamped` port — `frag_depth` was **not
  clamped to the viewport depth range** `[minDepth,maxDepth]` before the depth test (the out-of-range
  points drew, `expected 0, got 255`). The depth clamping is independent of the `depth-clip-control`
  feature; Dawn is the conformance reference.
- **Status:** **RESOLVED for yawgpu** — yawgpu `155a854` (clamp `frag_depth` to the viewport range); passes
  on Metal (`pass=1 skip=1`) and **native Vulkan** (user-confirmed full-CTS-green on Windows/NVIDIA). The
  residual MoltenVK failure (`pass=0 fail=1`) is a **confirmed MoltenVK-only artifact** (Vulkan→Metal),
  like F-033 — not a yawgpu defect. **Still an open wgpu-native gap.** Surfaced, not masked.

---

## F-046 — yawgpu: face culling / `front_facing` winding is mishandled — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass).
- **What:** the T47 `render_pipeline/culling_tests` port — yawgpu computed `@builtin(front_facing)` with the
  wrong winding (the CCW triangle read red when `frontFace=ccw`), so both the color and the `cullMode`
  decision were wrong (`pass=2 fail=10`).
- **Resolved:** yawgpu `f82c2d6`+`d6e700a` (cull/frontFace threaded through the subpass path); re-test
  `culling` `pass=12 fail=0` on both HALs.

## F-047 — yawgpu: pipeline-overridable constants are ignored (read as zero) — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass).
- **What:** the T50 `render_pipeline/overrides` (`pass=1 fail=5`) + T56 `compute_pipeline/overrides`
  (`pass=0 fail=1`) ports — WGSL `override` constants read back `0` (neither the WGSL default nor the
  pipeline `WGPUConstantEntry` value applied), in **both render and compute** pipelines.
- **Resolved:** yawgpu `fff8634` (apply overridable constants in shader codegen); re-test render `pass=6` +
  compute `pass=1` on both HALs.

---

## F-048 — yawgpu and wgpu-native: the stencil reference value is not masked to the stencil aspect's bit width

- **Backend:** yawgpu (cross-HAL) **and wgpu-native**. Not in Dawn (passes all 30).
- **What:** the T51 `render_pass/clear_value` `stencil_clear_value` port — the stencil **reference** wasn't
  masked to the 8-bit stencil aspect before the `equal` compare (the 6 cases with an unmasked out-of-range
  reference 258/65539 failed; the clear-value masking worked; `pass=24 fail=6`). The CTS encodes the
  masking requirement; Dawn is the reference.
- **Status:** **RESOLVED for yawgpu** — yawgpu `9bc49dc` (mask the stencil reference to the 8-bit aspect
  width); re-test `stencil_clear_value` `pass=30 fail=0` on both HALs. **wgpu-native is still affected.**
  Surfaced, not masked.

---

## F-049 — yawgpu: render-bundle execution mishandles the viewport rect, bundle draw-args, and repeated/blended replay — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass all 4).
- **What:** the T54 `command_buffer/render/render_bundle` port — only `basic` passed (`pass=1 fail=3`): the
  render-pass **viewport rect was ignored** (reused-bundle test filled the whole target), and the
  two-bundle / blended-replay cases mis-applied bundle draw-args / blend.
- **Resolved:** yawgpu `f82c2d6` (viewport/scissor threaded through the subpass path — same fix as F-046);
  re-test `render_bundle` `pass=4 fail=0` on both HALs.

---

## F-050 — yawgpu: occlusion query returns zero even when samples pass — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK). Not in Dawn/wgpu-native (both pass).
- **What:** the T58 `command_buffer/queries/occlusionQuery` port — `occlusion_query,basic` resolved to `0`
  for a covering draw (passing samples never counted; `empty` coincidentally passed; `pass=1 fail=1`).
- **Resolved:** yawgpu `37d36e6`+`e70d18d` (occlusion-query execution on Metal + Vulkan); re-test
  `occlusionQuery` `pass=2 fail=0` on both HALs.

---

## F-051 — yawgpu Metal HAL: crash creating a default view of a multisampled texture — Metal-HAL-only

- **Backend:** yawgpu **Metal HAL only** (Vulkan/MoltenVK + Dawn always passed). Distinct from the
  wgpu-native sample_mask defect (F-052).
- **What:** the T59 `render_pipeline/sample_mask` MSAA port — yawgpu's Metal HAL aborted creating a default
  `createView()` of a `sampleCount=4` texture (`_mtlValidateArgumentsForTextureViewOnDevice … textureType
  (MTLTextureType2DMultisample) not compatible with texture view textureType (MTLTextureType2D)`); it
  hardcoded `MTLTextureType2D` instead of propagating the source's multisample-ness.
- **Resolved:** yawgpu `c29dc78`-era update; re-test `sample_mask` `pass=6 fail=0 crash=0` on both HALs
  (Metal + MoltenVK). Removed from `expectations/yawgpu.crash.txt`. Surfaced, not masked.

---

## F-052 — wgpu-native: the pipeline `multisample.mask` is ignored

- **Backend:** wgpu-native (Metal, real-GPU). Not in Dawn (passes all 6) or yawgpu (Vulkan/MoltenVK passes
  all 6; Metal blocked separately by F-051).
- **Found by:** the T59 `render_pipeline/sample_mask` MSAA port — `pass=3 fail=3`.
- **Observed:** every case whose pipeline `multisample.mask != 0xF` fails (`sample_mask_subset`,
  `and_of_all`, `none`); every case with `mask == 0xF` passes (`all_full`, `raster_subset`,
  `frag_mask_subset`). Masked-out samples are still written (e.g. `none`/`mask=0` reads back the drawn
  texel colors instead of clear `0`). The fragment `@builtin(sample_mask)` output and the rasterization
  mask are honored; only the pipeline `multisample.mask` is not AND-ed into the coverage.
- **Expected (WebGPU):** the final per-sample coverage is the logical AND of the rasterization mask, the
  pipeline `multisample.mask`, and the fragment `@builtin(sample_mask)` output. Dawn is the reference.
- **Status:** **OPEN** (wgpu-native defect). Surfaced, not masked.

---

## F-053 — yawgpu: cannot render to multiple color attachments targeting different slices of one 3D texture — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK both fail). Not in Dawn or wgpu-native (both
  pass).
- **Found by:** the T62 `rendering/3d_texture_slices` `multiple_color_attachments,same_mip_level` port — a
  single render pass with 4 color attachments, each bound to a different `depthSlice` (0..3) of one
  `4×4×16` `rgba8unorm` 3D texture, drawing a 4-output fragment shader.
- **Observed:** the rendered slices read back **zero** (`rgba8unorm mismatch at (0,0,0) channel 0:
  expected 11, got 0`) — nothing is written to the multi-attachment 3D slices. `pass=0 fail=1`.
- **Expected (WebGPU):** each color attachment writes its fragment output to its `depthSlice`; slice `i`
  (covered texels) should hold location-`i`'s value. Dawn and wgpu-native pass.
- **Status:** **RESOLVED** — yawgpu `c29dc78`-era update; re-test `pass=1 fail=0` on the **Metal HAL** and
  green on **native Windows / NVIDIA Vulkan** (user-confirmed 2026-06-08). The residual MoltenVK failure
  (`[mvk-error] VK_ERROR_FEATURE_NOT_PRESENT: vkCreateImageView(): 2D views on 3D images can only be used as
  color attachments`) is a **confirmed MoltenVK-only Vulkan→Metal translation artifact** (like F-033 /
  F-045), **not** a yawgpu defect. Surfaced, not masked.

---

## F-054 — yawgpu: a render pass with a sparse / null color attachment renders nothing — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK both fail). Not in Dawn or wgpu-native (both
  pass).
- **Found by:** the T65 `render_pipeline/pipeline_output_targets` `color,attachments` port — a pipeline +
  render pass with 2 color-attachment slots where one slot (`emptyAttachmentId`) is **null** (null pipeline
  target `format=Undefined` + null render-pass attachment `view=nullptr`) and the other is a real
  `rgba8unorm` attachment; the fragment writes only to the non-null `@location`.
- **Observed:** the non-null attachment reads back **zero** (`expected 199 (±1), got 0` for
  `emptyAttachmentId=0`; `expected 31, got 0` for `=1`) — nothing is written. `pass=0 fail=2`.
- **Expected (WebGPU):** sparse color attachments are valid; the fragment's outputs go to the non-null
  slots. Dawn and wgpu-native pass.
- **Status:** **RESOLVED** — yawgpu `793fc6d`-era update; re-test `color,attachments` `pass=2 fail=0` on
  **both HALs** (Metal + Vulkan/MoltenVK). Surfaced, not masked.

---

## F-055 — yawgpu: wrong values sampling a depth/stencil aspect while it is a read-only DS attachment — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK both fail). Not in Dawn or wgpu-native (both
  pass).
- **Found by:** the T72 `memory_sync/texture/readonly_depth_stencil` `sampling_while_testing` port
  (`format=depth24plus-stencil8`, `depthReadOnly=true`, `stencilReadOnly=true`). A `3×3` depth-stencil
  texture is filled (stencil `x+1`, depth `(y+1)/10`), then in one render pass it is bound as a **read-only**
  depth+stencil attachment (depth/stencil tested) **and** its depth-only / stencil-only aspects are
  **sampled** in the fragment shader; a check pass re-samples and writes `1` on match.
- **Observed:** the check writes `0` (`resultTexture texel (0,0) expected 1, got 0`) — the sampled depth or
  stencil aspect does not read back the expected value. `pass=0 fail=1`.
- **Expected (WebGPU):** a read-only depth-stencil attachment may be concurrently sampled as a texture; both
  reads see the stored contents. Dawn and wgpu-native pass.
- **Status:** **RESOLVED** — yawgpu `79c4968`-era update; re-test `sampling_while_testing` `pass=1 fail=0`
  on **both HALs** (Metal + Vulkan/MoltenVK) and on native Windows/NVIDIA Vulkan (user-confirmed). Surfaced,
  not masked. (The MSVC-portability of the test itself — `__builtin_memcpy` → `std::memcpy` — was fixed in
  `726bb13`.)

---

## F-056 — wgpu-native: aborts on a mixed read-only/written depth-stencil attachment that is also sampled

- **Backend:** wgpu-native (`9176708`). **Not** in Dawn or yawgpu (Metal + Vulkan/MoltenVK all pass).
- **Found by:** the T74 `memory_sync/texture/readonly_depth_stencil` `sampling_while_testing` matrix — the
  two **mixed** combos `(depthReadOnly=true, stencilReadOnly=false)` and `(false, true)`, where one aspect
  is read-only **and sampled** while the other aspect is written in the same render pass. The both-read-only
  and both-written combos pass.
- **Observed:** wgpu-native **panics and aborts the process** (`signal 6`):
  `panicked … Error in wgpuQueueSubmit: Validation Error — Attempted to use Texture … with conflicting
  usages. Current usage TextureUses(RESOURCE) and new usage TextureUses(DEPTH_STENCIL_WRITE). … is an
  exclusive usage …` → `fatal runtime error … aborting`. Its usage-scope validation treats the texture's
  `DEPTH_STENCIL_WRITE` (on the written aspect) as conflicting with the `RESOURCE` sample of the
  **read-only aspect** — it does **not** track usage **per aspect**, and it **aborts** instead of returning
  a graceful validation error.
- **Expected (WebGPU):** a depth-stencil texture may have one aspect read-only (and concurrently sampled)
  while the other aspect is written; the aspects are distinct subresources. Dawn and yawgpu accept it.
- **Status:** open; tracked as a **wgpu-native defect** (the abort family, like F-001/F-002/F-036). Contained
  via `--isolate` (the 2 mixed cases crash, the other 2 pass). Surfaced, not masked. **TODO:** add the 2
  cases to `expectations/wgpu-native.crash.txt` on the next Windows `--emit-crash-list` regeneration (the
  list is Windows-generated and currently `api,validation`-only).

---

## F-057 — yawgpu: WGSL compiler errors on `texture_cube_array<f32>` (float cube-array sampled texture) — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK both fail). Not in Dawn (passes all 160).
- **Found by:** the `api,validation,non_filterable_texture` port (`non_filterable_texture_with_filtering_sampler`).
  Exactly the **8** `sampleType=float, viewDimension=cube-array` rows fail (compute/render × async ×
  sameGroup); the `sint`/`uint` cube-array rows and all other view dimensions compile fine.
- **Observed:** creating a pipeline whose shader declares a **float cube-array** texture
  (`texture_cube_array<f32>`) fails: `unexpected validation error: compute/render pipeline shader module
  must not be an error module`. The WGSL shader module became an **error module** at creation — yawgpu's
  WGSL frontend rejects/mishandles `texture_cube_array<f32>`.
- **Expected (WebGPU):** `texture_cube_array<f32>` is a valid filterable sampled-texture type; the shader
  compiles and the pipeline is valid. Dawn accepts it.
- **Status:** **RESOLVED** — yawgpu `8b42e5d`-era update; re-test `non_filterable_texture` `pass=160 fail=0`
  on **both HALs** (Metal + MoltenVK). Surfaced, not masked.

---

## F-058 — yawgpu: render-pipeline depth-stencil state over-requires depthCompare + depthWriteEnabled — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK both fail 10). Not in Dawn (passes all 1600).
- **Found by:** the `api,validation,render_pipeline,depth_stencil_state` port (10 cases).
- **Observed:** `unexpected validation error: render pipeline depth format requires depthCompare and
  depthWriteEnabled` — yawgpu rejects a render pipeline whose `depthStencil` uses a depth format but omits
  `depthCompare`/`depthWriteEnabled`, even in the cases where the WebGPU spec permits omitting them (the
  depth aspect is not actively read/written). yawgpu **over-validates**.
- **Expected (WebGPU):** `depthCompare`/`depthWriteEnabled` are only required when the depth aspect is used;
  Dawn accepts the 10 cases.
- **Status:** **RESOLVED** — yawgpu `8b42e5d`-era update; re-test `depth_stencil_state` `pass=1600 fail=0`
  on **both HALs** (Metal + MoltenVK). Surfaced, not masked.

---

## F-059 — yawgpu: storage-texture-format support gap in render-pipeline validation + WGSL — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK both fail ~366). Not in Dawn (passes all 744).
- **Found by:** the `api,validation,render_pipeline,misc` port — the `storage_texture,format` test
  (~366/744 cases).
- **Observed:** yawgpu rejects many storage-texture formats that the spec accepts:
  `unexpected validation error: pipeline auto layout storage texture format is unsupported` (~246),
  `storage texture binding format must support read-write storage access` (~36),
  `render pipeline auto layout storage texture format/access is unsupported` (~12); and ~74 cases produce
  `render pipeline vertex shader module must not be an error module` (the storage-texture WGSL type fails to
  compile). yawgpu's storage-texture-format support (write-only / read-write) is **narrower than the spec**,
  both in pipeline-layout validation and in the WGSL frontend.
- **Expected (WebGPU):** the spec's storage-capable formats (and read-write where the feature is present)
  are valid in an auto-layout render pipeline; Dawn accepts all 744.
- **Status:** **RESOLVED** — yawgpu `8b42e5d`-era update; re-test `render_pipeline/misc`
  `storage_texture,format` `pass=720 fail=0` on **both HALs** (Metal + MoltenVK). Surfaced, not masked.

---

## F-060 — yawgpu: WGSL compiler errors on `texture_external` (external-texture type) — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK both fail 2). Not in Dawn (passes both).
- **Found by:** the `api,validation,render_pipeline,misc` `external_texture` test (2 cases, `isAsync`
  {false,true}).
- **Observed:** a render pipeline whose WGSL binds a `texture_external` fails:
  `unexpected validation error: render pipeline vertex shader module must not be an error module` — yawgpu's
  WGSL frontend rejects/mishandles `texture_external` (same failure shape as the resolved F-057
  cube-array-float case). Dawn compiles it.
- **Expected (WebGPU):** `texture_external` is a valid WGSL sampled-texture type; the shader compiles. Dawn
  accepts it.
- **Status:** **RESOLVED** (yawgpu `fa97027`, 2026-06-09). Re-verified both HALs: `external_texture`
  `pass=2 fail=0` on Metal and Vulkan/MoltenVK. The fix lands full external-texture support on the **Metal**
  HAL; on Vulkan the prior SPIR-V hack was replaced with an honest rejection at the operation level (the
  validation test — shader/pipeline creation — passes on both). Surfaced, not masked.

---

## F-061 — yawgpu: render-pipeline over-rejects compatible pipeline-layout binding kinds — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == MoltenVK both fail 80). Not in Dawn (passes all).
- **Found by:** `api,validation,render_pipeline,resource_compatibility` (80/~15754 cases).
- **Observed:** `unexpected validation error: pipeline layout binding kind is incompatible with the shader
  binding` — yawgpu rejects an explicit pipeline layout whose binding kind IS compatible with the shader's
  resource binding (the spec/Dawn accept it). yawgpu **over-validates** binding-kind compatibility.
- **Status:** **RESOLVED** (yawgpu, 2026-06-09). Re-verified both HALs: `resource_compatibility`
  `pass=123 fail=0` on Metal and Vulkan/MoltenVK. Surfaced, not masked.

---

## F-062 — yawgpu: render-bundle over-rejects compatible attachment signatures — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == MoltenVK both fail 30). Not in Dawn (passes all).
- **Found by:** `api,validation,encoding,render_bundle` (30 cases).
- **Observed:** `unexpected validation error: render bundle attachment signature is incompatible with the
  render pass` — yawgpu rejects executing a render bundle whose attachment signature IS compatible with the
  render pass; Dawn accepts it. yawgpu **over-validates** the bundle/pass attachment-signature match.
- **Status:** **RESOLVED** (yawgpu, 2026-06-09). Re-verified both HALs: `render_bundle` `pass=21 fail=0`
  on Metal and Vulkan/MoltenVK. Surfaced, not masked.

---

## F-063 — yawgpu: inter-stage interpolation-sampling compatibility mis-validated — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == MoltenVK both fail 12). Not in Dawn (passes all).
- **Found by:** `api,validation,render_pipeline,inter_stage` (12 cases).
- **Observed:** 8 cases `unexpected validation error: render pipeline inter-stage interpolation sampling is
  incompatible` (over-reject valid inter-stage interpolation pairings) + 4 cases `expected validation error,
  got none` (under-validate — yawgpu misses a genuinely-incompatible pairing). yawgpu's inter-stage
  interpolation-sampling compatibility rule is both too strict and too lax in different spots.
- **Status:** **RESOLVED** (yawgpu, 2026-06-09). Re-verified both HALs: `inter_stage` `pass=26 fail=0`
  on Metal and Vulkan/MoltenVK. Surfaced, not masked.

---

## F-064 — yawgpu: WGSL frontend errors immediate-data shader modules — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == Vulkan/MoltenVK both fail 4). Dawn **skips** (this Dawn build
  reports `maxImmediateSize == 0`, so the test feature-gates off) — no direct oracle.
- **Found by:** `api,validation,pipeline,immediates` `pipeline_creation_immediate_size_mismatch` (4 cases,
  compute/render × isAsync).
- **Observed:** `unexpected validation error: compute/render pipeline … shader module must not be an error
  module` — yawgpu advertises `maxImmediateSize > 0` (so the port runs the test rather than skipping) but its
  WGSL frontend cannot compile a shader that declares immediate data, erroring the module. Same WGSL-frontend
  family as the resolved F-057 / F-060.
- **Expected (WebGPU):** if a backend reports immediate-data support, the immediate-data WGSL must compile;
  pipeline creation then fails (or not) on the size-mismatch rule the test targets — not on the shader module.
- **Status:** **RESOLVED** (yawgpu `f9a076e`; re-verified 2026-06-11 on Metal + MoltenVK, `pipeline/immediates` green).

---

## F-065 — yawgpu: error-scope out-of-memory type / filter handling — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == MoltenVK both fail 7). Not in Dawn (passes all 17).
- **Found by:** `api,validation,error_scope` (`simple`, `parent_scope`, `current_scope`; 7 cases).
- **Observed:** for an out-of-memory-triggering allocation yawgpu reports a **validation** error (`type=1`)
  instead of `out-of-memory` (`captured error type mismatch — expected out-of-memory, got type=1`), and
  `out-of-memory` / `internal` filtered scopes do not catch the expected type (`expected an uncaptured error,
  got none` / `scope did not catch the expected error type`). Dawn classifies and filters these correctly.
- **Expected (WebGPU):** an OOM allocation surfaces a `GPUOutOfMemoryError`, caught only by an
  `'out-of-memory'`-filtered scope; mismatched filters let it propagate as uncaptured.
- **Status:** **RESOLVED** (yawgpu `f9a076e` + `de7bae3`/`ef43eae`; re-verified 2026-06-11 on Metal + MoltenVK, `error_scope` green).

---

## F-066 — yawgpu: setViewport rejects an in-bounds viewport as out-of-bounds — cross-HAL

- **Backend:** yawgpu (cross-HAL, Metal == MoltenVK both fail 2). Not in Dawn (passes).
- **Found by:** `api,validation,encoding,cmds,render,dynamic_state` `setViewport,xy_rect_contained_in_bounds`
  (2 cases).
- **Observed:** `unexpected validation error: render pass viewport rectangle exceeds device bounds` — yawgpu
  rejects a viewport rectangle that **is** contained within the allowed bounds; Dawn accepts it. yawgpu's
  viewport-bounds validation is too strict.
- **Status:** **RESOLVED** (yawgpu `f9a076e`; re-verified 2026-06-11 on Metal + MoltenVK, `dynamic_state` green).

---

## F-067 — yawgpu: under-validates depth/stencil buffer copies & buffer device-mismatch — cross-HAL

- **Backend:** yawgpu (cross-HAL; Metal fails 15, MoltenVK fails 8 — the gap is MoltenVK feature-gating the
  single-aspect DS formats). Not in Dawn (passes all 692).
- **Found by:** `api,validation,image_copy,buffer_related` (`buffer,device_mismatch`, `bytes_per_row_alignment`).
- **Observed:** `expected validation error, got none` — yawgpu accepts copies the spec/Dawn reject:
  (a) an aspect-`all` buffer copy of a **combined** depth+stencil format (`depth24plus-stencil8`,
  `depth32float-stencil8`); (b) a `copyBufferToTexture` / `copyTextureToBuffer` whose buffer is from a
  **mismatched device**; (c) [Metal only] a non-256-aligned `bytesPerRow` for single-aspect DS formats
  (`stencil8`, `depth16unorm`, `depth32float`) — MoltenVK feature-gates those formats so they don't surface
  there.
- **Status:** **RESOLVED** (yawgpu `f9a076e`; re-verified 2026-06-11 on Metal + MoltenVK, `image_copy/buffer_related` green).

---

## F-068 — yawgpu: vertex-buffer OOB robustness broken for indirect draws — cross-HAL

- **Backend:** yawgpu (cross-HAL; Metal fails 89 unique cases, MoltenVK 129, 83 common — both dominated by
  `indirect=true`: 79/89 on Metal, 123/129 on MoltenVK). **Nondeterministic across runs** (65 unique on a
  second Metal sweep) — expected for unclamped OOB fetches that read whatever memory happens to hold; the
  indirect-dominated distribution is stable. wgpu-native passes all 1856 (so this is not shared-naga);
  Dawn passes (one worker-crash artifact re-ran clean in isolation).
- **Found by:** `shader,execution,robust_access_vertex` `vertex_buffer_access` (phase S1 / batch Y-1 port).
- **Observed:** `pixel(0,0) expected rgba={0,255,0,255}, got {255,0,0,255}` — the test shader detects
  out-of-bounds vertex-fetch data that should have been clamped/zeroed. Failures concentrate on
  **indirect** draws (`drawIndirect` / `drawIndexedIndirect`) across `vertexCount`/`instanceCount`/
  `baseVertex`/`firstVertex` overflows and all `float32*` attribute formats; a small non-indirect residue
  (10 Metal / 6 MoltenVK) also fails. yawgpu's vertex-robustness path is not applied (or applied with the
  wrong bounds) when draw parameters come from an indirect buffer.
- **Status:** **RESOLVED** (yawgpu `f857f3f` — Metal vertex pulling + Vulkan robustBufferAccess;
  re-verified 2026-06-11: **Metal green** (1856 cases) and **native Windows/Vulkan green** (user-confirmed
  2026-06-11). A **125-case MoltenVK-only residual** remains (indirect-dominated, 119/125
  `indirect=true`) — with native Vulkan clean this is a MoltenVK Vulkan→Metal robustness translation
  limitation, same class as F-033/F-045/F-053, not a yawgpu defect. Surfaced, not masked.

---

## F-069 — yawgpu: workgroup-memory loads read zeros (memory_layout) — Metal-dominant

- **Backend:** yawgpu (Metal-dominant: 55 cases fail on Metal that wgpu-native passes; only 6 of those also
  fail on MoltenVK). wgpu-native (upstream naga MSL) passes the same 55 on Metal — so this is yawgpu's
  Metal HAL or its naga-fork MSL emission, not shared-naga. Dawn passes all of `memory_layout`.
- **Found by:** `shader,execution,memory_layout` `read_layout`/`write_layout` (phase S1 / batch Y-1 port).
- **Observed:** `GPU buffer mismatch at byte 0: expected 42, got 0` — with `aspace="workgroup"`, data
  round-tripped through a `var<workgroup>` comes back as zeros (48 `read_layout` + 7 `write_layout`
  yawgpu-only cases; 54/55 are `workgroup`, plus 1 `uniform` straggler). Scalar, vector, matrix and
  array-of-matrix layouts all affected.
- **Status:** **RESOLVED** (yawgpu `a034b24`; re-verified 2026-06-11 — the 55 yawgpu-only cases pass on
  Metal; the naga-fork fix also cleared the previously shared workgroup `write_layout` set on Metal.
  Remaining `memory_layout` failures are tracked under F-070.)

---

## F-070 — shared-naga (yawgpu + wgpu-native): workgroup write_layout, struct_inner_align, matCx3 padding, loop shadowing

Recorded for completeness; these fail **identically on yawgpu and wgpu-native** (Dawn green), so they are
naga-lineage defects, not yawgpu-core defects. Deprioritized per the Y-batch focus.

- `memory_layout`: 48 shared cases — `write_layout` with `aspace="workgroup"` (44) and `struct_inner_align`
  across all address spaces (4). **Update 2026-06-11:** yawgpu's naga-fork fix (`a034b24`) cleared the
  workgroup `write_layout` set on **Metal** — the Metal residual is now `struct_inner_align` only
  (9 cases). The **MoltenVK** path still fails ~54 `memory_layout` cases (workgroup `write_layout`
  matrices/vectors, `struct_double_align`, `struct_inner_size_and_align`, `array_stride_size`) — the
  SPIR-V backend did not get the equivalent fix.
- `padding`: 16 shared cases on **Metal only** (`matCx3`/`array_of_matCx3` columns 2–4, plus struct cases) —
  implementation writes into matCx3 column padding bytes (`expected 239, got 0` at the padding byte).
  yawgpu-MoltenVK passes all but 2, so this is the naga **MSL** backend; yawgpu inherits it via its naga fork.
- `shadow`: `loop` fails on yawgpu Metal + MoltenVK + wgpu-native (`expected 0, got 239` at byte 0 — output
  never written) — naga mis-handles shadowing in `loop`; Dawn passes.
- **Status:** **OPEN** (naga lineage; affects yawgpu via its fork). Surfaced, not masked.

---

## F-071 — wgpu-native: zero_init fails massively; robust_access aborts the process

- **Backend:** wgpu-native only (yawgpu and Dawn pass both groups — `robust_access` Dawn 1626/1626,
  yawgpu Metal `fail=0 crash=0`).
- **Found by:** `shader,execution,zero_init` `compute,zero_init` and `shader,execution,robust_access`
  `linear_memory` (phase S1 / batch Y-1 port).
- **Observed:** (a) `zero_init`: 3930 subcase failures on Metal — wgpu-native does not zero-initialize the
  tested workgroup/private/function variables for most type/workgroup-size combinations. (b)
  `robust_access`: all 366 non-f16 case shards **abort** — pipeline creation fails validation
  (`ComputePipeline with '' label is invalid`) and the error surfaces as a Rust panic + `fatal runtime
  error` at `wgpuQueueSubmit` instead of a reportable error (same abort class as F-001).
- **Status:** **OPEN**; tracked as a **wgpu-native defect** (bring-up reference; to be reflected in
  `expectations/wgpu-native.*` on regen). Not masked.

---

## F-072 — yawgpu: zero-size map ranges fail — Metal-only

- **Backend:** yawgpu Metal only (MoltenVK passes all the same cases; Dawn passes everything;
  wgpu-native fails these groups too but for its own broader mapping defects — see F-075).
- **Found by:** `api,operation,buffers,map` `mapAsync,read` / `mapAsync,write` / `remapped_for_write` /
  `mappedAtCreation` (batch Y-2, phase Y2 port).
- **Observed:** `expected mapAsync success` — every subcase whose map region is **zero-sized** fails:
  the six `size=0` buffer subcases and the `size=12; range=[0,0]` explicit zero-length-range subcase, in
  every region-mode case (~93 unique cases). Mapping a zero-size buffer or a zero-length range is valid
  per spec (Dawn and yawgpu-Vulkan accept it); yawgpu's Metal HAL rejects/fails the map.
- **Status:** **RESOLVED** (yawgpu `726e8aa`; re-verified 2026-06-11, `buffers,map` green on Metal + MoltenVK).

---

## F-073 — yawgpu: panic-abort on OOM-sized mappedAtCreation buffer — cross-HAL

- **Backend:** yawgpu (cross-HAL: Metal and MoltenVK both abort with signal 6).
- **Found by:** `api,operation,buffers,map_oom` `mappedAtCreation` `oom=true;size=9007199254740984`.
- **Observed:** `shard worker aborted: signal 6 (Abort trap: 6)` — `wgpuDeviceCreateBuffer` with
  `mappedAtCreation=true` and a ~9 PB size **aborts the process** (Rust panic, likely an unchecked
  host-allocation/overflow) instead of failing gracefully (Dawn returns an unmappable buffer and raises
  no error; `getMappedRange` returns null).
- **Status:** **RESOLVED** (yawgpu `726e8aa`; re-verified 2026-06-11, `map_oom` green on Metal + MoltenVK — no abort).

---

## F-074 — yawgpu: queue.writeBuffer ordering vs prior submits broken — MoltenVK-only (native-Vulkan confirm pending)

- **Backend:** yawgpu Vulkan via MoltenVK only (Metal passes all 260 `multiple_buffers` cases; Dawn green).
  Like F-033, a MoltenVK translation artifact cannot be fully excluded until a native-Vulkan run, but the
  failure is an API-level ordering property, pointing at the yawgpu Vulkan HAL submission/staging path.
- **Found by:** `api,operation,memory_sync,buffer,multiple_buffers` `rw` (16) / `ww` (5).
- **Observed:** `GPU buffer mismatch at byte 0: expected 0, got 1` — all 21 failing cases have
  `boundary="queue-op"` and predominantly `writeOp="write-buffer"`: a `queue.writeBuffer` issued *after*
  a submitted read/write becomes visible to that earlier work (the read observes the later write), i.e.
  writeBuffer's staging upload is not ordered behind previously submitted command buffers.
- **Status:** **RESOLVED** (yawgpu `a034b24`; re-verified 2026-06-11, `multiple_buffers` 260 cases green on
  MoltenVK and Metal).

---

## F-075 — wgpu-native: buffer mapping broadly broken (586 fail/crash in `buffers,map`)

- **Backend:** wgpu-native only.
- **Found by:** `api,operation,buffers,map` (batch Y-2 port): `mapAsync,read` 129, `remapped_for_write`
  228, `mapAsync,mapState` 96, `mapAsync,write` 57, `typedArrayAccess` 54, `mappedAtCreation,mapState` 12,
  `unchanged_ranges_preserved` 10; 109 shard-worker crashes among them.
- **Observed:** widespread mapAsync/getMappedRange failures and process aborts on Metal.
- **Status:** **OPEN**; tracked as a **wgpu-native defect** (bring-up reference; to be reflected in
  `expectations/wgpu-native.*` on regen). Not masked.

---

## F-076 — yawgpu: anisotropic filtering broken — both HALs, differently

- **Backend:** yawgpu only (wgpu-native passes all 3 cases; Dawn passes all 3).
- **Found by:** `api,operation,sampling,anisotropy` (batch Y-3, phase Y3 port).
- **Observed:** on **Metal**, `anisotropic_filter_checkerboard` fails with
  `Render results with sampler.maxAnisotropy being 16 and 1024 should be the same.` — values above the
  hardware maximum are not clamped consistently, so two samplers that must behave identically render
  differently. On **MoltenVK**, all 3 cases (including `anisotropic_filter_mipmap_color`, which passes on
  Metal) fail with `queue submit cannot use an error command buffer` — an out-of-range `maxAnisotropy`
  appears to error sampler/pipeline creation instead of being clamped.
- **Status:** **RESOLVED** (yawgpu `726e8aa`; re-verified 2026-06-11, `anisotropy` 3/3 green on Metal + MoltenVK).

---

## F-077 — shared-naga: max-bindings shader invalid; yawgpu panics in the MSL writer instead of erroring

- **Backend:** naga lineage + yawgpu error-handling. Dawn (Tint) passes the same generated shader
  (1/1). wgpu-native (upstream naga) **aborts** the process; yawgpu (naga fork) **panics** at
  `naga/src/back/msl/writer.rs:391` `unreachable: module is not valid` inside
  `createRenderPipelineTracked` — i.e. naga validation rejected the module but yawgpu-core still invoked
  the MSL backend, converting a should-be-graceful `createShaderModule`/pipeline error into a
  non-unwinding panic → process abort. On MoltenVK (SPIR-V writer) yawgpu fails gracefully
  (`queue submit cannot use an error command buffer`).
- **Found by:** `api,operation,sampling,sampler_texture` `sample_texture_combos` — a generated WGSL using
  the device's full `maxSampledTexturesPerShaderStage` × `maxSamplersPerShaderStage` binding matrix.
- **Status:** **RESOLVED** (yawgpu `d376a1b` — naga storage-access fix + Metal per-kind/per-stage binding
  slots; re-verified 2026-06-11, `sampler_texture` green on Metal + MoltenVK, no panic). Note: the same
  commit introduced regressions tracked as F-078/F-081.

---

## F-078 — naga lineage: validator treats `let`-propagated indices as const-expression OOB (robust_access) — NOT a yawgpu regression

- **Backend:** naga lineage (yawgpu fork AND upstream — verified with naga-cli at both `f510a088` and
  `ecad2036`: identical rejection). Dawn (Tint) accepts and passes all 1626.
- **Found by:** `shader,execution,robust_access` `linear_memory` — every non-f16 subcase (1068 on yawgpu,
  cross-HAL identical; wgpu-native aborts on the same group, recorded under F-071 — same root).
- **Observed:** `uncaptured error: queue submit cannot use an error command buffer`. Root cause: naga
  const-propagates `let index = (3u);` into `s.data[index]` (array length 3) and raises a **static OOB
  validation error**; per WGSL a `let` is a runtime value, so this must be a runtime-clamped access —
  Tint is correct, naga over-validates.
- **History note (test oracle):** yawgpu's earlier `pass=1068` was a **false pass** — the invalid pipeline
  meant the dispatch never ran and the zero-initialized result buffer happened to equal the expected
  success value (0). The F-065 uncaptured-error wiring (2026-06-11) exposed this correctly; the
  "regression" classification in earlier revisions of this entry was wrong.
- **Status:** **OPEN** (naga-fork validator fix: do not treat `let`-propagated indices as constant
  expressions for OOB validation). Queued with the F-070 naga batch. Surfaced, not masked.

---

## F-079 — yawgpu regression: destroyed-resource errors fire outside the expected validation point — cross-HAL

- **Backend:** yawgpu (cross-HAL, identical on Metal and MoltenVK). New with the 2026-06-11 update.
- **Found by:** `api,validation,encoding,cmds,setBindGroup` `state_and_binding_index` (6: compute pass ×
  `state="destroyed"` buffer/texture) and `api,validation,queue,destroyed,query_set` `timestamps` (1).
- **Observed:** `uncaptured error: bind group buffer must not be destroyed` / `... texture must not be
  destroyed` / `render pass timestamp writes query set cannot use a destroyed query set` — the error now
  surfaces as an uncaptured error outside the point where the spec (and the test's error scope) expects
  it, instead of failing the expected call (submit-time validation).
- **Status:** **RESOLVED** (yawgpu `9382206` — submit-time destroyed validation; re-verified 2026-06-11 on
  Metal + MoltenVK, `setBindGroup` and `queue/destroyed/query_set` green).

---

## F-080 — yawgpu regression: filtering-sampler + unfilterable-float texture no longer rejected — cross-HAL

- **Backend:** yawgpu (cross-HAL, 32 cases identical on Metal and MoltenVK). Regression of the F-057-area
  validation, new with the 2026-06-11 update.
- **Found by:** `api,validation,non_filterable_texture` `non_filterable_texture_with_filtering_sampler`
  (`sampleType="unfilterable-float"` combinations; other sample types still pass).
- **Observed:** `expected validation error, got none` — pairing a filtering sampler with an
  `unfilterable-float` texture binding must fail pipeline validation; yawgpu now accepts it.
- **Status:** **RESOLVED** (yawgpu `9382206` — layout-aware filterable check; re-verified 2026-06-11 on
  Metal + MoltenVK, `non_filterable_texture` green, 160 cases).

---

## F-081 — yawgpu regression: external-texture pipelines error "missing params buffer slot" — cross-HAL

- **Backend:** yawgpu (cross-HAL, 2 cases identical on Metal and MoltenVK — the message references the MSL
  path but the Vulkan backend fails identically, so the shared binding-slot assignment is at fault).
  Regression of the F-060-area support, new with the 2026-06-11 update (suspect `d376a1b`).
- **Found by:** `api,validation,render_pipeline,misc` `external_texture` (isAsync=false/true).
- **Observed:** `uncaptured error: MSL external texture binding is missing params buffer slot` — creating
  a render pipeline that binds a `texture_external` errors.
- **Status:** **RESOLVED** (yawgpu `4770131` — fragment-only external textures regained their params
  buffer slot; re-verified 2026-06-11, Metal `external_texture` pass=2). Note: on **MoltenVK/Vulkan** the
  2 cases now fail with `external textures are not supported on the Vulkan backend` — that is the
  **deliberate** Vulkan-side rejection introduced by `fa97027` (F-060), surfaced honestly by the F-065
  error wiring (the earlier "pass=2 both HALs" record was a false pass on Vulkan, cf. F-078's lesson).
  Tracked as a documented yawgpu-Vulkan feature limitation, not a defect.

---

_Add new findings as `F-00N` with the same fields._
