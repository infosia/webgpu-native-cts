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

## Re-test summary — every yawgpu finding through T27 resolved on Metal (incl. F-031, F-032)

Every defect this suite surfaced against **yawgpu** (the primary conformance subject) through T26 — both
correctness and resource-lifetime — was fixed in yawgpu and re-confirmed on real hardware (the full
intended cycle: the suite reports a divergence, yawgpu fixes it, the fix is verified on real-GPU Metal,
and for `api,validation` + the Vulkan-specific findings on Windows/Vulkan, NVIDIA). The **T26**
depth/stencil `copyTextureToTexture` port surfaced
[F-031](#f-031--yawgpu-diverges-on-the-depth-aspect-of-copytexturetotexture-copied-depth-fails-an-equality-re-render)
(the **depth aspect** of `copyTextureToTexture`), now **resolved** — the root cause was that yawgpu's
regular (non-tiled) real-backend render path had no depth support (seven gaps; see F-031). Re-test:
`copy_depth_stencil` `pass=216 fail=0` (Dawn-equal). `expectations/yawgpu.txt` carries **no** expected-failure
lines (F-031 surfaced and fixed, not masked).

The last two yawgpu findings are now resolved too, verified on real-GPU Windows/Vulkan (NVIDIA):
[F-029](#f-029--yawgpu-leaks-vulkan-device-resources-across-image_copy-cases-later-tests-in-the-same-process-fail)
(a **cross-test** Vulkan device-resource leak in `image_copy`) and
[F-030](#f-030--yawgpu-map_read-readback-reads-the-buffer-before-the-gpu-copy-completes-intermittent-zeros)
(an intermittent `MAP_READ` readback race the F-029 fix un-masked).

The **T27** `image_copy` depth/stencil ports surfaced
[F-032](#f-032--yawgpu-returns-zeros-for-depthstencil-aspect-buffertexture-copies-except-plain-stencil8)
— yawgpu zeroed out the **depth aspect** of `copyTextureToBuffer` (all depth formats) and the **stencil
aspect** of the packed depth+stencil formats (plain `Stencil8` was fine), where Dawn and wgpu-native
pass all 1152 — now **resolved** (`c8f15d5`, `af9ac5c`): `image_copy` depth/stencil `pass=1152 fail=0`,
full `image_copy pass=138408 fail=0`. Surfaced, not masked. **yawgpu now has no open Metal findings.**

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

**Resolved yawgpu findings:** F-005/006/008/009/010/011/014/016/018/020/022/023/024/025/026/029/030/031/032/034/035/037/038/039
— each keeps a compact record below.
**Open — yawgpu: none.**
[F-039](#f-039--yawgpu-two-dispatches-in-one-compute-pass-lose-their-writes-under-batch-execution--cross-hal)
(`two_dispatches_in_the_same_compute_pass` read back `0` instead of `2` under batch/`--isolate` —
`memory_sync/buffer/single_buffer`, T35) is now **resolved** (`89f25df`, real-GPU Metal + Vulkan/MoltenVK:
`pass=25 fail=0` across all run modes, was `pass=24 fail=1` in batch); the root cause was yawgpu treating
the whole compute pass as one usage scope instead of per-dispatch, and it reproduced on Metal +
Vulkan/MoltenVK, which localized it to yawgpu's shared layer.
[F-038](#f-038--yawgpu-mishandles-stencil-operations-compare-and-masks--cross-hal)
(yawgpu mishandled stencil ops/compare/masks — `rendering/stencil`, T33) is now **resolved** (`40f5d7f`,
real-GPU Metal + Vulkan/MoltenVK: `pass=188 fail=0`, was `pass=97 fail=91`); the single root cause was the
dynamic stencil reference (`setStencilReference`) not being threaded to the HAL, and it reproduced
byte-identically on Metal + Vulkan/MoltenVK, which localized it to yawgpu's shared state path.
[F-037](#f-037--yawgpu-metal-hal-non-deterministic-depth-attachment-renderreadback-race)
(a **Metal-HAL-only** non-deterministic flake on the `rendering/depth` ports, T32 — the drawn point's
output intermittently read back as the clear value) is now **resolved** (`186cd54`): the root cause was
yawgpu's Metal HAL not emitting `[[point_size]]` for **`point-list`** pipelines (the depth tests are the
suite's first point-list users), so the point size was undefined on Metal; re-test `pass=130 fail=0`
across 11 consecutive runs (was `fail≈33–44`), neighboring triangle-list rendering unaffected
(`pass=589 fail=0`). Vulkan/MoltenVK, Dawn, and wgpu-native/Metal were always clean.
[F-035](#f-035--yawgpu-ignores-gpucolortargetstate-blend-and-writemask-writes-the-raw-fragment-output--cross-hal)
(yawgpu ignored `GPUColorTargetState` `blend` + `writeMask`, writing the raw fragment output to all
channels — `rendering/color_target_state`, T31) is now **resolved** (`74f5ef2`, real-GPU Metal +
Vulkan/MoltenVK: `pass=23 fail=0`, was `pass=2 fail=21`); it reproduced byte-identically on Metal +
Vulkan/MoltenVK, which localized it to yawgpu's shared color-target-state translation.
[F-034](#f-034--yawgpu-a-fragment-storage-write-is-lost-on-indexed--indirect-draws) (yawgpu didn't execute
**indexed/indirect** draws — `rendering/draw`, T30) is now **resolved** (`36a6b66`, real-GPU Metal:
`pass=564 fail=0`, was `340 fail=224`); it reproduced byte-identically on Metal + Vulkan/MoltenVK, which
localized it to yawgpu's shared draw path. The depth/stencil findings are all resolved too: confirmed on
**native Windows/Vulkan, NVIDIA RTX 5060 Ti, 2026-06-04** — F-031 `copy_depth_stencil`
`pass=216 fail=0` (`cac328a`) and F-032 `image_copy` depth/stencil `pass=1152 fail=0` (`3c847ac`, up from
`pass=352 fail=800`); the full ported suite on native Windows/Vulkan is green — **all 7596 ported cases**
pass or skip (`pass=7208 skip=388 fail=0`, a per-**case** count; the per-test `pass=…` totals elsewhere in
this file are per-**subcase**, so they are much larger). The **GLES** HAL is the only remaining untested
follow-up (not a known defect). F-033 is a **confirmed** MoltenVK-only Mac artifact, not a yawgpu defect.
**Open — wgpu-native only:** F-001–F-004, F-007, F-012, F-013, F-015, F-017, F-019, F-021, F-027,
F-028, F-036 (abort when a constant-factor blend draws without `setBlendConstant`; `color_target_state`,
T31) (full detail retained). *(Real-GPU verification runs with the Bash sandbox disabled — see the
F-023 note; under the macOS sandbox Metal enumerates no adapters and every case false-fails.)*
**Tooling / environment (not a backend conformance defect):** F-033 — color `copyTextureToTexture`
pixel mismatches when yawgpu's Vulkan HAL is run on **Mac via MoltenVK**; a **confirmed** MoltenVK
translation artifact — native Windows/Vulkan does **not** exhibit it (`pass=7208 skip=388 fail=0`, all
7596 cases), low priority.

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

- **Backend:** yawgpu (`1297b7e`, Metal). **Not** present in Dawn **or** wgpu-native — both pass all 216 cases.
- **Found by:** `api/operation/command_buffer/copyTextureToTexture:copy_depth_stencil` (T26) — the first
  ported test that exercises **depth-attachment rendering + depth/stencil-aspect copies**. It renders a
  per-layer depth (`depthCompare:'always'`, depth = `0.5 + 0.2·sin(layer)`) into the source,
  `copyTextureToTexture`-copies the subresource, then re-renders against the destination with
  `depthCompare:'equal'` (writing green where the copied depth equals the freshly-computed depth) and
  asserts the colour output is all green; the stencil aspect is checked by a byte-exact
  `copyTextureToBuffer` readback.
- **Observed on yawgpu:** every **depth** format fails — `Depth16Unorm`, `Depth24Plus`,
  `Depth24PlusStencil8`, `Depth32Float`, `Depth32FloatStencil8` (enum 45–49) are each `fail=36/36`; the
  green re-render produces **no green** (`GPU buffer mismatch at byte 1: expected 255, got 0` — the G
  channel is 0, so the `equal` depth test passed nowhere). **`Stencil8`** (enum 44, stencil-only, no depth
  aspect) **passes 36/36** — so the **stencil-aspect copy works; the depth-aspect path does not**.
  Deterministic; all 180 depth subcases fail identically. Full run `pass=36 fail=180`.
- **Expected (WebGPU):** the copied depth must equal the source depth, so the `depthCompare:'equal'`
  re-render is all green. Dawn and wgpu-native both pass all 216 (`pass=216 fail=0` each).
- **Likely root:** the **depth aspect is not preserved across `copyTextureToTexture`** (the destination
  depth ≠ the rendered source depth, so `equal` fails everywhere), while the stencil aspect copies
  correctly. Whether the gap is in the depth-aspect copy itself or in yawgpu's depth-attachment
  render/compare path is for the yawgpu side to localize; the stencil-only pass isolates it to the
  **depth** path. (This is the suite's first depth-render + depth-copy coverage — newly exercised, not a
  regression.)
- **Status:** **RESOLVED** (2026-06-03, real-GPU Metal; yawgpu `f3afc31`). The depth aspect failed not
  because of the copy but because yawgpu's **regular (non-tiled) real-backend render path had no depth
  support** — the
  depth init/verify render passes never ran correctly. Seven gaps were fixed in sequence: (1) render-pass
  depth-stencil attachment binding + (2) no-colour render passes; (3) render-pipeline depth-stencil +
  vertex-only (no-fragment) pipelines; (4) `MTLCompileOptions.preserveInvariance` for cross-pipeline
  depth equality; (5) separate vertex/fragment shader modules on Metal (per-stage MSL + two libraries);
  (6) render-attachment mip-level/array-layer targeting; (7) over-strict depth/stencil copy validation
  (the "single 2D layer" + "origin-zero full-subresource" rules were applied to multi-layer / layer-ranged
  `copyTextureToTexture` and multi-layer stencil write/`copyTextureToBuffer`; corrected to full-w/h-at-zero-
  x/y-origin while allowing layer ranges, matching WebGPU/Dawn). Re-test: `copy_depth_stencil`
  `pass=216 fail=0` (Dawn-equal, up from `pass=36 fail=180`); full `copyTextureToTexture` `pass=31126
  fail=0`; `image_copy` regression `pass=137256 fail=0`. 3-way confirmed (Dawn + wgpu-native pass all 216).
  No `expectations/yawgpu.txt` lines were ever added (surfaced for the fix, not masked). **Note (Vulkan):**
  the `f3afc31` fix landed in the **Metal** HAL only — initially confirmed on Mac via MoltenVK that the
  Vulkan backend still failed `copy_depth_stencil` `pass=36 fail=180` (byte-identical to the pre-fix
  profile). yawgpu then ported the depth render path to the **Vulkan HAL** in `cac328a`
  ("resolve depth copyTextureToTexture on the Vulkan HAL"); first re-confirmed (2026-06-04, Mac via MoltenVK —
  `CTS_YAWGPU_BACKEND=vulkan`): `copy_depth_stencil` `pass=216 fail=0` (Dawn-equal), then **confirmed on
  native Windows/Vulkan (NVIDIA RTX 5060 Ti, 2026-06-04): `copy_depth_stencil` `pass=216 fail=0`** — so
  F-031 is fixed on the Vulkan HAL with no residual MoltenVK doubt. **GLES** remains the untested follow-up.

---

## F-032 — yawgpu returns zeros for depth/stencil aspect buffer⇄texture copies (except plain Stencil8)

- **Backend:** yawgpu (`f3afc31`, real-GPU Metal). **Not** present in Dawn or wgpu-native.
- **Found by:** the T27 `image_copy` depth/stencil ports
  (`api,operation,command_buffer,image_copy:rowsPerImage_and_bytesPerRow_depth_stencil` and
  `:offsets_and_sizes_copy_depth_stencil`) — the suite's first image_copy depth/stencil aspect
  coverage. **Dawn (oracle) and wgpu-native both pass all 1152** subcases with the exact same harness
  code; **yawgpu passes 288, fails 864** (9 of the 12 case-level combos), which isolates the behavior
  to the backend.
- **Observed on yawgpu (every failure reads back `got 0`):**
  - **Depth aspect** `copyTextureToBuffer` (depth-only) — **all three depth formats** zero out:
    `Depth16Unorm` (`expected 255, got 0` at byte 0 — the `1.0`→`0xFFFF` texel), `Depth32Float` and
    `Depth32FloatStencil8` (`expected 128, got 0` at byte 2 — the `1.0f`→`00 00 80 3F` texel). The test
    stages depth by sampling an `r32float` source in a fragment shader that writes `@builtin(frag_depth)`,
    then copies the depth aspect to a buffer; yawgpu yields all zeros.
  - **Stencil aspect** of the **combined** depth+stencil formats — `Depth24PlusStencil8` and
    `Depth32FloatStencil8` zero out for all of `WriteTexture` / `CopyB2T` / `CopyT2B` (stencil-only)
    (`expected 1, got 0`).
  - **Plain `Stencil8` passes** all three stencil methods (288 subcases) — a standalone single-plane
    stencil texture copies correctly; only the **aspect extraction from a packed depth+stencil
    texture** and the **depth plane** are broken.
- **Expected (WebGPU):** buffer⇄texture copies of the depth aspect (`Depth16Unorm`/`Depth32Float`/
  `Depth32FloatStencil8`) and the stencil aspect (all stencil formats) must round-trip the aspect's
  bytes. Dawn and wgpu-native do.
- **Scope / localization:** distinct from [F-031](#f-031--yawgpu-diverges-on-the-depth-aspect-of-copytexturetotexture-copied-depth-fails-an-equality-re-render)
  (that was the depth aspect of *`copyTextureToTexture`*, fixed in `f3afc31`). F-032 is the depth/stencil
  aspect of *buffer⇄texture* copies (`writeTexture` / `copyBufferToTexture` / `copyTextureToBuffer`).
  Two sub-gaps: (a) depth-plane copy/extraction (or the frag-depth render staging not surviving a depth
  T2B), and (b) stencil-plane extraction from a packed depth+stencil texture (plain `Stencil8` works,
  so it is the packed-aspect path, not stencil copies in general).
- **Status:** **RESOLVED** (2026-06-04, real-GPU Metal; yawgpu `c8f15d5` + `af9ac5c`). yawgpu added
  depth/stencil aspect buffer-copy support (depth-plane copy + packed-aspect extraction) and corrected
  the packed-depth aspect-copy buffer-size validation. Re-test: `image_copy` depth/stencil
  `pass=1152 fail=0` (Dawn-equal, up from `pass=288 fail=864`); full `image_copy` now `pass=138408
  fail=0` (137256 color + 1152 depth/stencil); combined `command_buffer` cross-test `pass=33937 fail=0`.
  3-way confirmed (Dawn + wgpu-native pass all 1152). No `expectations/yawgpu.txt` lines were ever added
  (surfaced for the fix, not masked). It was surfaced only because the T27 readback buffers are
  zero-initialized (a copy that writes nothing fails instead of being masked by a pre-filled expected
  buffer).
- **Note (Vulkan) — RESOLVED on the Vulkan HAL (`3c847ac`):** the original F-032 fix (`c8f15d5`/`af9ac5c`)
  was **Metal**-only, so the Vulkan HAL initially still failed the aspect buffer copies. This was confirmed
  to be a real HAL gap (not a MoltenVK artifact) on **native Windows/Vulkan (NVIDIA RTX 5060 Ti):
  `pass=352 fail=800`, byte-identical to MoltenVK** — the 800 fails (`got 0`) being stencil-plane
  extraction from packed depth+stencil formats (576: `Depth24PlusStencil8` + `Depth32FloatStencil8` stencil
  aspect, all three methods) plus depth-aspect `copyTextureToBuffer` (224). yawgpu then ported the
  depth/stencil-aspect buffer-copy support to the **Vulkan HAL** in `3c847ac` ("resolve depth/stencil
  aspect buffer copies on the Vulkan HAL"); **confirmed on native Windows/Vulkan (NVIDIA RTX 5060 Ti,
  2026-06-04): `image_copy` depth/stencil `pass=1152 fail=0`** (Dawn-equal, up from `pass=352 fail=800`),
  with the full ported suite green (`pass=7208 skip=388 fail=0`) and no `copy_depth_stencil` regression
  (F-031 still `216/0`). So F-032 is now fixed on Metal **and** Vulkan. **GLES** remains the only untested
  HAL (follow-up, not a known defect).

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

- **Backend:** yawgpu (`af9ac5c`, real-GPU Metal). **Not** present in Dawn or wgpu-native.
- **Found by:** the T30 `rendering/draw` ports (`arguments`, `default_arguments`) — the suite's first
  vertex/index-buffer + multi-variant draw coverage. **Dawn (oracle) and wgpu-native both pass all 744**
  with the exact same harness code; **yawgpu fails 224** (`pass=340 fail=224`, plus 183 feature-gated
  skips for `indirect-first-instance`), which isolates the behavior to the backend.
- **Observed (all 224 failures are `result` buffer `expected 1, got 0`):** the test's fragment shader
  writes `result.value = 1u` to a `@group(0) @binding(0) var<storage, read_write>` whenever it runs; the
  read-back is **0** (the write didn't take effect) for:
  - **indexed** draws — `indexed=true` (both direct `drawIndexed` 128 and `drawIndexedIndirect` 64), and
  - **indirect** non-indexed draws — `indexed=false; indirect=true` (`drawIndirect`, 16).
  - **Plain `draw` (`indexed=false; indirect=false`) passes** — the fragment storage write works there.
  `default_arguments` mirrors it: the 8 `draw` subcases pass, the 16 `drawIndexed` subcases fail.
- **Expected (WebGPU):** a fragment-stage `read_write` storage write must take effect on every draw
  variant. Dawn and wgpu-native do.
- **Scope / localization:** the divergence is on the **`drawIndexed` / `drawIndirect` /
  `drawIndexedIndirect`** paths specifically (the index-buffer and/or indirect-buffer draw encoding);
  the plain `draw` path is fine. The reported failure is the fragment **storage side-effect**
  (`result==0`); the test's first-failure report doesn't say whether the raster (the green pixels) also
  diverges on these paths — **for yawgpu to localize** (fragment storage binding/flush on indexed/indirect
  draws vs the draw not rasterizing at all).
- **Cross-HAL (not HAL-specific):** re-run on Mac via MoltenVK (yawgpu `3c847ac`, **Vulkan** HAL,
  `CTS_YAWGPU_BACKEND=vulkan`) — **byte-identical** to Metal: `pass=340 fail=224`, same
  `indexed=true (128+64)` + `indexed=false;indirect=true (16)` pattern, same `result==0`. The
  indexed/indirect draw + fragment storage write are MoltenVK-supported (Dawn/wgpu-native pass), so this
  is **not** a MoltenVK artifact (unlike F-033) and **not** Metal-specific — it points at yawgpu's
  **shared (HAL-agnostic) indexed/indirect draw path**, not a per-HAL backend.
- **Status:** **RESOLVED** (2026-06-05, real-GPU Metal; yawgpu `36a6b66`). The root cause was that
  yawgpu **did not execute indexed / indirect draws** at all (the `drawIndexed` / `drawIndirect` /
  `drawIndexedIndirect` paths weren't issued) — so neither the raster nor the fragment storage write
  happened (the `result==0` was the first-reported symptom). yawgpu `36a6b66` ("implement indexed /
  indirect draw execution") adds those paths. Re-test: `rendering/draw` `pass=564 fail=0` (Dawn/wgpu-equal,
  up from `pass=340 fail=224`); V1/V2 unaffected. 3-way confirmed (Dawn + wgpu-native pass all 744). No
  `expectations/yawgpu.txt` lines were ever added (surfaced for the fix, not masked). The cross-HAL
  reproduction above (Metal == Vulkan/MoltenVK) correctly localized it to yawgpu's shared draw path.

---

## F-035 — yawgpu ignores `GPUColorTargetState` `blend` and `writeMask` (writes the raw fragment output) — cross-HAL

- **Backend:** yawgpu (`36a6b66`, real-GPU **Metal** and **Vulkan/MoltenVK** — byte-identical). **Not**
  present in Dawn (oracle passes all).
- **Found by:** the T31 (V4) `rendering/color_target_state` ports — the suite's first
  `GPUColorTargetState` `blend` + `writeMask` coverage (`color_write_mask,{channel_work,
  blending_disabled}`, `blend_constant,{initial,setting,not_inherited}`). **Dawn (oracle) passes all 23**
  (`pass=23 fail=0`) with the exact same harness code; **yawgpu fails 21** (`pass=2 fail=21`), which
  isolates the behavior to the backend.
- **Observed (every failure is `expected 0` — or `0.5` — `got 1`: a channel that should be masked off
  or blended down is written full-scale):** yawgpu writes the raw (clamped) fragment output to all four
  channels, ignoring **both** fields of `GPUColorTargetState`:
  - **`writeMask` ignored** — `color_write_mask,channel_work` fails 15/16 (only `mask=15`, all channels,
    passes); `mask=0` (write nothing) returns all-`1`. `color_write_mask,blending_disabled` fails both
    subcases (`writeMask=RED`, yet green reads `1`). All four channels are always written regardless of
    the mask.
  - **`blend` ignored** — `blend_constant,{initial,setting,not_inherited}` use a `srcFactor=constant`
    blend; `src * blendConstant` is **not** applied — the raw source (clamped to `1`) is written. Only
    `setting:{r:1,g:1,b:1,a:1}` passes (there `constant=1` ⇒ `src*1=src`, indistinguishable from the bug).
  - The unifying tell: a case passes **iff** its expected result equals the raw source (`1`); whenever
    `writeMask` or `blend` should pull a channel **below** the source, yawgpu emits the source.
- **Expected (WebGPU):** the per-target `writeMask` gates which channels the pipeline writes, and `blend`
  (here `src*constant + dst*…`) is applied before the unorm store. Dawn does both; wgpu-native honors
  `writeMask` (`color_write_mask,*` `pass=18`, and would honor `blend` but for the unrelated F-036 abort).
- **Likely root cause:** yawgpu reads only `WGPUColorTargetState.format` and does **not** propagate
  `.writeMask` / `.blend` into the HAL render-pipeline color-attachment state. (Two observable symptoms;
  could be one color-target-state translation gap or two — **for yawgpu to localize**.)
- **Cross-HAL (not HAL-specific):** Metal (`target/release`) and Vulkan/MoltenVK (`target-vulkan`,
  `CTS_YAWGPU_BACKEND=vulkan`) are **byte-identical** — `pass=2 fail=21`, same case-by-case pattern.
  `writeMask` + blending are MoltenVK-supported (Dawn/wgpu-native honor them), so this is **not** a
  MoltenVK artifact (unlike F-033) and **not** Metal-specific — it points at yawgpu's shared
  (HAL-agnostic) pipeline color-target-state translation.
- **Status:** **RESOLVED** (2026-06-05, real-GPU Metal **and** Vulkan/MoltenVK; yawgpu `74f5ef2` —
  "apply color-target blend + writeMask + blend constant"). Re-test: `color_target_state`
  `pass=23 fail=0` on **both** HALs (Dawn-equal, up from `pass=2 fail=21`). Surfaced, not masked
  (`expectations/yawgpu.txt` never carried a line for it). The cross-HAL reproduction (Metal ==
  Vulkan/MoltenVK) correctly localized it to yawgpu's shared color-target-state translation.

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

- **Backend:** yawgpu **Metal HAL** (`74f5ef2`, real-GPU Metal). **Not** present in Dawn (Metal),
  wgpu-native (Metal), or yawgpu's **own Vulkan/MoltenVK HAL** — all three pass `130/130` deterministically.
- **Found by:** the T32 (V5) `rendering/depth` ports — the suite's first standalone depth-stencil-
  **attachment** render path (a render pass with a color **and** a depth-stencil attachment +
  `GPUDepthStencilState`, depth via the vertex `z`). **Dawn (oracle) passes all 130 deterministically
  (×3); yawgpu Metal is flaky.**
- **Observed (non-deterministic):** running the full 130-case set in one process gives `fail≈33–44`,
  **varying run to run** (`fail=44`, then `37`, then `40`, …); ~43/44 of the mismatches are
  `expected 1, got 0` — the drawn point's color reads back as the render-pass **clear/background value**,
  i.e. the draw's output is **intermittently missing**. The failing *set* changes every run. Crucially,
  **every case passes when run alone in a fresh process** (e.g. `reverse_depth:reversed=true`,
  `depth_compare_func …Always;clear=0`, `depth_test_fail`, `depth_disabled` — each `20/20` / `10/10`),
  and `--isolate` over the whole set is *also* flaky — so it is not a single bad case and not one-process
  cross-case poisoning per se, but a **timing/synchronization race that surfaces under batch execution**.
- **Expected (WebGPU):** a depth-attachment render pass must complete (and be ordered) before the
  subsequent `copyTextureToBuffer` snapshot reads the color attachment. Dawn, wgpu-native/Metal, and
  yawgpu/Vulkan all guarantee this.
- **Localization (tight):** the **color**-only render+readback paths on the *same* yawgpu Metal HAL are
  deterministic at much larger scale — T30 `rendering/draw` (`pass=564`) and T31 `color_target_state`
  (`pass=23`) never flaked — so it is **not** readback volume in general. The differentiator is the
  **depth-stencil attachment**. Combined with Vulkan/MoltenVK being clean, this points at a missing/weak
  **synchronization (fence/barrier) on yawgpu's Metal HAL** between a depth-attachment render pass and the
  following texture→buffer copy (the color result is treated as ready before the pass actually finishes),
  or a per-pass Metal resource that isn't fenced — **for yawgpu to localize**.
- **Not cross-HAL (unlike F-035), not Metal-generic (unlike a driver issue):** Metal-HAL-specific to
  yawgpu. (Contrast F-033, a MoltenVK-only artifact; this is the opposite — the *native* Metal HAL is the
  one that races, while the Vulkan-via-MoltenVK HAL is clean.)
- **Status:** **RESOLVED** (2026-06-05, real-GPU Metal; yawgpu `186cd54` — "emit `[[point_size]]` for
  Metal point-list pipelines"). **Root cause: not a sync race** (the hypothesis above was wrong) — yawgpu's
  Metal HAL did not emit a `[[point_size]]` from the vertex function for **`point-list`** pipelines, so the
  rendered point's size was **undefined** on Metal and the single pixel was intermittently not covered
  (hence the non-determinism and the `expected 1, got 0`). The depth tests are simply the suite's **first
  `point-list` users** (T30/T31 use `triangle-list`), so only they flaked — the depth attachment was
  incidental; the **point-list topology** was the real differentiator. Vulkan defaults point size to `1.0`
  (and Dawn/wgpu-native emit it for Metal), which is why every other path was clean. Re-test:
  `rendering/depth` `pass=130 fail=0` across **11 consecutive runs** (8 sequential + 3 `--workers 8`), no
  flakiness; neighboring rendering (`basic`+`draw`+`color_target_state`, all triangle-list)
  `pass=589 fail=0` (no regression). Metal-HAL-only fix; Vulkan/MoltenVK was always clean. Surfaced, not
  masked — no `expectations/yawgpu.txt` entry was ever added.

---

## F-038 — yawgpu mishandles stencil operations, compare, and masks — cross-HAL

- **Backend:** yawgpu (`186cd54`, real-GPU **Metal** and **Vulkan/MoltenVK** — byte-identical). **Not**
  present in Dawn or wgpu-native (both pass all 188).
- **Found by:** the T33 (V5b) `rendering/stencil` ports — the suite's first stencil pipeline state
  (`setStencilReference`, the `GPUStencilFaceState` `compare`/`failOp`/`passOp`/`depthFailOp`,
  `stencilReadMask`/`stencilWriteMask`). **Dawn (oracle) and wgpu-native both pass all 188** with the
  identical harness; **yawgpu fails 91** (`pass=97 fail=91`), **deterministic** (same set across runs),
  which isolates it to the backend. (The tests verify stencil state via a color readback — green if the
  stencil test passes / the resulting stencil equals the expected value, base color otherwise.)
- **Observed (three deterministic facets):**
  - **Stencil operations** — `invert`, `increment-clamp`, `increment-wrap`, `decrement-wrap` produce the
    **wrong** resulting stencil value (the `equal`-compare verification reads back base, not green);
    `keep`, `zero`, `replace`, `decrement-clamp` pass (some likely coincidentally). Hits
    `stencil_passOp_operation` (7/12 rows), `stencil_failOp_operation` (8/13), and
    `stencil_depthFailOp_operation` (8/13).
  - **Stencil compare** — 8/24 `stencil_compare_func` rows fail; the failing pattern is exactly the set
    where the result should depend on `reference vs stored-stencil(1)` — yawgpu instead returns the
    **reflexive** result (passes for `equal`/`less-equal`/`greater-equal`/`always`, fails for
    `less`/`greater`/`not-equal`/`never`, regardless of `reference`), i.e. the **stored stencil value is
    not correctly used** in the comparison.
  - **Stencil read/write masks** — `stencil_read_write_mask` fails 6/12 (the `stencilReadMask` /
    `stencilWriteMask` gating diverges the same way: the masked compare wrongly passes).
  - `stencil_reference_initialized` **passes** (it only exercises `equal(0,0)`, which is reflexive-true).
- **Expected (WebGPU):** the stencil test is `f(reference & readMask, storedStencil & readMask)`, and each
  `GPUStencilOperation` updates the stored stencil per spec. Dawn and wgpu-native do both.
- **Cross-HAL (not HAL-specific):** Metal (`target/release`) and Vulkan/MoltenVK (`target-vulkan`,
  `CTS_YAWGPU_BACKEND=vulkan`) fail the **byte-identical 91-case set** (`diff` empty). So this is **not** a
  per-HAL stencil-op/compare enum mapping (those differ between Metal and Vulkan) — it is in yawgpu's
  **shared (HAL-agnostic) stencil state translation**. (Contrast F-037, which was Metal-HAL-only.)
- **Status:** **RESOLVED** (2026-06-05, real-GPU Metal **and** Vulkan/MoltenVK; yawgpu `40f5d7f` —
  "thread dynamic stencil reference to the HAL"). **Single root cause: the dynamic stencil reference
  (`setStencilReference`) was not threaded through to the HAL** — so everything that depends on it was
  scrambled at once (the base `replace` wrote against the wrong reference, the verify `equal` compared
  against the wrong reference, and the masked compares diverged). That single gap is why the three
  observed facets above (ops / compare / masks) never reconciled into one black-box hypothesis — they were
  all downstream of the missing reference. Re-test: `rendering/stencil` `pass=188 fail=0` on **both** HALs
  (Dawn/wgpu-equal, up from `pass=97 fail=91`); neighboring rendering + compute `pass=720 fail=0` (no
  regression). The cross-HAL reproduction (Metal == Vulkan/MoltenVK) correctly localized it to yawgpu's
  shared state path. Surfaced, not masked — no `expectations/yawgpu.txt` entry was ever added.

---

## F-039 — yawgpu: two dispatches in one compute pass lose their writes under batch execution — cross-HAL

- **Backend:** yawgpu (`40f5d7f`, real-GPU **Metal** and **Vulkan/MoltenVK** — identical). **Not** present
  in Dawn or wgpu-native (both pass in every mode).
- **Found by:** the T35 (V7) `memory_sync/buffer/single_buffer` port —
  `two_dispatches_in_the_same_compute_pass` (two compute dispatches write `1` then `2` to one storage
  buffer **in the same pass**, which the spec orders ⇒ expect `2`). **Dawn and wgpu-native pass it in
  every mode; yawgpu reads back `0`** (the buffer's initial value — **neither** dispatch's write is
  visible) **only under batch / `--isolate` execution** (`pass=24 fail=1`), yet it **passes deterministically
  (10/10 Metal, 5/5 Vulkan) when run as the sole query in a fresh process.**
- **Observed:** `expected 2, got 0`. Deterministic — the full `single_buffer:*` run fails this one case
  every time (Metal **and** Vulkan/MoltenVK). The `rw`/`wr`/`ww` cross-boundary cases (single dispatch per
  pass, storage writes/reads/copies across separate command buffers and submits) **all pass even in the
  same batch** — so it is **specific to the multiple-dispatches-in-one-pass path**, not storage compute or
  the readback helper in general (the same `expectGPUBufferValuesEqual` readback verifies `rw`/`wr`/`ww`).
- **Expected (WebGPU):** dispatches in the same compute pass are ordered; after the two writes the buffer
  holds `2`, and that result is visible to a subsequent `copyBufferToBuffer` readback. Dawn and
  wgpu-native do this in all modes.
- **Cross-HAL (not HAL-specific):** Metal (`target/release`) and Vulkan/MoltenVK (`target-vulkan`,
  `CTS_YAWGPU_BACKEND=vulkan`) both show `pass=24 fail=1` in batch and both pass the case in isolation —
  so it is in yawgpu's **shared (HAL-agnostic)** layer, not a per-HAL path. (Same family as the resolved
  **F-029**/**F-030** cross-test contamination, here on Metal+Vulkan and **deterministic**, and specific to
  the two-dispatch compute pass.)
- **Localization (for yawgpu):** the case is correct in isolation, so prior in-process GPU work (or the
  `--run-case`/batch execution mode) leaves yawgpu in a state where a compute pass with **two** dispatches
  writing one storage buffer produces no visible result on the following readback. A cross-test
  state/resource leak or a missing flush on the multi-dispatch path — **for yawgpu to localize**.
- **Status:** **RESOLVED** (2026-06-05, real-GPU Metal **and** Vulkan/MoltenVK; yawgpu `89f25df` —
  "compute dispatch is a per-dispatch usage scope"). **Root cause: yawgpu treated the whole compute pass
  as a single usage scope instead of per-dispatch**, so two dispatches writing one storage buffer in one
  pass were mishandled (the result was lost); because that scope-tracking state depended on prior
  in-process work, it surfaced only under batch / `--isolate`. Re-test: `single_buffer` `pass=25 fail=0` on
  **both** HALs across `--workers` / no-workers / `--isolate` (was `pass=24 fail=1` in batch); regression
  sweep (compute + rendering + sampling) `pass=926 fail=0`. The cross-HAL reproduction (Metal ==
  Vulkan/MoltenVK) correctly localized it to yawgpu's shared layer; the "specific to the multi-dispatch
  path" observation pinpointed it. Surfaced, not masked — no `expectations/yawgpu.txt` entry was ever
  added.

---

_Add new findings as `F-00N` with the same fields._
