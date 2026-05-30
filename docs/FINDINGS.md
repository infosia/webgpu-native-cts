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
- **Not an ABI artifact:** the `WGPUTextureFormat` enum mapping is **byte-identical** between the
  wgpu-native and yawgpu `webgpu-headers/webgpu.h` (verified by diff), so the same `format=N` value
  denotes the same format on both — this is a genuine format-handling gap in yawgpu, not an
  enum/ABI mismatch in how the suite passes the value.
- **Status:** open; tracked as a **yawgpu defect** (3-way confirmed). Not masked; recorded in
  `expectations/yawgpu.txt` (T1: 32 fails + 2 crashes; T2 added 16 more `dimension_type…` fails for the
  4 tier1 formats, and the same rejections re-appear inside the `sampleCount…` triage), so a
  `--isolate --expectations` run over `createTexture:*` exits 0 on yawgpu; wgpu-native and Dawn need no
  entries.

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
- **Status:** open; tracked as a **yawgpu defect** (3-way confirmed). Not masked; the 12 cases (6
  formats × 2 dimensions) are in `expectations/yawgpu.txt`, so a `--isolate --expectations` run over
  `createTexture:*` exits 0 on yawgpu; wgpu-native and Dawn need no entries.

---

_Add new findings as `F-00N` with the same fields._
