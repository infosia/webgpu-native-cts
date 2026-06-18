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

## Full cross-backend sweep — 2026-06-14

The entire ported suite (234 files) run per-file on each backend:

| Backend | pass | skip | fail | crash | xfail | verdict |
|---------|-----:|-----:|-----:|------:|------:|---------|
| **Dawn** (oracle) | 534184 | 63364 | **0** | 0 | — | fully green |
| **yawgpu — Metal** | 460730 | 136816 | **0** | 0 | 2 | green — the 2 `xfail` are the documented Dawn-leniency `draw,index_buffer_format_dirtying` (yawgpu *stricter*, not a defect), now carried in `expectations/yawgpu.txt` |
| **yawgpu — Vulkan (native, NVIDIA)** | — | — | **0** | 0 | 94 | green — every ported file `fail=0` in per-file isolation; F-005…F-112 all fixed & native-Vulkan-re-verified (2026-06-15/16); F-085 (92) + F-111 (2) = 94 `xfail` |
| **yawgpu — MoltenVK** (Vulkan HAL) | 445041 | 136816 | 15599 | 0 | 92 | Vulkan-path residuals (all Metal-green AND native-Vulkan-green) — see below; F-085 92 xfailed |
| **wgpu-native** (`--isolate`, per-case) | 25668 | 10200 | 5680 | 6808 | — | bring-up reference (known panic-heavy state) |

> **Update (2026-06-16):** the native-Vulkan (NVIDIA) sweeps that followed this MoltenVK run surfaced eight
> genuine Apple-masked findings — **F-105, F-106, the F-107…F-110 batch, and F-112** (all fixed &
> native-Vulkan-re-verified), plus **F-111** (external-texture feature gap, documented `xfail`). **F-112**
> is the cross-check payoff: suspected to be a naga workgroup-atomic SPIR-V defect, but wgpu-native passed
> the same case with byte-identical SPIR-V, so the real cause was yawgpu's storage-buffer bounds-check
> policy (`Restrict`), fixed by gating on `VK_EXT_robustness2` — *not* naga. **No yawgpu finding remains
> open on Metal or native Vulkan.** The 2 Dawn-leniency Metal cases are now `xfail` (not raw `fail`).

**yawgpu's Metal HAL passes the entire ported suite** (bar the 2 Dawn-leniency cases, now `xfail`) — every
finding F-005…F-103 is fixed and re-verified on Metal. The **Vulkan HAL (via MoltenVK)** still has
residuals, all of which are **green on Metal** (Vulkan-path-specific). Breakdown of the 15599:
- **`api,operation,command_buffer,copyTextureToTexture` — 14512** → **F-104**, a **MoltenVK translation
  artifact** (native-Vulkan-confirmed green 2026-06-14 — does NOT fail on Windows/Vulkan; yawgpu's Vulkan
  HAL is correct, MoltenVK mistranslates the T2T copy). Unlike F-103, this one is not a yawgpu defect.
- SPIR-V/naga-backend shader-execution residuals (Metal-green): `zero_init` 801, `robust_access_vertex` 200,
  `memory_layout` 42 (the **F-070** Vulkan-side residual), `padding` 2, `memory_model/barrier` 1,
  `shader_io/shared_structs` 1, `statement/{compound,discard}` 1+1, `limits` 1.
- known small artifacts: `maxComputeWorkgroupStorageSize` 30 (SPIR-V compile-at-limit, prior F-101 note),
  `rendering/{3d_texture_slices 1, depth_clip_clamp 2}`, `render_pipeline/misc` 2.

**All of these Vulkan-path residuals are MoltenVK translation artifacts, not yawgpu defects** — F-104 was
native-Vulkan-confirmed green (2026-06-14), and the SPIR-V/`memory_layout`/`zero_init`/compute-storage items
are the F-070-classified SPIRV-Cross residue. **yawgpu has no open implementation defect on either
real-hardware path** (native Metal + native Vulkan both pass the ported suite, bar the 2 Dawn-leniency
cases). The Metal-run `expectations/yawgpu.txt` stays clean; the MoltenVK artifacts (incl. F-104, F-085)
belong in `expectations/yawgpu-vulkan.txt` for Vulkan-backend runs.

## Re-test summary

Every defect this suite surfaced against **yawgpu** (the primary conformance subject) was fixed in yawgpu
and re-confirmed on real hardware. The Metal `expectations/yawgpu.txt` carries a single `xfail`: the
`draw,index_buffer_format_dirtying` Dawn-leniency (yawgpu is *stricter* — not a defect); Vulkan-only
expected failures (F-085 spec-in-flux, F-111 external-texture gap) live in `expectations/yawgpu-vulkan.txt`.
No yawgpu *defect* is masked — every entry is a documented non-defect. The early validation/copy milestones
(commit + result):

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

**Resolved yawgpu findings:** F-005/006/008/009/010/011/014/016/018/020/022/023/024/025/026/029/030/031/032/034/035/037/038/039/040/041/042/043/044/045/046/047/048/049/050/051/053/054/055/057/058/059/060/061/062/063/064/065/066/067/068/069/070/072/073/074/076/077/078/079/080/081/082/087/089/090/091/092/093/094/095/096/098/099/100/101/102/103
— each keeps a compact record below. The 2026-06-11 yawgpu update (`f9a076e`…`f857f3f`) fixed the eleven
findings F-064–F-069, F-072–F-074, F-076, F-077, re-verified on Metal + MoltenVK (F-068 additionally
confirmed green on native Windows/Vulkan; its 125-case MoltenVK-only residual is a translation
limitation, same class as F-033/F-045/F-053).

**2026-06-14 yawgpu fix batch (re-verified green on Metal + MoltenVK).** A yawgpu update (`…58d8aab`:
`cts(F-093a-d)`, `cts(F-094)`, `cts(F-101)`, `cts(F-098,F-099)`, + a naga rev) **resolved ten** of the
open findings, re-confirmed cross-HAL on this run: **F-089** (`createBindGroup` filtering-sampler — now
`pass=2358 fail=0`), **F-090** (`render_pipeline/fragment_state` — `10754/0`), **F-091** (naga MSL writer
panic on generated vertex shaders — `render_pipeline/vertex_state` `28151/0 crash=0`; the Metal crash is
gone), **F-092** (`render_pass/*` — `12095/0`), **F-093** (encoding validation: compressed-copy bounds,
unused vertex-buffer gap slots, defer pass-after-end, auto-layout exclusive-pipeline compat — all green;
`copyTextureToTexture 9254/0`, `encoder_open_state 119/0`, `draw 15708/2`† ), **F-094** (`image_copy/*` —
`65794/0`), **F-098** (`texture-component-swizzle` gating), **F-099** (`rgba16unorm/snorm` tier1 gating)
(`capability_checks/features 1178/0`), **F-101** (per-stage resource limits at auto-layout pipeline
creation), **F-102** (default/auto bind-group-layout compatibility, both directions —
`pipeline_bind_group_compat 2520/0`). †the 2 residual `draw,index_buffer_format_dirtying` cases are the
documented Dawn-leniency (yawgpu is *stricter*; not a defect — see the F-093 note below).

> **Harness note (not a finding):** under `--workers`, the encoding sweep now shows ~193
> `clearBuffer`/`copyBufferToBuffer` cases failing with `failed to request device: adapter is consumed` —
> each file passes green **in isolation**. yawgpu now consumes the adapter on `requestDevice` (spec-correct);
> the harness's device-mismatch / per-test private-device paths leave the shared adapter consumed for sibling
> tests in the same process. This is the known consumed-adapter harness-normalization follow-up, not a port
> or yawgpu defect.

**Open — yawgpu:** **none of the yawgpu-core findings remain.** The 2026-06-14 batch additionally resolved
**F-095** (`resource_usages/buffer/*` `c0e5ba7` — `1422/0`), **F-096** (`resource_usages/texture/*`
`5ed5ada` — `6556/0`), and **F-103** (yawgpu Vulkan-HAL 3D image-copy slice-stride `e7db246` —
`command_buffer/image_copy 138408/0`, native-Vulkan + MoltenVK confirmed). **F-100** (out-of-range `@binding` validation-timing) was **also fixed** by `16ee140`, and **F-070**
(memory_layout `struct_inner_align` / matCx3 padding) by `a558b71`+`e4a31d1` — **on Metal**
(`memory_layout 434/0`, was 9 fail). yawgpu's **Metal HAL now passes the entire ported suite** (bar the 2
Dawn-leniency `draw,index_buffer_format_dirtying` cases).

The MoltenVK sweep's residuals have since been **split by a native-Vulkan (NVIDIA) run** (the question this
paragraph used to leave open): the **MoltenVK-only** items — **F-104** (`copyTextureToTexture` data wrong,
14512), the SPIR-V/SPIRV-Cross shader-execution residuals (`zero_init`, `robust_access_vertex`,
`memory_layout` 42, `padding`, etc.), and the `maxComputeWorkgroupStorageSize` SPIR-V compile residual — are
all **native-Vulkan-green**, i.e. confirmed MoltenVK/SPIRV-Cross translation artifacts, not yawgpu defects.
The **genuine Vulkan-HAL defects** the native-Vulkan runs surfaced (Apple-masked) — **F-105, F-106,
F-107…F-110, F-112** — are all **fixed and native-Vulkan-re-verified** (2026-06-15/16); **F-111** is the
external-texture feature gap (`xfail`). The spec-in-flux **F-085** (`sample_mask`/`position`) stays `xfail`
in `expectations/yawgpu-vulkan.txt`. **F-087** (requestDevice limit & adapter-lifecycle, surfaced by Y-5)
was fixed the same area-sweep day (yawgpu `0be6c55`) and re-verified 2026-06-12 (`requestDevice` `pass=289
fail=0` on both HALs, matching Dawn). The same `0be6c55` (naga rev bump) also resolved **F-078**
(`robust_access` let-OOB over-validation — now 1068 genuine passes) and **F-082**
(`texture_intra_invocation_coherence` — 12 passes both HALs). The naga-lineage **F-070** residual
(struct_inner_align + matCx3 padding + `shadow:loop`) was resolved for yawgpu on 2026-06-14 (naga fork:
`ebec34ae4` shadow:loop, `197a3ddd` matCx3/struct padding, `ee37a074` struct_inner_align) — Metal-green and
native-Vulkan-confirmed; the remaining MoltenVK `memory_layout` residue is the SPIRV-Cross artifact above.
**F-085** was native-Vulkan confirmed (2026-06-11, Windows/NVIDIA RTX
5060 Ti: the same 92 cases as MoltenVK) but then **reclassified — NOT an implementation defect**:
wgpu-native on the same machine/driver fails the identical 92 cases (its earlier "fully green"
Y-4b record was wgpu-native-on-Metal), and Dawn behaves the same on Vulkan (no FragCoord/
SampleMaskIn normalization; suppresses these cases via crbug.com/407144390). The WGSL WG resolved
to respecify `sample_mask` input to the Vulkan single-bit semantics (gpuweb/gpuweb#5457; CTS
change gpuweb/cts#4510 pending merge); `position` under per-sample invocation is open in
gpuweb/gpuweb#4777. The 92 cases are `xfail` in the **Vulkan-only** expectation files
`expectations/yawgpu-vulkan.txt` / `expectations/wgpu-native-vulkan.txt` (applied to Vulkan-backend
runs only; the Metal-run files stay xfail-free so Metal sweeps show no xpass noise) until cts#4510
merges. The same native-Vulkan run (yawgpu
`9382206`) cleared the other two pending findings: **F-083** (memory_model/barrier) is green
natively (`pass=12 fail=0`, two consecutive runs) and **F-086** (compound eval order, discard
derivatives, IO-struct-in-buffer) passes all three cases natively — both reclassified MoltenVK-only
translation artifacts (same class as F-033/F-045/F-053/F-068-residual). Batch Y-4b (statement +
shader_io, 11 files incl. fragment_builtins 2399-line port): Dawn green 2929/0; yawgpu Metal and
wgpu-native fully green. The 2026-06-11 regressions F-079/F-080/F-081 were fixed the same day (yawgpu `4770131` +
`9382206`) and re-verified: `api,validation` full sweep on Metal `pass=107608 fail=0`; F-079/F-080 also
green on MoltenVK. `external_texture` on Vulkan now fails honestly with "not supported on the Vulkan
backend" — the deliberate `fa97027` limitation (previously a false pass), documented under F-081, not an
open defect. Batch Y-4a (flow_control + memory_model): `flow_control` is green on **all four** targets
(140 cases); memory_model surfaced **F-082** (naga-MSL: storage-texture intra-invocation coherence — also
fails on wgpu-native, queued with the naga batch), F-083 above, and wgpu-native **F-084**.

**Open — naga lineage / wgpu-native:** **F-078** (validator treats `let`-propagated indices as
const-expression OOB → all `robust_access` compute pipelines error; Tint correct; yawgpu's earlier
"green" was a false pass exposed by the F-065 uncaptured-error wiring — NOT a yawgpu regression),
**F-070** (`memory_layout`/`padding`/`shadow:loop` — **fixed on yawgpu** both HALs via the naga-fork revs,
but **wgpu-native (upstream naga) still fails it**: 2026-06-16 cross-check on Metal measured `memory_layout`
fail=55+crash=1, `padding` fail=16, `shadow` crash=1),
**F-071** (wgpu-native `zero_init` 3930 + `robust_access` aborts — same naga root as F-078), **F-075**
(wgpu-native buffer mapping broadly broken). The `api,operation` `texture_component_swizzle` test remains
Dawn-only oracle (yawgpu/wgpu-native lack the feature); the Y-6 V9 validation file additionally surfaces
**F-098** (yawgpu/wgpu-native do not reject a swizzle view when the feature is absent).

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

- **RESOLVED for yawgpu** (yawgpu `155a854`): `rendering/depth_clip_clamp:depth_test_input_clamped` — `frag_depth` not clamped to the viewport depth range `[minDepth,maxDepth]` before the depth test (out-of-range points drew). Green on Metal (`1 skip 1`) + native Vulkan; residual MoltenVK `0/1` is a confirmed MoltenVK-only artifact. **Still open on wgpu-native.**

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

- **RESOLVED for yawgpu** (yawgpu `9bc49dc`): `render_pass/clear_value:stencil_clear_value` — the stencil reference wasn't masked to the 8-bit aspect width before the `equal` compare (6 unmasked-out-of-range cases failed). `stencil_clear_value 30/0` both HALs. **wgpu-native still affected.**

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

- **RESOLVED** (yawgpu `c29dc78`-era): `rendering/3d_texture_slices:multiple_color_attachments,same_mip_level` — couldn't render to 4 color attachments each bound to a different 3D `depthSlice` (read back zero). Green on Metal + native Windows/Vulkan; residual MoltenVK `VK_ERROR_FEATURE_NOT_PRESENT` (2D-view-on-3D) is a confirmed MoltenVK-only artifact.

---

## F-054 — yawgpu: a render pass with a sparse / null color attachment renders nothing — cross-HAL

- **RESOLVED** (yawgpu `793fc6d`-era): `render_pipeline/pipeline_output_targets:color,attachments` — a render pass with a sparse/null color attachment rendered nothing (non-null slot read back zero). `color,attachments 2/0` both HALs (Metal + Vulkan/MoltenVK).

---

## F-055 — yawgpu: wrong values sampling a depth/stencil aspect while it is a read-only DS attachment — cross-HAL

- **RESOLVED** (yawgpu `79c4968`-era): `memory_sync/texture/readonly_depth_stencil:sampling_while_testing` — wrong values sampling a depth/stencil aspect while it's a read-only DS attachment (check wrote 0). `1/0` both HALs + native Windows/Vulkan.

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

- **RESOLVED** (yawgpu `8b42e5d`-era): `api,validation,non_filterable_texture` — WGSL frontend errored on `texture_cube_array<f32>` (8 float cube-array cases → error module). `non_filterable_texture 160/0` both HALs (Metal + MoltenVK).

---

## F-058 — yawgpu: render-pipeline depth-stencil state over-requires depthCompare + depthWriteEnabled — cross-HAL

- **RESOLVED** (yawgpu `8b42e5d`-era): `render_pipeline,depth_stencil_state` — over-required `depthCompare`/`depthWriteEnabled` for a depth format even when the depth aspect is unused (10 cases). `depth_stencil_state 1600/0` both HALs.

---

## F-059 — yawgpu: storage-texture-format support gap in render-pipeline validation + WGSL — cross-HAL

- **RESOLVED** (yawgpu `8b42e5d`-era): `render_pipeline,misc:storage_texture,format` — storage-texture-format support narrower than spec in pipeline-layout validation + WGSL (~366 cases). `storage_texture,format 720/0` both HALs.

---

## F-060 — yawgpu: WGSL compiler errors on `texture_external` (external-texture type) — cross-HAL

- **RESOLVED** (yawgpu `fa97027`, 2026-06-09): `render_pipeline,misc:external_texture` — WGSL frontend errored on `texture_external` (2 cases). `external_texture 2/0` both HALs (full external-texture support on Metal; honest operation-level rejection on Vulkan).

---

## F-061 — yawgpu: render-pipeline over-rejects compatible pipeline-layout binding kinds — cross-HAL

- **RESOLVED** (yawgpu, 2026-06-09): `render_pipeline,resource_compatibility` — over-rejected compatible pipeline-layout binding kinds (80 cases). `resource_compatibility 123/0` both HALs.

---

## F-062 — yawgpu: render-bundle over-rejects compatible attachment signatures — cross-HAL

- **RESOLVED** (yawgpu, 2026-06-09): `encoding,render_bundle` — over-rejected compatible render-bundle attachment signatures (30 cases). `render_bundle 21/0` both HALs.

---

## F-063 — yawgpu: inter-stage interpolation-sampling compatibility mis-validated — cross-HAL

- **RESOLVED** (yawgpu, 2026-06-09): `render_pipeline,inter_stage` — inter-stage interpolation-sampling compatibility both over- and under-validated (12 cases). `inter_stage 26/0` both HALs.

---

## F-064 — yawgpu: WGSL frontend errors immediate-data shader modules — cross-HAL

- **RESOLVED** (yawgpu `f9a076e`; re-verified 2026-06-11 Metal + MoltenVK): `pipeline,immediates:pipeline_creation_immediate_size_mismatch` — WGSL frontend errored on immediate-data shader modules (4 cases). `pipeline/immediates` green.

---

## F-065 — yawgpu: error-scope out-of-memory type / filter handling — cross-HAL

- **RESOLVED** (yawgpu `f9a076e` + `de7bae3`/`ef43eae`; re-verified 2026-06-11 Metal + MoltenVK): `api,validation,error_scope` — OOM reported as validation (`type=1`) not out-of-memory, and OOM/internal filtered scopes didn't catch (7 cases). `error_scope` green.

---

## F-066 — yawgpu: setViewport rejects an in-bounds viewport as out-of-bounds — cross-HAL

- **RESOLVED** (yawgpu `f9a076e`; re-verified 2026-06-11 Metal + MoltenVK): `dynamic_state:setViewport,xy_rect_contained_in_bounds` — rejected an in-bounds viewport as out-of-bounds (2 cases). `dynamic_state` green.

---

## F-067 — yawgpu: under-validates depth/stencil buffer copies & buffer device-mismatch — cross-HAL

- **RESOLVED** (yawgpu `f9a076e`; re-verified 2026-06-11 Metal + MoltenVK): `image_copy,buffer_related` — under-validated combined-DS aspect-`all` buffer copies, mismatched-device buffers, and [Metal] non-256 `bytesPerRow` for single-aspect DS (Metal 15 / MoltenVK 8). `image_copy/buffer_related` green.

---

## F-068 — yawgpu: vertex-buffer OOB robustness broken for indirect draws — cross-HAL

- **RESOLVED** (yawgpu `f857f3f` — Metal vertex pulling + Vulkan robustBufferAccess; re-verified 2026-06-11): `robust_access_vertex:vertex_buffer_access` — vertex-buffer OOB robustness broken for indirect draws (Metal 89 / MoltenVK 129 cases). Metal green (1856) + native Windows/Vulkan green; a 125-case MoltenVK-only residual is a confirmed MoltenVK artifact.

---

## F-069 — yawgpu: workgroup-memory loads read zeros (memory_layout) — Metal-dominant

- **RESOLVED** (yawgpu `a034b24`; re-verified 2026-06-11): `shader,execution,memory_layout` `read_layout`/`write_layout` — `var<workgroup>` round-trips read back zeros (55 yawgpu-only cases, Metal-dominant). The 55 pass on Metal; remaining `memory_layout` tracked under F-070.

---

## F-070 — shared-naga (yawgpu + wgpu-native): workgroup write_layout, struct_inner_align, matCx3 padding, loop shadowing

- **RESOLVED for yawgpu 2026-06-14** (naga fork: `ebec34ae4` shadow:loop, `197a3ddd` matCx3/struct padding-preserving MSL stores, `ee37a074` struct_inner_align IR alignment): shared-naga `memory_layout` / `padding` / `shadow:loop` defects (workgroup write_layout, struct_inner_align, matCx3 padding, loop shadowing) — fail identically on yawgpu + wgpu-native (Dawn green). Metal-green (`memory_layout 434/0`, `padding 18/0`, `shadow 7/0`); native-Vulkan-confirmed resolved 2026-06-14. Remaining MoltenVK-only `memory_layout`/`zero_init` residue = SPIRV-Cross translation artifact. wgpu-native (upstream naga) still shares the defects.

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

- **RESOLVED** (yawgpu `726e8aa`; re-verified 2026-06-11): `api,operation,buffers,map` — zero-size buffer / zero-length map ranges rejected on yawgpu Metal (~93 cases, Metal-only). `buffers,map` green on Metal + MoltenVK.

---

## F-073 — yawgpu: panic-abort on OOM-sized mappedAtCreation buffer — cross-HAL

- **RESOLVED** (yawgpu `726e8aa`; re-verified 2026-06-11): `buffers,map_oom:mappedAtCreation` — panic-abort (signal 6) on a ~9 PB `mappedAtCreation` buffer (cross-HAL). `map_oom` green, no abort.

---

## F-074 — yawgpu: queue.writeBuffer ordering vs prior submits broken — MoltenVK-only (native-Vulkan confirm pending)

- **RESOLVED** (yawgpu `a034b24`; re-verified 2026-06-11): `memory_sync,buffer,multiple_buffers` `rw`/`ww` — `queue.writeBuffer` not ordered behind previously submitted command buffers (21 cases, MoltenVK-only). `multiple_buffers 260` green Metal + MoltenVK.

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

- **RESOLVED** (yawgpu `726e8aa`; re-verified 2026-06-11): `api,operation,sampling,anisotropy` — anisotropic filtering broken (Metal: out-of-range `maxAnisotropy` not clamped consistently; MoltenVK: error command buffer; 3 cases). `anisotropy 3/3` green Metal + MoltenVK.

---

## F-077 — shared-naga: max-bindings shader invalid; yawgpu panics in the MSL writer instead of erroring

- **RESOLVED** (yawgpu `d376a1b` — naga storage-access fix + Metal per-kind/per-stage binding slots; re-verified 2026-06-11): `sampling,sampler_texture:sample_texture_combos` — yawgpu panicked in the naga MSL writer (`module is not valid`) instead of erroring gracefully on a max-bindings generated shader. `sampler_texture` green Metal + MoltenVK, no panic. (Same commit introduced F-078/F-081.)

---

## F-078 — naga lineage: validator treats `let`-propagated indices as const-expression OOB (robust_access) — NOT a yawgpu regression

- **RESOLVED** (yawgpu `0be6c55`, naga rev bump; re-verified 2026-06-12): `shader,execution,robust_access:linear_memory` — naga const-propagated a `let` index and raised a static-OOB validation error (over-validation; per WGSL a `let` is a runtime value), erroring every non-f16 compute pipeline (1068, naga-lineage). `linear_memory 1068/0` Metal + MoltenVK (genuine pass, not the earlier false pass).

---

## F-079 — yawgpu regression: destroyed-resource errors fire outside the expected validation point — cross-HAL

- **RESOLVED** (yawgpu `9382206` — submit-time destroyed validation; re-verified 2026-06-11 Metal + MoltenVK): `setBindGroup:state_and_binding_index` + `queue,destroyed,query_set` — destroyed-resource errors fired outside the expected validation point (7 cases, regression). green.

---

## F-080 — yawgpu regression: filtering-sampler + unfilterable-float texture no longer rejected — cross-HAL

- **RESOLVED** (yawgpu `9382206` — layout-aware filterable check; re-verified 2026-06-11 Metal + MoltenVK): `non_filterable_texture` — filtering sampler + unfilterable-float texture no longer rejected (32 cases, regression). `non_filterable_texture` green (160).

---

## F-081 — yawgpu regression: external-texture pipelines error "missing params buffer slot" — cross-HAL

- **RESOLVED** (yawgpu `4770131` — fragment-only external textures regained their params buffer slot; re-verified 2026-06-11): `render_pipeline,misc:external_texture` — external-texture pipelines errored "missing params buffer slot" (2 cases, regression). Metal `external_texture 2`. On Vulkan/MoltenVK the 2 now fail with the deliberate "not supported on the Vulkan backend" rejection (documented limitation, not a defect).

---

## F-082 — naga-MSL lineage: storage-texture intra-invocation coherence broken on Metal

- **RESOLVED** (yawgpu `0be6c55`, naga rev bump; re-verified 2026-06-12): `memory_model,texture_intra_invocation_coherence` — storage-texture write→same-texel read within one invocation returned stale/zero on the MSL path (12 cases, naga-MSL lineage). `12/0` Metal + MoltenVK.

---

## F-083 — yawgpu: workgroupBarrier does not order non-atomic storage-texture accesses — MoltenVK-only (native Vulkan green)

- **Backend:** yawgpu Vulkan via MoltenVK only (Metal passes; Dawn passes; **native Vulkan passes** —
  see below). On MoltenVK reproducible, not statistical noise: 17k–25k disallowed observations out of
  65 536 per run.
- **Found by:** `shader,execution,memory_model,barrier` `workgroup_barrier_load_store`
  `accessValueType="u32";memType="non_atomic_texture";accessPair="rw";normalBarrier=true`.
- **Observed:** `memory model test failed: testResults[1] == 25567, expected == 0 (disallowed weak
  behavior observed)` — a `workgroupBarrier()` between a non-atomic storage-texture write and a read does
  not establish ordering on the MoltenVK path.
- **Native-Vulkan run (2026-06-11, Windows 11 / NVIDIA RTX 5060 Ti, yawgpu `9382206`):**
  `memory_model,barrier:*` = `pass=12 skip=24 fail=0` (skips are shader-f16 / workgroupUniformLoad
  variants), green on **two consecutive runs** including the exact failing case above. The ordering
  hole does not exist on a native Vulkan driver.
- **Status:** **RECLASSIFIED — MoltenVK translation artifact** (same class as F-033/F-045/F-053/
  F-068-residual: the SPIR-V→Metal translation, not yawgpu's emitted barriers, loses the image-memory
  ordering). Not a yawgpu defect on native Vulkan; record kept for the MoltenVK environment.

---

## F-084 — wgpu-native: disallowed weak-memory behaviors on Metal (barrier/coherence/weak)

- **Backend:** wgpu-native only (yawgpu Metal passes these; Dawn passes).
- **Found by:** `shader,execution,memory_model`: `barrier:workgroup_barrier_load_store` (2),
  `coherence:corw1`/`corw2` (3), `weak:load_buffer` (1) — beyond the 12 F-082 cases it shares with yawgpu.
- **Observed:** disallowed weak behaviors observed in the stress harness — barrier/coherence guarantees
  violated on Metal.
- **Status:** **OPEN**; tracked as a **wgpu-native defect** (bring-up reference). Not masked.

---

## F-085 — Vulkan per-sample dispatch: sample_mask / position fragment builtins — NOT an implementation defect (spec in flux; xfail)

- **Backend:** **every Vulkan-based implementation** — yawgpu (MoltenVK AND native Windows/Vulkan,
  NVIDIA RTX 5060 Ti, yawgpu `9382206`), **wgpu-native on the same machine/driver (identical 92
  fails; verified 2026-06-11 — its earlier "fully green" Y-4b record was wgpu-native-on-Metal)**,
  and Dawn (no FragCoord/SampleMaskIn normalization on Vulkan: `RenderPipelineVk.cpp` enables
  sample shading only for framebuffer fetch; Chromium suppresses exactly these cases on
  Linux/Vulkan via crbug.com/407144390). Metal-family targets pass (Metal's builtins natively
  match the current oracle).
- **Found by:** `shader,execution,shader_io,fragment_builtins` — `inputs,sample_mask` (88) and
  `inputs,position` (4), exactly the `sampleCount=4` × `interpolation="…,sample"` (linear/perspective)
  cases; same 92-case set and signature on MoltenVK and native Vulkan.
- **Observed:** with `@interpolate(…, sample)` forcing per-sample dispatch, Vulkan's native
  semantics surface: the `sample_mask` input contains only the current sample's bit (1/2/4/8;
  spec-conformant per the Vulkan `SampleMask` builtin definition) where the current CTS oracle
  expects the full coverage mask (15), and `position.xy` is at the sample location where the
  oracle expects the pixel center. The port's oracle matches upstream `fragment_builtins.spec.ts`
  exactly — this is not a porting bug.
- **Root cause:** WebGPU↔Vulkan semantic gap, currently being resolved **in Vulkan's favor**: the
  WGSL WG resolved to respecify `sample_mask` input to single-bit-under-per-sample-invocation
  (gpuweb/gpuweb#5457, WGSL minutes 2025-12-09 / 2026-01-06; CTS change gpuweb/cts#4510 pending
  merge). `position` center-vs-sample-location remains an open spec question (gpuweb/gpuweb#4777).
- **Status:** **RECLASSIFIED — not an implementation defect.** The 92 cases are `xfail` in the
  **Vulkan-only** expectation files `expectations/yawgpu-vulkan.txt` and
  `expectations/wgpu-native-vulkan.txt` (verified `fail=0 xfail=92`, exit 0 on Windows/Vulkan).
  They are deliberately NOT in the Metal-run files (`expectations/yawgpu.txt`,
  `expectations/wgpu-native.txt`) — Metal passes these cases, so a single shared file would emit
  `xpass=92` noise on every Metal sweep (verified). Apply the `-vulkan` file for Vulkan-backend runs
  only. When gpuweb/cts#4510 merges: re-port the new sample_mask oracle and drop those xfail
  entries; keep the 4 `inputs,position` entries until gpuweb#4777 resolves.

---

## F-086 — yawgpu/naga-SPIR-V: three single-case Vulkan divergences (compound eval order, discard derivatives, IO-struct-in-buffer) — MoltenVK-only (native Vulkan green)

- **Backend:** yawgpu Vulkan via MoltenVK only (yawgpu Metal, wgpu-native, Dawn pass; **native Vulkan
  passes all three** — see below).
- **Found / observed (MoltenVK):**
  (a) `statement,compound` `eval_order`: `arr[idx()] += foo()` — the `expect_not_reached()` branch runs
  (`arr[0] != 42` after the compound assignment), i.e. the WGSL-specified evaluation order of a compound
  assignment's reference/RHS is violated on the SPIR-V path;
  (b) `statement,discard` `derivatives:useStorageBuffers=true`: 2176 derivative elements outside
  tolerance — helper-invocation/derivative semantics after discard;
  (c) `shader_io,shared_structs` `shared_with_buffer`: `queue submit cannot use an error command buffer`
  — a struct shared between entry-point IO and a storage buffer errors pipeline creation.
- **Native-Vulkan run (2026-06-11, Windows 11 / NVIDIA RTX 5060 Ti, yawgpu `9382206`):** all three
  cases **pass** — `compound:eval_order`, `discard:derivatives` (both `useStorageBuffers` variants),
  and `shared_structs:shared_with_buffer`; the containing files are otherwise green too
  (`statement,compound` + `statement,discard` + `shader_io,shared_structs` = fail=0).
- **Status:** **RECLASSIFIED — MoltenVK translation artifacts** (per-item: the SPIR-V naga emits is
  consumed correctly by a native driver; MoltenVK's SPIR-V→Metal translation diverges). Not yawgpu
  defects on native Vulkan; record kept for the MoltenVK environment.

---

## F-087 — yawgpu: requestDevice limit & adapter-lifecycle conformance gaps — cross-HAL

- **RESOLVED** (yawgpu `0be6c55`; re-verified 2026-06-12): `api,operation,adapter,requestDevice` — defaults not honored, adapter not single-use, better-than-supported not rejected, advertised-vs-delivered limit mismatch (73 cases, cross-HAL). `requestDevice 289/0` Metal + MoltenVK, matching Dawn.

---

## F-088 — wgpu-native: lifecycle/reflection groups panic-abort & under-validate (Y-5 bring-up reference)

- **Backend:** wgpu-native only (Dawn passes; yawgpu passes everything except F-087's requestDevice set).
- **Found by:** the batch Y-5 groups: 56 **process aborts** (contained as `crash` via `--isolate`) across
  `object_has_descriptor_label` (18), `pipeline_layout_with_null_bind_group_layout` (16),
  `getCompilationInfo_returns`/`offset_and_length`/`line_number_and_position` (16),
  `max_storage_buffer_texture_frag_outputs` (3), `iff_uncaptured` (2), `texture_creation_from_reflection`
  (1); plus ~126 fails (requestDevice limit-reporting gaps similar to F-087 but broader, and
  `texture_reflection_attributes` 9, `lost_on_destroy` 1).
- **Observed:** the same eager-panic class as F-001…F-021 (wgpu-native aborts on paths Dawn/yawgpu handle
  gracefully) plus requestDevice/limit conformance gaps.
- **Status:** **OPEN**; tracked as a **wgpu-native defect** (bring-up reference; to be reflected in
  `expectations/wgpu-native.txt` on regen). Not masked.

---

## F-089 — yawgpu: filtering sampler not rejected for a non-filtering sampler binding — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `createBindGroup:binding_must_contain_resource_defined_in_layout` — a filtering sampler bound to a `non-filtering` BGL entry wasn't rejected (1 case, cross-HAL). `createBindGroup 2358/0`.

---

## F-090 — yawgpu: render-pipeline fragment-state validation gaps — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `render_pipeline,fragment_state` — color-target/blend/bytes-per-sample validation gaps (146 cases: maxColorAttachmentBytesPerSample under-validation, no-target/blend/blendable over-validation; cross-HAL). `fragment_state 10754/0`.

---

## F-091 — naga-MSL lineage: MSL writer panics on generated vertex shaders during render-pipeline creation

- **RESOLVED on yawgpu 2026-06-14** (naga-fork rev bump): `render_pipeline,vertex_state` — naga MSL writer panicked (signal 6) on generated vertex shaders during render-pipeline creation (518 crashes on Metal, naga-MSL lineage; MoltenVK was already green). `vertex_state 28151/0 crash=0` on Metal. wgpu-native (upstream naga) may still crash.

---

## F-092 — yawgpu: render-pass descriptor & attachment-compatibility validation gaps — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `render_pass,{render_pass_descriptor,attachment_compatibility}` — depth/stencil loadOp-vs-readOnly under-validation [864], pipeline-vs-pass depth read-only/format compat, bytes-per-sample, snorm-16 resolve (1082 cases, cross-HAL). `render_pass/* 12095/0`.

---

## F-093 — yawgpu: encoding-validation gaps (compressed copy / encoder-state / pipeline-layout / vertex-OOB) — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu `cts(F-093a-d)`, re-verified green Metal + MoltenVK): `encoding,{copyTextureToTexture,render/draw,encoder_open_state,pipeline_bind_group_compat}` — compressed-copy over-validation [dominant], vertex-buffer OOB, encoder-open-state error timing, auto-vs-explicit pipeline-layout compat (cross-HAL). `copyTextureToTexture 9254/0`, `encoder_open_state 119/0`, `draw 15708/2`† (the 2 are the documented Dawn-leniency `index_buffer_format_dirtying`, not a defect), `pipeline_bind_group_compat 2520/0`.

---

## F-094 — yawgpu: image-copy buffer/layout validation gaps — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu `cts(F-094)`): `api,validation,image_copy/*` buffer/layout validation gaps (required-bytes under-validation, offset-alignment over-validation, offset+bytesPerRow, d/s aspect; 3513 cases, cross-HAL). Re-verified green Metal+MoltenVK (`image_copy/* 65794/0`).

---

## F-095 — yawgpu: buffer usage-scope conflicts not detected in a render pass — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu `cts(F-095)` `c0e5ba7`, re-verified green Metal + MoltenVK): `resource_usages,buffer,{in_pass_encoder,in_pass_misc}` — same buffer used as read-only + writable-storage in one render-pass scope not rejected (296 cases, cross-HAL; Dawn + wgpu-native pass). `resource_usages/buffer/* 1422/0`.

---

## F-096 — yawgpu: texture subresource usage-scope conflicts not detected — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu `cts(F-096)` `5ed5ada`, re-verified green Metal + MoltenVK): `resource_usages,texture,*` — texture subresource usage-scope hazards not tracked (851 cases, cross-HAL; the texture analog of F-095). `resource_usages/texture/* 6556/0`.

---

## F-097 — wgpu-native: destroyed-device operations diverge from spec (every case) — bring-up reference

- **Backend:** wgpu-native only (Dawn, yawgpu Metal, yawgpu MoltenVK all pass 2568/14; wgpu-native fails
  **all 2568**). Surfaced by Y-6 V8.
- **Found by:** `api,validation,state,device_lost,destroy` (every native test: create*/command/queue on a
  destroyed device).
- **Observed:** the spec (and Dawn/yawgpu) treats most operations on a destroyed device as succeeding
  without a validation error (invalid objects / no-ops); wgpu-native produces a different result for
  every case (the destroyed-device state model differs).
- **Status:** **OPEN**; tracked as a **wgpu-native defect** (bring-up reference; to be reflected in
  `expectations/wgpu-native.*` on regen). Not masked.

---

## F-098 — yawgpu: `texture-component-swizzle` feature gating not enforced — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `capability_checks,features,texture_component_swizzle:only_identity_swizzle` — a non-identity swizzle view wasn't rejected on a device without the feature (18 cases, cross-HAL; wgpu-native shares the gap).

## F-099 — yawgpu: `rgba16unorm`/`rgba16snorm` not gated behind `texture-formats-tier1` — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `capability_checks,features,{texture_formats,texture_formats_tier1}` — `rgba16unorm`/`rgba16snorm` treated as core, not gated behind `texture-formats-tier1` (28 cases, cross-HAL). wgpu-native is worse here (crashes on tier1 16-bit-norm formats; bring-up reference).

---

## F-100 — yawgpu (naga frontend): out-of-range `@binding` rejected at `createShaderModule`, not pipeline creation — cross-HAL

- **RESOLVED on yawgpu 2026-06-14** (yawgpu `cts(F-100)` `16ee140`): `capability_checks,limits,maxBindingsPerBindGroup:createPipeline,at_over` — naga frontend rejected an out-of-range `@binding` at `createShaderModule` instead of pipeline creation (validation-timing divergence, 12 cases, cross-HAL). `maxBindingsPerBindGroup 43/0`. wgpu-native (upstream naga) may still crash.

---

## F-101 — yawgpu: per-stage resource binding limits not enforced at auto-layout pipeline creation — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `capability_checks,limits,*PerShaderStage/*Stage:createPipeline,at_over` — per-stage resource binding limits not enforced at **auto-layout** pipeline creation (312 cases, cross-HAL; explicit-layout paths were fine). Separate MoltenVK-only residual: `maxComputeWorkgroupStorageSize` at-limit SPIR-V compile (30, artifact). wgpu-native crashes/fails heavily (bring-up reference).

---

## F-102 — yawgpu: default/auto bind-group-layout compatibility validation diverges (both directions) — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu `cts(F-093d)` auto-layout BGL exclusive-pipeline compat, re-verified green Metal + MoltenVK): `pipeline_bind_group_compat:default_bind_group_layouts_never_match,{compute,render}_pass` — default/auto BGL compatibility mis-keyed in both directions (18 cases, cross-HAL; Dawn + wgpu-native pass). `pipeline_bind_group_compat 2520/0`.

---

## F-103 — yawgpu Vulkan-HAL: 3D image-copy loses/corrupts non-zero depth slices (+ stencil8 stencil-only) — Vulkan-specific, native-confirmed

- **RESOLVED 2026-06-14** (yawgpu `cts(F-103)` `e7db246` — "fix Vulkan HAL 3D/multi-slice copy slice stride"): `api,operation,command_buffer,image_copy:{rowsPerImage_and_bytesPerRow,offsets_and_sizes,origins_and_extents}` — yawgpu Vulkan-HAL read back wrong data at non-zero 3D z-slices (7450 cases across 43 formats) + stencil8 stencil-only (96); Metal always green. Native-Vulkan-confirmed; re-verified `image_copy 138408/0` (was `fail=7546`).

---

## F-104 — MoltenVK translation artifact: `copyTextureToTexture` wrong data — Metal AND native Vulkan green

- **Backend:** **MoltenVK only** (`api,operation,command_buffer,copyTextureToTexture` `pass=16614
  fail=14512`); yawgpu **Metal green**, **native Windows/Vulkan green** (user-confirmed 2026-06-14 — the
  cases do **not** fail on native Vulkan), Dawn green. Surfaced by the 2026-06-14 full sweep.
- **Found by:** `api,operation,command_buffer,copyTextureToTexture:*` — the texture↔texture copy
  data-correctness tests; pixel mismatches across color formats, including basic **2D** copies
  (e.g. `srcFormat=r8unorm;dstFormat=r8unorm;dimension="2d"` — `pixel mismatch at 0,0,0`), not only 3D.
- **Observed:** a `copyTextureToTexture` on the Vulkan path reads back wrong pixel data; Metal copies the
  same cases correctly. Sibling of [F-103](#f-103) (the buffer↔texture `image_copy` Vulkan-HAL slice-stride
  bug, fixed `e7db246`); the T2T path was not covered by that fix. The breadth (2D color, ~14.5k cases)
  points to a copy-region / layout defect in yawgpu's Vulkan-HAL T2T path rather than a per-format issue.
- **Status:** **MoltenVK translation artifact — NOT a yawgpu defect** (native-Vulkan-confirmed
  2026-06-14: green on Windows/Vulkan). Same class as F-033/F-045/F-053/F-070-residue: yawgpu's Vulkan HAL
  is correct (native Vulkan + Metal both pass); MoltenVK mistranslates the T2T copy. No yawgpu/naga action;
  if applied to a Vulkan-backend run it belongs in `expectations/yawgpu-vulkan.txt`. Surfaced, not masked.

---

## F-105 — yawgpu: robust-access write to a `bool` workgroup array not clamped — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `cts(F-105)` `87cc2c6`, naga fork `7dd824389`): `shader,execution,robust_access:linear_memory` — OOB write to a `bool` workgroup array not clamped (`expected 0, got 1`; 3 cases, bool-only). Native-Vulkan-only (NVIDIA exposed it, Apple masked it); the naga SPIR-V backend emitted the wrong `bool` array stride. Verified native Vulkan; `robust_access 1068/0` on Metal + MoltenVK (no regression).

---

## F-106 — yawgpu Vulkan HAL: missing write→read barrier for indirect-args / index / copy-source reads — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `cts(F-106)` `858de27`): `api,operation,memory_sync,buffer,multiple_buffers:wr` — Vulkan HAL omitted the write→read barrier when the read uses the buffer as indirect-args / index / copy-source (18 cases; `expected 1, got 0`). Fix adds the missing dst access/stage (INDIRECT_COMMAND_READ / INDEX_READ / TRANSFER_READ). Latent on Apple (coherent memory masked it), exposed on NVIDIA native Vulkan. Verified native Vulkan; `multiple_buffers 263/0` Metal + MoltenVK (no regression).

---

## F-107 — yawgpu Vulkan HAL: `storeOp: "discard"` not honored (content stored instead of discarded) — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `3a10aa7`): `api,operation,render_pass,storeOp:*` + `storeop2:*` — the Vulkan HAL mapped `"discard"` to `VK_ATTACHMENT_STORE_OP_DONT_CARE`, which kept the drawn value on the immediate-mode NVIDIA path (`expected 0 got 255`; 18 cases, all `storeOperation="discard"`). Fix explicitly clears every discarded attachment subresource to zero after `vkCmdEndRenderPass`. Native-Vulkan-only (Apple tilers drop tile content, masking it); re-verified native Vulkan `storeOp 26/0`, `storeop2 2/0`. Metal/Noop unchanged.

---

## F-108 — yawgpu Vulkan HAL: srgb→non-srgb `viewFormat` reinterpretation applies wrong gamma on render+resolve — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `7b3a05c`): `api,operation,texture_view,format_reinterpretation:render_and_resolve_attachment:*` — rendering+resolving through a non-srgb `viewFormat` of an srgb texture stored the wrong gamma (`expected 179 +/- 2, got 218`; 4 cases). Fix threads the reinterpreted view format core→HAL and uses it for the Vulkan color/resolve attachment descriptions, image views, and clear values. Native-Vulkan-only (Apple masks it); re-verified native Vulkan `format_reinterpretation 6/0`. Metal/GLES/Noop unaffected.

---

## F-109 — yawgpu Vulkan HAL: depth clip/clamp wrong — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `1a0f9b4`): `api,operation,rendering,depth_clip_clamp:*` — WebGPU always clamps fragment depth to the viewport before the test, but the HAL mapped `depthClampEnable=unclippedDepth`, so the default `unclippedDepth=false` path only clamped to `[0,1]` (`expected 0, got 255`; 2 cases). Fix enables `VK_EXT_depth_clip_enable`+`depthClamp`, sets `depthClampEnable=TRUE` always, and controls clipping independently via `depthClipEnable=!unclippedDepth`. Native-Vulkan-only (Apple masks it); re-verified native Vulkan `depth_clip_clamp 3/1skip/0` (`unclippedDepth=true` intentionally skips). Metal/Noop unaffected.

---

## F-110 — yawgpu Vulkan HAL: `triangle-strip` primitive restart not applied — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `41751d0`): `api,operation,render_pipeline,primitive_topology:basic:topology="triangle-strip";primitiveRestart=true` — the Vulkan input-assembly state hardcoded `primitiveRestartEnable=false`, so the strip was not cut at the sentinel (`expected 0, got 255`; 2 cases, indirect=false/true). Fix enables primitive restart iff the topology is a strip. Native-Vulkan-only (Apple masks it; Metal strips restart implicitly); re-verified native Vulkan `primitive_topology 20/0`. Metal/Noop unaffected.

---

## F-111 — yawgpu Vulkan: external textures unsupported (uncaptured error where validation expected) — native Vulkan

- **Backend:** yawgpu native Vulkan (NVIDIA). Feature-completeness gap rather than a correctness bug.
- **Found by:** `api,validation,render_pipeline,misc:external_texture:isAsync=false`/`true` — 2 cases.
  Isolated `pass=742 fail=2`.
- **Observed:** `uncaptured error: external textures are not supported on the Vulkan backend` — yawgpu's
  Vulkan backend has no `GPUExternalTexture` support, so the validation test errors instead of exercising
  the validation path. Needs either external-texture support on the Vulkan HAL or a capability-gated skip.
- **Status:** **RESOLVED** (documented feature gap, 2026-06-15) — capability-gated skip, no yawgpu change.
  naga's SPIR-V backend does not lower `naga::ImageClass::External` (the 3-plane + params expansion), so a
  render pipeline whose shader uses `texture_external` is rejected at SPIR-V codegen with a
  `GPUInternalError`; the validation case expects creation to succeed (Dawn supports external textures on
  Vulkan). This is a feature-completeness gap, not a correctness defect (Metal has full external-texture
  support), consistent with the prior "documented limitation, not a defect" treatment. The two cases are
  listed as expected failures in `expectations/yawgpu-vulkan.txt`; re-verified native Vulkan (NVIDIA):
  `external_texture` `xfail=2 fail=0` under that expectations file. Drop the entries if/when
  external-texture SPIR-V lowering lands.

---

## F-112 — yawgpu Vulkan: workgroup-class atomics violate read-read coherence (`corr`) — native Vulkan

- **RESOLVED** (yawgpu `b602ff2`, 2026-06-16): `shader,execution,memory_model,coherence:corr` (`atomic_workgroup;intra_workgroup` non-RMW, 1 subcase) — workgroup-atomic read-read coherence violated (WebGPU-disallowed `r0==1 && r1==0`) on native Vulkan (NVIDIA RTX 5060 Ti); wgpu-native passed the same case on the same GPU. Not a naga defect: yawgpu and wgpu-native emit byte-identical workgroup-atomic SPIR-V (GLSL450, `scope=Workgroup`, `semantics=0`, no `Coherent`), verified by reassembling wgpu-native's `VK_APIDUMP_SHOW_SHADER` capture. Cause was yawgpu's SPIR-V `buffer` bounds-check policy = `Restrict`, whose software clamp (`OpArrayLength`+`OpISub`+`UMin`) on storage-buffer accesses breaks the NVIDIA driver's coherence; SPIR-V/Vulkan version and zero-init mode ruled out. Fixed by gating `buffer` on `VK_EXT_robustness2`/`robustBufferAccess2` (→ `Unchecked` when present; `index`/`image_load` stay `Restrict`; Metal/MSL unchanged); design in yawgpu `specs/blocks/60-real-backends.md` § "CTS finding F-112". Re-verified native Vulkan: `coherence:*` 27/27, `weak`/`atomicity`/`barrier`/`adjacent`/`texture_intra_invocation_coherence` no failures, validation-layer clean. Never added to `expectations/yawgpu-vulkan.txt`.

---

## F-113 — wgpu-native: workgroup `atomic` array not zero-initialized (`atomicExchange` advanced) — bring-up reference

- **Backend:** wgpu-native (Metal, Apple). Cross-backend divergence; **not a yawgpu defect**.
- **Found by:** `shader,execution,expression,call,builtin,atomics,atomicExchange:exchange_workgroup_advanced:*`
  — 62 of 64 subcases fail. Isolated (`--workers 1`) reproduces: `pass=2 fail=62`.
- **Observed:** the advanced test allocates an extra validation element in the `var<workgroup>`
  atomic array that the shader never writes, and asserts it stays `0` (WebGPU guarantees workgroup
  memory is zero-initialized). wgpu-native reads garbage there — `sorted values mismatch: actual
  0,<nonzero>, expected 0,0`, with a different nonzero per subcase (uninitialized memory). The only
  2 "passes" are luck (garbage happened to be 0), so the set is unstable run-to-run.
- **Cross-check:** **Dawn 1445/1445 and yawgpu (Metal) 1445/1445 pass** the full atomics query; yawgpu
  passes this exact case 64/64 in isolation. Both honor the workgroup-memory zero-init guarantee, so
  the test is correct and the defect is wgpu-native-specific (its compute pipeline does not zero-init
  workgroup `atomic` storage). Per [[naga-fix-crosscheck-wgpu-native]] this is wgpu-native, not naga.
- **Status:** OPEN (wgpu-native bring-up reference, 2026-06-17). Not added to `expectations/wgpu-native.txt`:
  the case is partial-failing on garbage (would create xpass noise / be unstable), and the canonical
  wgpu-native expectations are regenerated on Windows — fold it in there on the next regen. The other
  10 atomic built-ins and all storage/non-advanced workgroup cases pass on wgpu-native.

---

## F-114 — yawgpu: `textureSampleGrad` on 3D / cube textures errors (vec3 gradients) — cross-HAL (Metal)

- **Backend:** yawgpu (Metal, Apple). Deterministic (same fail count at `--workers 1` and `8`).
- **Found by:** `shader,execution,expression,call,builtin,textureSampleGrad:sampled_3d_coords:*`
  (49815 fail) and `sampled_array_3d_coords:*` (7830 fail) — 57645 total, i.e. every
  3D (`dim="3d"`) and cube (`dim="cube"`) subcase across all stages
  (compute/fragment/vertex). The 2D / 2d-array Grad cases pass; `sampled_3d` Grad has
  **0 passes** on yawgpu.
- **Observed:** `uncaptured error: queue submit cannot use an error command buffer` — the
  compute/render command buffer becomes an error object before submit, so nothing executes.
- **Cross-check:** **Dawn passes textureSampleGrad fully (161865/0)**, so the port and WGSL
  are correct. yawgpu's `textureSampleLevel`/`textureSample` on the SAME 3D/cube textures
  pass (3D texture creation/upload/sampling is fine), so this is specific to the
  `textureSampleGrad` builtin with vec3 gradients (3D/cube) — most likely naga's MSL lowering
  of `textureSampleGrad` for a 3D/cube image, or a yawgpu-core validation of the grad call.
  wgpu-native cross-check (shared upstream naga → would classify naga vs yawgpu-core) was
  attempted but the wgpu-native run did not complete on this host (hung/too slow on the ~50k
  -subcase query); **cross-check pending** per [[naga-fix-crosscheck-wgpu-native]].
- **Status:** **RESOLVED 2026-06-18** (naga fork `infosia/wgpu` `430e6e3c8`, pinned-rev bump in
  yawgpu `2d2594f`). Root cause was naga's MSL backend: `put_image_sample_level` emitted
  `metal::gradient2d` for `textureSampleGrad` regardless of texture dimension, so 3D/cube
  produced `gradient2d(float3, float3)` → Metal compile error → error command buffer → the
  generic "queue submit cannot use an error command buffer". Not a yawgpu-core/HAL bug (the
  generated MSL string was the proof). Fix makes the gradient builtin dimension-aware
  (`gradient2d`/`gradient3d`/`gradientcube`) + naga snapshot test. Re-verified on yawgpu/Metal:
  `textureSampleGrad:*` **135945 pass / 0 fail / 0 crash** (was 78300/57645). Confirms the
  naga-MSL attribution; no cts-side change.

---

## F-115 — yawgpu: `textureLoad` on combined depth-stencil formats errors (Metal)

- **Backend:** yawgpu (Metal, Apple). Deterministic.
- **Found by:** `shader,execution,expression,call,builtin,textureLoad:*` — 384 fail, ALL
  `format="depth24plus-stencil8"` (192) or `format="depth32float-stencil8"` (192), spread across
  the arrayed / depth / multisampled / sampled_2d cases. Plain depth formats (depth16unorm,
  depth24plus, depth32float) pass on yawgpu.
- **Observed:** `uncaptured error: queue submit cannot use an error command buffer` — the command
  buffer becomes an error object before submit.
- **Cross-check:** **Dawn passes textureLoad fully (18048/0)**, and yawgpu passes textureLoad on the
  depth-ONLY formats — so it is specific to the depth aspect of a COMBINED depth+stencil format under
  textureLoad (the depth-only view / load of depth24plus-stencil8 / depth32float-stencil8). Likely
  naga's MSL lowering of textureLoad on a depth aspect of a combined format, or yawgpu-core's
  aspect-view validation. wgpu-native cross-check pending per [[naga-fix-crosscheck-wgpu-native]].
- **Status:** **RESOLVED 2026-06-18** (yawgpu `baa0c81`, a yawgpu-core/HAL fix — naga rev unchanged,
  confirming it was not a naga issue). Re-verified on yawgpu/Metal: `textureLoad:*` now 0 fail (was
  384); the combined depth-stencil depth/stencil-aspect load path is fixed. The cts port was correct
  (Dawn-green) throughout; no cts-side change.

---

## F-116 — yawgpu: `arrayLength` off-by-one when binding size isn't a whole multiple of element stride (Metal)

- **Backend:** yawgpu (Metal, Apple). Deterministic.
- **Found by:** `shader,execution,expression,call,builtin,arrayLength:multiple_elements:buffer_size=1004;type="vec3<f32>"|"vec3<i32>"|"vec3<u32>";stride=16` — 3 cases. `GPU buffer mismatch: expected 62, got 63`.
- **Observed:** for a runtime-sized `array<vec3<T>>` with element stride 16 bound over a 1004-byte
  region, `arrayLength` returns **63**; the WGSL spec value is `floor((bindingSize − arrayOffset) /
  elementStride) = floor(1004/16) = floor(62.75) = 62`. yawgpu over-counts by one when the binding
  size is not a whole multiple of the element stride (here the element *size* 12 < *stride* 16, so the
  trailing 12 bytes don't form a full strided element) — likely yawgpu rounds up / counts the last
  element by size rather than stride.
- **Cross-check:** **Dawn returns 62 (= the spec value, test passes)**; only yawgpu diverges. Likely
  yawgpu-core's runtime-array-length derivation (the binding-size → element-count math), not naga.
  wgpu-native cross-check pending per [[naga-fix-crosscheck-wgpu-native]].
- **Status:** **RESOLVED 2026-06-18** (yawgpu `94694e2`). Re-verified on yawgpu/Metal:
  `arrayLength:*` now 205 pass / 0 fail (was 3 fail); the non-stride-multiple binding now floors to
  the spec element count. yawgpu-core fix; the cts port was correct (Dawn-green) throughout.

---

## F-117 — yawgpu/naga: `firstLeadingBit(u32)` of `0xFFFFFFFF` returns `0xFFFFFFFF` instead of 31 (Metal)

- **Backend:** yawgpu (Metal). Deterministic.
- **Found by:** `shader,execution,expression,call,builtin,firstLeadingBit:u32` — the all-ones input
  `0xFFFFFFFF` returns `0xFFFFFFFF`; spec value is **31** (the most-significant set bit). Every other
  u32 value is correct (e.g. 0x1FFFFFFF→28, 0x7FFFFFFF→30), and the i32 variant is fully correct.
- **Cross-check:** Dawn returns 31 (test passes). **wgpu-native (upstream naga) ALSO FAILS** the u32
  all-ones case — 12 fails on the runtime path (`firstLeadingBit:u32:inputSource="uniform"/"storage_r"/
  "storage_rw"`, all vectorize): `0xFFFFFFFF` returns `0xFFFFFFFF`, expected 31 (verified 2026-06-19,
  `build/cts`). Confirms a real **upstream-naga** lowering defect for `firstLeadingBit(u32)` all-ones;
  the yawgpu fix legitimately preempts a bug upstream naga still carries (per [[naga-fix-crosscheck-wgpu-native]]).
- **Status:** **RESOLVED 2026-06-18** (yawgpu `ee77bf3`). Re-verified yawgpu/Metal: `firstLeadingBit:*`
  0 fail. cts port was correct (Dawn-green) throughout. wgpu-native still fails (upstream naga unfixed).

## F-118 — yawgpu/naga: `insertBits` const-eval returns 0 (Metal)

- **Backend:** yawgpu (Metal). Deterministic.
- **Found by:** `shader,execution,expression,call,builtin,insertBits:integer:inputSource="const";*` —
  all 8 const-eval cases (signed/unsigned × width 1–4) return **0** instead of the inserted result
  (e.g. input `2303862050` → returns 0, expected 2303862050). The `uniform`/`storage_r`/`storage_rw`
  input sources (runtime evaluation) all pass — **only `inputSource=const` fails**.
- **Cross-check:** Dawn passes all input sources. **wgpu-native (upstream naga) ALSO FAILS** all 8
  `insertBits:integer:inputSource="const";*` cases (return 0; verified 2026-06-19, `build/cts`), while
  its `uniform`/`storage_r`/`storage_rw` runtime cases pass — identical signature to yawgpu. Confirms a
  real **upstream-naga const-eval** defect for `insertBits`; the yawgpu fix preempts a bug upstream naga
  still carries (per [[naga-fix-crosscheck-wgpu-native]]).
- **Status:** **RESOLVED 2026-06-18** (yawgpu `ee77bf3`). Re-verified yawgpu/Metal: `insertBits:*` 0
  fail (const-eval now correct). cts port was correct (Dawn-green) throughout. wgpu-native still fails
  (upstream naga unfixed).

---

## F-119 — yawgpu: `pack2x16float` / `unpack2x16float` error (Metal)

- **Backend:** yawgpu (Metal). Deterministic.
- **Found by:** `shader,execution,expression,call,builtin,pack2x16float:*` (4) and `unpack2x16float:*`
  (4) — all input sources (const/uniform/storage_r/storage_rw) fail with `uncaptured error: queue
  submit cannot use an error command buffer`. The 8 snorm/unorm pack/unpack builtins
  (`{pack,unpack}{4x8,2x16}{snorm,unorm}`) all pass on yawgpu.
- **Cross-check:** Dawn passes all 11 (port correct). **wgpu-native (upstream naga) PASSES** both
  `pack2x16float:*` and `unpack2x16float:*` (all 8, verified 2026-06-19, `build/cts`) — so this is **NOT
  a naga bug**; it was **yawgpu-specific**. `pack2x16float`/`unpack2x16float` are core WGSL builtins (no
  `shader-f16` feature required — the f16 is internal); naga's MSL lowering emits `half`-typed code for
  the internal f16 (un)packing, which Dawn/wgpu-native's Metal handle but yawgpu's pipeline rejected
  until it enabled the internal-f16 path. Cross-check (wgpu-native green) confirms the yawgpu-side fix
  was the right layer (per [[naga-fix-crosscheck-wgpu-native]]).
- **Status:** **RESOLVED 2026-06-18** (yawgpu `bc1d44b`, enable `SHADER_FLOAT16_IN_FLOAT32` for
  pack/unpack2x16float — confirms the internal-f16 diagnosis). Re-verified yawgpu/Metal:
  `pack2x16float:*` + `unpack2x16float:*` 0 fail (now run, no skip). cts port was correct (Dawn-green).
  wgpu-native already passed (not a naga bug — yawgpu-local feature-gating).

---

_Add new findings as `F-00N` with the same fields._
