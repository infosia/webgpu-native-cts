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

## Re-test summary — all yawgpu findings resolved (through T24b)

Every defect this suite has surfaced against **yawgpu** (the primary conformance subject) has been
fixed in yawgpu and re-confirmed on real hardware — the full intended cycle: the suite reports a
divergence, yawgpu fixes it, the fix is verified on real-GPU Metal (and, for `api,validation`, also
on Windows/Vulkan, NVIDIA). `expectations/yawgpu.txt` carries **no** expected-failure lines.

| milestone | yawgpu fix(es) | result after fix |
|-----------|----------------|------------------|
| `api,validation` baseline (`55ac04d`→`92db062`) | F-005/006/008/009/010 — `2667b0a`, `92db062` | `pass=2594 skip=16 fail=0 crash=0` |
| `createView` (T9–T11) | F-011 `41e007b`, F-014 `baa78cb` | clean (F-015 is wgpu-native-only) |
| `createBindGroupLayout` (T13–T16) | F-016 `4292f76`, F-018 `925520a` | `pass=4271 skip=377 fail=0` |
| `createPipelineLayout` (T18–T21) | F-020 `f75fc0a`, F-022 `798fc6a` | `pass=4332 skip=383 fail=0` |
| `api,operation` buffer/queue (T22–T23) | F-023 `e56f30a`, F-024 `c893eac` | `command_buffer,* pass=5` (Dawn-equal) |
| `image_copy` color (T24b) | F-025, F-026 — `1e6c70b` | `image_copy pass=137256 fail=0` |

**Resolved yawgpu findings:** F-005/006/008/009/010/011/014/016/018/020/022/023/024/025/026 — **all**
of them; each keeps a compact record below.
**Open — wgpu-native only:** F-001–F-004, F-007, F-012, F-013, F-015, F-017, F-019, F-021, F-027
(full detail retained). *(Real-GPU verification runs with the Bash sandbox disabled — see the F-023
note; under the macOS sandbox Metal enumerates no adapters and every case false-fails.)*

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

_Add new findings as `F-00N` with the same fields._
