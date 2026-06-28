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

## Native-Vulkan Windows / NVIDIA full sweep — 2026-06-21 (Jun-19 build, pre-fix snapshot)

The first whole-suite **native-Vulkan** sweep at the grown **462-file** listing on **Windows 11 / NVIDIA
RTX 5060 Ti** (`cts.exe --isolate`, per-case process pool). **Important — pre-fix build:** it ran on the
**Jun-19 `yawgpu.dll`**, which **predates** the 2026-06-20/21 naga-fork fixes (**F-120** uniformity +
shader/validation, **F-121** f16, **F-122/F-123/F-125**, **F-124** scalar/vector). So the large
shader/validation + f16 fail blocks below are the **expected pre-fix residuals of already-resolved
findings**, not new defects. A post-rebuild re-sweep is deferred (a full native-Vulkan sweep is multi-hour
and thermally constrained — see F-126).

**Counts are per-CASE** (`--isolate` granularity = one subprocess per case), so the pass/skip totals are
**not comparable** to the Metal/MoltenVK whole-suite per-**subcase** rows. Fails are reported per-subcase.
**crash=0, no hard freeze** — but only because the sweep was run in thermal-safe chunks with cooldowns; a
raw long sweep freezes the whole machine (F-126: a CPU-thermal host limit on this box, separate from the
copy OOB).

| metric (per-case) | pass | skip | fail (subcase) | crash | xfail |
|---|---:|---:|---:|---:|---:|
| yawgpu — Vulkan (native, NVIDIA RTX 5060 Ti), Jun-19 build | 89794 | 59058 | **594** | 0 | 11 |

**Fail decomposition (594) — already-resolved findings vs. new candidates:**

| area | fail | attribution |
|---|---:|---|
| `shader/validation` (`uniformity` 409 + `shader_io,interpolate` 13, `decl,var` 10, `workgroup_size` 7, `pipeline_stage` 6, `functions,alias_analysis` 6, `types,pointer` 5, `functions,restrictions` 5, `types,atomics` 4, `shader_io,locations` 4, `decl,override` 3, `shader_io,size` 2, `id`/`align` 1+1) | **476** | **F-120** — `expected a validation error, got none` + `unexpected validation error for valid shader` (incl. 120× `Entry point main at Fragment is invalid`). RESOLVED post-Jun-19; expected on this build |
| f16 (`bitcast` 12, `access,vector,components` 2) | **14** | **F-121** — f16 path; RESOLVED post-Jun-19 |
| `shader_io,fragment_builtins` 8, `render_pipeline,misc` 2 | **10** | **F-085** (per-sample, spec-in-flux) + **F-111** (external-texture) — carried `xfail` |
| `shader,execution,robust_access` | **24** | → **F-127** (CONFIRMED post-rebuild 2026-06-21) — uniform-buffer OOB reads not zeroed (`expected 0, got 3`); F-112 interaction |
| `textureStore` (`rgb10a2unorm`, 3d / 2d-array) | **20** | → **F-128** (CONFIRMED post-rebuild 2026-06-21) — `expected 0, got 240` (rgb10a2unorm pack) |
| `textureLoad` (`*-srgb`) | **24** | **candidate** (minor) — green-channel sRGB-decode rounding (~1 ULP), e.g. `expected 1064469166, got 1064435712`. **Still un-re-swept** |
| `fwidth` / `fwidthFine` / `fwidthCoarse` (`f32`) | **24** | → **F-129** (CONFIRMED post-rebuild 2026-06-21) — derivative builtins: `discard`+derivative + denormal interval |
| `api,validation` (`encoding,cmds,render,draw` 1, `compute_pass` 1) | **2** | `compute_pass:indirect_dispatch_buffer,usage` = `HAL queue submission failed: vulkan`. **Still un-re-swept** |

**Takeaway:** on the pre-fix build, **~500 of the 594 fails are already-resolved (F-120/F-121) or
known-`xfail` (F-085/F-111)**; the genuinely-new native-Vulkan candidates are **~92** execution/api cases
(`robust_access`, `textureStore` `rgb10a2unorm`, `textureLoad` sRGB, `fwidth*`, one compute
indirect-dispatch). These need a post-rebuild re-sweep (latest naga) before being filed as findings —
recorded here so the data is not lost. Sweep artifacts: `sweep-out/` (git-ignored), runner
`chunked-sweep.sh` / `resume-sweep.sh`.

## F-138 — yawgpu Vulkan: `textureStore` to `bgra8unorm` writes wrong/zero bytes — native Vulkan

- **Backend:** yawgpu native Vulkan (NVIDIA RTX 5060 Ti, Windows). Deterministic. **Dawn passes.**
- **Found by:** full yawgpu/Vulkan sweep 2026-06-28. `textureStore:texel_formats` — **20 fail, all
  `format="bgra8unorm"`** + `textureStore:bgra8unorm_swizzle` **1 fail** = 21. `mismatch at byte 0:
  expected 51, got 0, format bgra8unorm`. Every other store format passes (incl. `rgb10a2unorm`,
  which used to fail as F-128 but was an oracle bug — now fixed).
- **Cross-check (attribution):** Dawn runs the same cases `fail=0` → the CTS oracle is correct, so this
  is a **real yawgpu Vulkan defect**. (Contrast F-128 `rgb10a2unorm`, where Dawn fails identically =
  oracle bug.)
- **Regression boundary = naga→Tint frontend migration.** The 2026-06-21 sweep (yawgpu `97b4827`)
  had `bgra8unorm` textureStore **passing**; between `97b4827` and the 2026-06-28 build (`05bf865`,
  84 commits) yawgpu **removed naga and made Tint the sole shader frontend** (`64fe785`/`7fda995`/
  `b0dad39`). The SPIR-V (Vulkan) storage-texture path is now Tint-generated, and `bgra8unorm` storage
  writes regressed there. (Same migration drives F-127 — see that finding.)
- **Root cause (direction; the HAL VkFormat map is NOT the bug):** `yawgpu-hal/src/vulkan/format.rs:35`
  correctly maps `Bgra8Unorm → VK_FORMAT_B8G8R8A8_UNORM`. The defect is in the **Tint SPIR-V
  storage-texture path** (`yawgpu-core/src/shader_tint.rs` `texel_format`): SPIR-V has **no `Bgra8`
  storage image format**, so `bgra8unorm` storage must be emitted as an `Rgba8` image with the B↔R
  channel order handled on the write — `expected 51, got 0` at byte 0 is the classic channel-order /
  zeroed-store symptom. This is the sibling of `4264df3` (which fixed `rgb10a2`/`rg11b10` storage
  format names for Tint Dawn-parity but did **not** cover `bgra8unorm`).
- **Note:** unrelated to yawgpu's *own* internal "F-138" (commit `0e5d92d`, texture lazy zero-init) —
  finding-number collision between the two repos; this `docs/FINDINGS.md` F-138 is the bgra8unorm one.
- **Status:** OPEN (2026-06-28; root-caused to the Tint SPIR-V storage path). yawgpu work item — xfail'd
  in `expectations/yawgpu-vulkan.txt`.

---

## F-137 — zero-dimension compute dispatch hard-wedges ANV-Haswell (whole-machine freeze; NOT yawgpu)

**Backend/host:** Linux, Intel Iris 5100 / **Haswell GT3**, Mesa ANV (`MESA-INTEL: warning: Haswell
Vulkan support is incomplete`), VT-d on. **Found by:** `api,validation,encoding,cmds,compute_pass`
during the 2026-06-25 Linux/Vulkan full sweep — the box froze *immediately* on that file (file 129),
twice, with **no DMAR fault and no kernel log** (a clean GPU wedge, unlike F-126's IOMMU DMA-write).

**Root cause (confirmed in pure Vulkan, yawgpu EXONERATED):** a `vkCmdDispatch` whose workgroup
count has a **zero in any dimension** — e.g. `(1,0,0)` — hard-wedges the Haswell GPU. Haswell has no
working GPU reset, so the hang is unrecoverable → whole-machine freeze (manual reboot). A normal
`(1,1,1)` dispatch is fine.

- **CTS path:** `compute_pass:dispatch_sizes` expands subcases over `smallDimValue ∈ {0,1}`, so its
  first executed dispatch is always zero-dim (`(1,0,0)`, `(0,1,0)`, …). Per-case bisection
  (`cp-bisect.sh`) froze on the first `dispatch_sizes` dispatch every time, on *different* `lv_*`
  params — i.e. it is the zero dimension, not a specific size, and `shader,execution,zero_init`
  (a normal `(1,1,1)` dispatch) passes immediately before it.
- **Standalone proof (`cp_repro.c`, hand-written no-op SPIR-V, no yawgpu/naga):** `cp_repro 1 1 1`
  completes cleanly; **`cp_repro 1 0 0` freezes the box at `vkQueueWaitIdle`** (last sync'd line on
  disk). Pure-Vulkan reproduction ⇒ the defect is **Mesa ANV / Haswell**, not yawgpu. Per the Vulkan
  spec a zero-dim dispatch is valid and a no-op; ANV-Haswell mishandles it.
- **Cross-host control (Windows / NVIDIA RTX 5060 Ti, yawgpu Vulkan, 2026-06-26):** the same
  `compute_pass:dispatch_sizes:*` (incl. the zero-dim `lv_mult=0;lv_add=0` subcases that wedge Haswell)
  runs `--isolate` `pass=12 fail=0 crash=0` with **no freeze** — the zero-dim dispatch is handled as a
  correct no-op. Confirms the wedge is **Mesa ANV/Haswell-specific**, not yawgpu and not zero-dim
  dispatch in general (a GPU with working reset/TDR is unaffected).

**Status:** driver/HW defect — not fixable in yawgpu or the CTS (the test is legitimate). Mitigations:
(a) quarantine `compute_pass` on this host (`run-linux-vulkan/full-0625/quarantine.txt`) so the sweep
survives; (b) an *optional* yawgpu/wgpu-level workaround would be to skip submission when any dispatch
dimension is 0 (semantically a no-op), sidestepping the ANV-Haswell wedge. Repro artifacts (git-ignored):
`run-linux-vulkan/{cp_repro.c,cp_repro.comp,cp_repro_build.sh,cp-bisect.sh}`.

**Linux freeze landscape (so a future sweep stays survivable — both are host/driver, not yawgpu):**
- **F-137 compute_pass zero-dim dispatch** — *immediate*, deterministic, no DMAR. Quarantined.
- **F-126 copy OOB DMA write** — *load-dependent*: `copyTextureToTexture`+`image_copy` run clean cold
  even at workers 1–8 (verified 2026-06-25: `image_copy` ×5, 692k subcases, only 3 survivable DMAR
  faults), but a long *warm* session accumulates i915/IOMMU state until `image_copy` storms (~8 DMAR
  faults in ~40 s) and freezes. Quarantined for warm sweeps; cold results are clean
  (`copyTextureToTexture pass=31126 fail=0`, `image_copy pass=138408 fail=0`).

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

**RESOLVED** (yawgpu `2667b0a`+`92db062`) — 12 valid color formats rejected as `Undefined` + D24S8 abort; wgpu-native/Dawn always passed.

---

## F-006 — yawgpu disagrees on which texture formats are multisampleable

**RESOLVED** (yawgpu `2667b0a`) — wrong multisampleable-format set (`sampleCount=4`); wgpu-native/Dawn always passed.

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

**RESOLVED** (yawgpu `2667b0a`) — 6 invalid `TransientAttachment` combos accepted; wgpu-native/Dawn always passed.

---

## F-009 — yawgpu over-restricts render-attachment dimension and under-validates storage usage

**RESOLVED** (yawgpu `2667b0a`+`92db062`) — 3D render-attachment over-rejected + tier1 storage gaps; Dawn always passed.

---

## F-010 — yawgpu's newly-enabled compressed / feature-gated formats have validation gaps

**RESOLVED** (yawgpu `92db062`) — compressed-format block-alignment / size limits unvalidated; wgpu-native/Dawn always passed.

---

## F-011 — yawgpu createView view-dimension gaps (2D-multilayer, cube, cube-array square)

**RESOLVED** (yawgpu `41e007b`) — 2D-multilayer/cube/cube-array-square view-dimension gaps; Dawn/wgpu-native always passed.

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

**RESOLVED** (yawgpu `baa78cb`) — out-of-range 3D-texture view array-layer ranges accepted; Dawn always passed.

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

**RESOLVED** (yawgpu `4292f76`) — read-write storage rejected on r32 formats; Dawn always passed.

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

**RESOLVED** (yawgpu `925520a`) — 1D storage-texture view dim + rgba8snorm base storage over-rejected; Dawn always passed.

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

**RESOLVED** (yawgpu `f75fc0a`) — null (unused) BGL slots not implemented; Dawn always passed.

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

**RESOLVED** (yawgpu `798fc6a`) — `minBindingSize=0` rejected at pipeline creation instead of deferred to bind time; Dawn always passed.

---

## F-023 — yawgpu aborts on a 0-size clearBuffer / copyBufferToBuffer (un-ended Metal blit encoder)

**RESOLVED** (yawgpu `e56f30a`, Metal) — 0-size clear/copy aborted via un-ended Metal blit encoder; Dawn always handled it.

---

## F-024 — yawgpu loses data in an rgba8uint texture-copy roundtrip (copyBufferToTexture → copyTextureToBuffer)

**RESOLVED** (yawgpu `c893eac`, Metal) — HAL lacked `rgba8uint`, copy silently a no-op; Dawn/wgpu-native always passed.

---

## F-025 — yawgpu `queueWriteTexture` writes zeros to color textures

**RESOLVED** (yawgpu `1e6c70b`, Metal) — `queueWriteTexture` upload path wrote zeros; Dawn/wgpu-native always passed.

---

## F-026 — yawgpu mishandles non-default buffer layout (and mip levels) in `copyBufferToTexture` / `copyTextureToBuffer`

**RESOLVED** (yawgpu `1e6c70b`, Metal) — non-default buffer layout / non-base mip mishandled; Dawn/wgpu-native always passed.

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

**RESOLVED** (yawgpu `1e67300`, Vulkan) — in-flight copy resources freed early, exhausting the VkDevice for later tests; retire ring now retains them until the fence signals.

---

## F-030 — yawgpu `MAP_READ` readback reads the buffer before the GPU copy completes (intermittent zeros)

**RESOLVED** (yawgpu `1297b7e`, Vulkan) — `MAP_READ` raced ahead of the GPU copy (intermittent zeros); read-map now idles the device queue first.

---

## F-031 — yawgpu diverges on the depth aspect of `copyTextureToTexture` (copied depth fails an equality re-render)

**RESOLVED** (yawgpu `f3afc31`+`cac328a`, Metal + Vulkan) — render path had no depth support; Dawn/wgpu-native always passed.

---

## F-032 — yawgpu returns zeros for depth/stencil aspect buffer⇄texture copies (except plain Stencil8)

**RESOLVED** (yawgpu `c8f15d5`+`af9ac5c`+`3c847ac`, Metal + Vulkan) — depth/stencil-aspect copies returned zeros; Dawn/wgpu-native always passed.

---

## F-033 — color `copyTextureToTexture` pixel mismatches on Mac via MoltenVK (confirmed MoltenVK artifact, not a yawgpu defect)

**MoltenVK translation artifact** (not a yawgpu defect; native-Vulkan-green + Metal-green) — color `copyTextureToTexture` byte-exact pixel mismatches under MoltenVK only.

---

## F-034 — yawgpu: a fragment storage write is lost on **indexed / indirect** draws

**RESOLVED** (yawgpu `36a6b66`, cross-HAL) — indexed/indirect draw paths not executed; Dawn/wgpu-native always passed.

---

## F-035 — yawgpu ignores `GPUColorTargetState` `blend` and `writeMask` (writes the raw fragment output) — cross-HAL

**RESOLVED** (yawgpu `74f5ef2`, cross-HAL) — `blend`/`writeMask` ignored, raw fragment output written; Dawn always passed.

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

**RESOLVED** (yawgpu `186cd54`, Metal-only) — missing `[[point_size]]` for point-list pipelines; Dawn/wgpu-native/yawgpu-Vulkan always clean.

---

## F-038 — yawgpu mishandles stencil operations, compare, and masks — cross-HAL

**RESOLVED** (yawgpu `40f5d7f`, cross-HAL) — dynamic stencil reference not threaded to the HAL; Dawn/wgpu-native always passed.

---

## F-039 — yawgpu: two dispatches in one compute pass lose their writes under batch execution — cross-HAL

**RESOLVED** (yawgpu `89f25df`, cross-HAL) — whole compute pass treated as one usage scope, not per-dispatch; Dawn/wgpu-native always passed.

---

## F-040 — yawgpu: multisample resolve does not write the resolve target — cross-HAL

**RESOLVED** (yawgpu `bc8c280`+`3303058`, cross-HAL) — no MSAA-resolve support, `resolveTarget` never written; Dawn/wgpu-native always passed.

---

## F-041 — yawgpu: read-only storage-texture `textureLoad` reads back zero — cross-HAL

**RESOLVED** (yawgpu `2e4edb7`, cross-HAL) — storage-texture bindings not wired to the shader; Dawn/wgpu-native always passed.

---

## F-042 — yawgpu: a render-stage (fragment) storage-buffer write from a point draw reads back zero — cross-HAL

**RESOLVED** (yawgpu `042902b`+`eadc2f6`, cross-HAL) — render usage scope rejected write+write + render-bundle draws not executed; Dawn/wgpu-native always passed.

---

## F-043 — yawgpu: render-pass `depthSlice` is ignored — always renders to slice 0 of a 3D texture — cross-HAL

**RESOLVED** (yawgpu `c6935f7`, cross-HAL) — `depthSlice` not threaded into the 3D render-target view; Dawn/wgpu-native always passed.

---

## F-044 — yawgpu: non-`float32` vertex formats decode to zero in the shader — cross-HAL

**RESOLVED** (yawgpu `706087f`, cross-HAL) — vertex-format conversion not applied beyond 32-bit-float passthrough; Dawn/wgpu-native always passed.

---

## F-045 — yawgpu and wgpu-native: `frag_depth` is not clamped to the viewport depth range before the depth test

- **RESOLVED for yawgpu** (yawgpu `155a854`): `rendering/depth_clip_clamp:depth_test_input_clamped` — `frag_depth` not clamped to the viewport depth range `[minDepth,maxDepth]` before the depth test (out-of-range points drew). Green on Metal (`1 skip 1`) + native Vulkan; residual MoltenVK `0/1` is a confirmed MoltenVK-only artifact. **Still open on wgpu-native.**

---

## F-046 — yawgpu: face culling / `front_facing` winding is mishandled — cross-HAL

**RESOLVED** (yawgpu `f82c2d6`+`d6e700a`, cross-HAL) — `@builtin(front_facing)` winding wrong, breaking color + `cullMode`; Dawn/wgpu-native always passed.

## F-047 — yawgpu: pipeline-overridable constants are ignored (read as zero) — cross-HAL

**RESOLVED** (yawgpu `fff8634`, cross-HAL) — WGSL `override` constants ignored in render + compute pipelines; Dawn/wgpu-native always passed.

---

## F-048 — yawgpu and wgpu-native: the stencil reference value is not masked to the stencil aspect's bit width

- **RESOLVED for yawgpu** (yawgpu `9bc49dc`): `render_pass/clear_value:stencil_clear_value` — the stencil reference wasn't masked to the 8-bit aspect width before the `equal` compare (6 unmasked-out-of-range cases failed). `stencil_clear_value 30/0` both HALs. **wgpu-native still affected.**

---

## F-049 — yawgpu: render-bundle execution mishandles the viewport rect, bundle draw-args, and repeated/blended replay — cross-HAL

**RESOLVED** (yawgpu `f82c2d6`, cross-HAL) — viewport rect ignored + bundle draw-args/blend mis-applied; Dawn/wgpu-native always passed.

---

## F-050 — yawgpu: occlusion query returns zero even when samples pass — cross-HAL

**RESOLVED** (yawgpu `37d36e6`+`e70d18d`, cross-HAL) — occlusion query never counted passing samples; Dawn/wgpu-native always passed.

---

## F-051 — yawgpu Metal HAL: crash creating a default view of a multisampled texture — Metal-HAL-only

**RESOLVED** (yawgpu `c29dc78`-era, Metal-HAL-only) — hardcoded `MTLTextureType2D` instead of propagating multisample-ness; Dawn/Vulkan always passed.

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

**RESOLVED** (yawgpu `a034b24`) — `var<workgroup>` round-trips read back zeros (55 cases, Metal-dominant); remaining `memory_layout` tracked under F-070.

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
- **Root cause of (a) — confirmed (2026-06-24, wgpu-native `9176708`, wgpu-core 29.0.1, Metal):** the
  `zero_init` failures are **all** `addressSpace="workgroup"` (re-run: `pass=925 fail=4164`; every fail is
  `workgroup`, `function`/`private` all pass; Dawn oracle `5089/0`). Workgroup-memory zero-init in wgpu is a
  naga **polyfill** (zeroing prologue + barrier) that wgpu-core injects **only when the pipeline stage
  descriptor's `zero_initialize_workgroup_memory` flag is `true`** (`wgpu-core/src/device/resource.rs:3905`,
  `pipeline.rs:185`). Browsers (Firefox/Gecko) call wgpu-core's Rust API directly and set this `true` per the
  WebGPU spec requirement → polyfill runs → **the same tests pass on Firefox CTS**. But **wgpu-native's C FFI
  hardcodes `zero_initialize_workgroup_memory: false`** at all three pipeline-creation sites
  (`wgpu-native/src/lib.rs:2073` compute, `:2242` render-vertex, `:2340` render-fragment), each marked
  `// TODO(wgpu.h)` — the field is not in the standard `webgpu.h` `ProgrammableStageDescriptor`, so the FFI
  layer disables it. Hence the polyfill never runs and workgroup vars hold garbage. **Not a naga/wgpu-core
  defect, not a test-port defect — a wgpu-native FFI conformance gap** (same wgpu-core/naga/Metal HAL as the
  passing Dawn/Firefox paths; the only difference is this one bool). A wgpu-native fix would set the flag
  `true` (or expose it via its `WGPUNativeShaderModuleDescriptor`-style extension) until upstream `webgpu.h`
  gains the field.
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

**MoltenVK translation artifact** (not a yawgpu defect; native-Vulkan-green + Metal-green) — `workgroupBarrier` storage-texture ordering lost only under MoltenVK.

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

**Not a defect** — spec in flux (gpuweb#5457 / cts#4510 pending, position open in gpuweb#4777); every Vulkan impl (yawgpu, wgpu-native, Dawn) diverges from the current oracle; 92 cases xfail in `expectations/{yawgpu,wgpu-native}-vulkan.txt`.

---

## F-086 — yawgpu/naga-SPIR-V: three single-case Vulkan divergences (compound eval order, discard derivatives, IO-struct-in-buffer) — MoltenVK-only (native Vulkan green)

**MoltenVK translation artifact** (not a yawgpu defect; native-Vulkan-green + Metal-green) — three single-case SPIR-V divergences (compound eval order, discard derivatives, IO-struct-in-buffer) only under MoltenVK.

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

**MoltenVK translation artifact** (not a yawgpu defect; native-Vulkan-green + Metal-green) — `copyTextureToTexture` color (2D + 3D, ~14.5k cases) reads wrong data only under MoltenVK.

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

**RESOLVED** (feature gap, 2026-06-15) — naga SPIR-V doesn't lower `texture_external` on Vulkan; capability-gated skip, 2 cases xfail in `expectations/yawgpu-vulkan.txt`; Metal has full support.

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

**RESOLVED** (yawgpu `2d2594f`, naga fork `430e6e3c8`) — naga MSL emitted `gradient2d` regardless of texture dimension; Dawn-green throughout.

---

## F-115 — yawgpu: `textureLoad` on combined depth-stencil formats errors (Metal)

**RESOLVED** (yawgpu `baa0c81`, yawgpu-core/HAL) — combined depth-stencil aspect load path errored; Dawn-green throughout.

---

## F-116 — yawgpu: `arrayLength` off-by-one when binding size isn't a whole multiple of element stride (Metal)

**RESOLVED** (yawgpu `94694e2`, yawgpu-core) — `arrayLength` over-counted by one when binding size wasn't a stride multiple; Dawn-green throughout.

---

## F-117 — yawgpu/naga: `firstLeadingBit(u32)` of `0xFFFFFFFF` returns `0xFFFFFFFF` instead of 31 (Metal)

**RESOLVED** (yawgpu `ee77bf3`) — `firstLeadingBit(u32)` all-ones; Dawn-green throughout; wgpu-native still fails (upstream naga).

## F-118 — yawgpu/naga: `insertBits` const-eval returns 0 (Metal)

**RESOLVED** (yawgpu `ee77bf3`) — `insertBits` const-eval returned 0; Dawn-green throughout; wgpu-native still fails (upstream naga).

---

## F-119 — yawgpu: `pack2x16float` / `unpack2x16float` error (Metal)

**RESOLVED** (yawgpu `bc1d44b`) — internal-f16 path disabled for `pack/unpack2x16float`; Dawn + wgpu-native always passed (yawgpu-specific feature-gating).

---

## F-120 — shader/validation under/over-validation — **RESOLVED** (yawgpu naga-fork: structural validation + full graph uniformity analysis)

**RESOLVED** (yawgpu naga-fork: structural fixes `8157263`/`f502b19`/`9bc23d6`/`b9d4393`/`c80b32f` + graph uniformity `0320944`/`66bee46`) — shared-naga under/over-validation; entire `shader/validation` area now yawgpu-clean on Metal (22781→0); wgpu-native still fails (upstream naga has no uniformity analysis).

---

## F-121 — yawgpu: newly-landed `shader-f16` errors the pipeline for f16 in access/bitcast (Metal)

**RESOLVED** (yawgpu `c937a32`+`a900cf8`) — newly-landed f16 const-eval / MSL half lowering errored pipelines (access/bitcast); Dawn-green throughout (wgpu-native skips, no shader-f16).

---

## F-122 — yawgpu: `<<` (shift left) abstract-int const-eval errors the pipeline (Metal)

**RESOLVED** (yawgpu `653ca12`, yawgpu-only) — abstract-int `<<` const-eval errored the pipeline; Dawn + wgpu-native always passed.

---

## F-123 — yawgpu: `sub_neg` operator-precedence const-eval errors the pipeline (Metal)

**RESOLVED** (yawgpu `653ca12`, yawgpu-only) — `sub_neg` precedence const-eval errored the pipeline; Dawn + wgpu-native always passed.

---

## F-124 — upstream-naga: abstract-float const-eval readback snippet fails/panics (Metal; blocks all `abstract_float` math)

- **Backend:** yawgpu (Metal) → `uncaptured error: queue submit cannot use an error command buffer`;
  wgpu-native (Metal) → **panic/crash (signal 6)**. Same 24 cases. Deterministic.
- **Found by:** `shader,execution,expression,call,builtin,{abs,floor,ceil,trunc,sqrt,cos}:abstract_float:inputSource="const";*`
  (phaseY13 Stage B/1) — every `abstract_float` const case of every math builtin fails. The **f32 paths all
  pass** on yawgpu. The failure is in the upstream **abstract-float readback snippet** (`abstractFloatSnippet`:
  splits the f64 abstract-float result into low/high u32 via const-eval `frexp`/`ldexp`/`select`/`floor`),
  NOT in the builtin itself (even trivial `floor`/`abs` fail) — i.e. naga cannot const-evaluate that snippet.
- **Cross-check:** **Dawn passes all 24** (oracle — Tint const-evals the snippet). **Both naga backends fail
  the SAME 24** (yawgpu errors the pipeline; wgpu-native panics) → **upstream-naga** (shared abstract-float
  const-eval gap), **not yawgpu-only**. Faithful port (Dawn-green; the f64 interval acceptance is correct).
- **Impact:** blocks the `abstract_float` variant of **every** math builtin in the Stage-B fan-out (~52
  builtins) and the abstract-float operators. The f32 (and later f16) variants are unaffected.
- **Status:** **PARTIALLY RESOLVED 2026-06-21** (yawgpu `97b4827`, naga bump 01e4e710 — const-eval
  frexp/ldexp on abstract-float). Re-verified yawgpu/Metal: the **scalar + vector** abstract-float readback
  now works — `abstract_float:const` across access + bitcast + scalar/vector math + scalar/vector & matrix
  operators **3014 pass / 0 fail**. **STILL OPEN (~88 cases, all `error command buffer`, Dawn-green),
  in two distinct groups:**
  - **(a) composite-result abstract-float const-eval** — `transpose:abstract_float` (9) +
    `determinant:abstract_float` (3) (matrix-result), `smoothstep:abstract_float` (4), `modf:abstract_*`
    (8). The matrix-result + struct-result readback snippets need the same abstract-float const-eval
    treatment the fix gave the scalar/vector path. (This is the genuine F-124-family remainder.)
  - **(b) f16 struct-returning `frexp`/`modf`** (~56: `frexp` f16 + `modf` f16) — a **separate
    upstream-naga issue, NOT abstract-float const-eval and NOT a regression** (yawgpu investigation
    2026-06-21, confirmed via `naga-cli` against the baseline naga `4065fd824` — these were 40-fail there
    too; F-124's bump actually *improved* `frexp` by +8 abstract cases, `modf` unchanged). Root cause:
    naga's front-end `FrexpResult.exp` is `Sint` width 2 (i16) which the validator rejects ("Sint scalar
    width 2 is not supported") — per WGSL it should be `i32`; plus a back/MSL f16 `frexp`/`modf` lowering
    gap on the runtime path. Independent naga work (i32-exp `FrexpResult` + f16 frexp/modf lowering).
  Re-verify both groups on the next naga bump. cts ports correct (Dawn-green) throughout. (wgpu-native
  also panics 7 `f32_addition:*_compound:const` — wgpu-native-only, bring-up reference.)

---

## F-125 — yawgpu: `atanh` f32 const-eval returns out-of-interval values (Metal)

**RESOLVED** (yawgpu `653ca12`, yawgpu-only) — `atanh` f32 const-eval out-of-interval near ±1; Dawn + wgpu-native always passed.

---

## F-126 — yawgpu: texture-copy GPU out-of-bounds DMA write (whole-machine freeze) — native Vulkan (Intel/VT-d), cross-OS

**RESOLVED for yawgpu (exonerated)** — multi-slice copy OOB DMA write; emitted `VkImageCopy` proven in-bounds, root cause attributed to Mesa ANV-Haswell execution; Windows/NVIDIA freeze still unconfirmed as the same cause.

---

## F-127 — yawgpu Vulkan: uniform-buffer OOB reads not zeroed (`robust_access`) — native Vulkan

- **Backend:** yawgpu native Vulkan (NVIDIA RTX 5060 Ti, Windows). Deterministic. **Metal-green.**
- **Found by:** `shader,execution,robust_access:linear_memory:addressSpace="uniform";access="read";*` — **24
  fail** (every `uniform`/`read` case: `containerType=vector|matrix` × `baseType=i32|u32|f16` × both
  `dynamicOffset` × all `shadowingMode`). `GPU buffer mismatch at byte 0: expected 0, got 3`. All
  `storage`/`workgroup`/`private`/`function` reads and all writes pass (`pass=462 fail=24`). Confirmed on
  the current lib (yawgpu `97b4827`, naga `01e4e710`), post-rebuild re-sweep 2026-06-21.
- **Observed:** an out-of-bounds **read of a uniform buffer** returns adjacent in-buffer data (`3`) instead
  of the robust `0`.
- **Root cause (Metal-vs-Vulkan classifier → SPIR-V bounds policy):** naga's `buffer` `BoundsCheckPolicy`
  is a **single knob governing both the storage AND uniform address spaces** (naga `proc/index.rs`:
  *"applies only to accesses to storage and uniform globals"*). **F-112** set `buffer = Unchecked` on
  devices exposing `VK_EXT_robustness2`/`robustBufferAccess2` (to fix the NVIDIA workgroup-atomic coherence
  violation). That is correct for **storage** (robustBufferAccess2 zeroes storage OOB at byte granularity —
  the `storage` robust_access cases pass), but **wrong for uniform**: robustBufferAccess2 only bounds-checks
  uniform buffers at `robustUniformBufferAccessSizeAlignment` granularity (up to 256 B on NVIDIA), so a
  sub-granularity OOB uniform read returns real adjacent data, not 0. **Metal stays `buffer = Restrict`
  (software clamp → in-bounds 0) and passes** — and these uniform cases were green natively under the
  pre-F-112 `Restrict` policy, so F-112 traded uniform robustness for storage coherence.
- **Cross-check:** wgpu-native (Vulkan) crashes the whole file (366, its known panic-heavy `robust_access`
  state, F-071/F-078) — no usable oracle; Dawn + yawgpu/Metal green is the spec reference.
- **Fix direction (Jun-21, naga era — now historical):** keep `buffer = Unchecked` for storage but
  `Restrict` for uniform via a per-address-space split of naga's `buffer` policy. **Superseded —
  naga was removed (see below).**
- **ROOT CAUSE + widening (2026-06-28, Tint era):** Dawn runs the full `robust_access:linear_memory`
  `fail=0` vs **216 fail** on yawgpu Vulkan → all **real yawgpu defects**. The failure widened far
  beyond the original 24 uniform/read: by addressSpace `workgroup=162`, `uniform=48`, `private=3`,
  `function=3`; by access `read=96`, `write=120`. **Cause = the naga→Tint frontend migration**: between
  `97b4827` (Jun-21) and `05bf865` (Jun-28, 84 commits) yawgpu removed naga and made Tint the sole
  frontend (`64fe785`/`7fda995`/`b0dad39`). naga had a **per-address-space** `BoundsCheckPolicy` —
  F-112 set only `buffer = Unchecked`, leaving workgroup/function/private software-clamped. Tint's
  SPIR-V path exposes only a **single whole-shader `robust` flag**, and yawgpu drives it as
  `robust = !unchecked_buffer_bounds` with `unchecked_buffer_bounds = hal_device.robust_buffer_access2()`
  (`yawgpu-core/src/compute_pipeline.rs:271` + `render_pipeline.rs:735` → `shader_tint.rs:70` →
  `yawgpu-tint generate_spirv(robust)`). So on a robustBufferAccess2 device (NVIDIA), Tint robustness
  is turned **OFF for the whole shader**; the device feature only zeroes *buffers*, so
  **workgroup/function/private OOB and writes lose all clamping** → the widening. (Storage still passes
  via the device feature; uniform partially fails at the 256 B granularity — the original F-127 core.)
- **Fix direction (Tint era):** keep Tint robustness **ON** for non-buffer address spaces while still
  relying on device robustBufferAccess2 for buffers (restore naga's per-address-space behaviour under
  Tint). If Tint cannot express per-space robustness, enable it wholesale and re-validate F-112
  (`coherence:corr`, which motivated turning buffer checks off). yawgpu work item (yawgpu-core
  robustness wiring + yawgpu-tint). xfail'd (66 clean cases) in `expectations/yawgpu-vulkan.txt`; ~30
  mixed pass/fail-subcase cases can't be pinned at query granularity (54 residual subcases).
- **Status:** OPEN — root-caused to the naga→Tint migration (single whole-shader `robust` flag),
  2026-06-28. Supersedes the Jun-21 naga `BoundsCheckPolicy` analysis above (naga removed).

---

## F-128 — `textureStore` to `rgb10a2unorm` "wrong pack" was a CTS oracle bug — RESOLVED (not a yawgpu defect)

- **Reclassified 2026-06-28** (was: "yawgpu Vulkan HAL packs `rgb10a2unorm` wrong", OPEN 2026-06-21).
  The original finding mis-attributed this to the Vulkan HAL on **"Metal-green ⇒ Vulkan/HAL defect"**
  reasoning. A **Dawn cross-check disproves that**: Dawn (the reference impl) fails the same 20
  `rgb10a2unorm` cases **identically** → the fault is the **CTS port's expected-value oracle**, not any
  backend.
- **Root cause:** the `rgb10a2unorm` store input set includes `0.5`, and `0.5 × 1023 = 511.5` is an exact
  10-bit quantization **tie**. The oracle's `std::llround` rounds half away from zero → 512 (`byte 2` =
  `0x00`); GPUs that round the tie down store 511 (`0xF0`/`0xDF`) — hence `expected 0, got 240`. Both
  neighbours are spec-permitted at an exact half, but the store comparison demanded **byte-exact** equality.
  "Metal-green" was a red herring: the Apple GPU happens to round the tie up to 512, matching the
  too-strict oracle.
- **Fix (CTS oracle, backend-independent):** compare normalized (unorm/snorm) store components with **±1
  ULP** instead of byte-exact (`texture_utils.cpp` `normalizedStoreTexelMatches`); int/float/padding stay
  exact. Commit `833954c` (with the sibling textureLoad-sRGB, storage-textureLoad-coord, and trig-validation
  `absBigInt` oracle fixes — all Dawn-confirmed). Post-fix: `rgb10a2unorm` passes on **both** yawgpu Vulkan
  and Dawn.
- **Lesson:** "Metal-green, Vulkan-fail" does **not** imply a Vulkan/HAL defect — a spec-permitted rounding
  tie can split per-GPU. Cross-check against Dawn (or another reference) before attributing to a backend.
- **Sibling real defect:** the `bgra8unorm` cases in the *same* `textureStore` test ARE a genuine yawgpu
  Vulkan defect (Dawn passes them; `expected 51, got 0`) — split out to **F-138**.
- **Status:** RESOLVED (oracle fix `833954c`, 2026-06-28).

---

## F-129 — yawgpu Vulkan: `fwidth`/`fwidthFine`/`fwidthCoarse` — `discard`+derivative errors + denormal interval — native Vulkan — CLOSED ((1) FIXED, (2) not-a-defect → xfail)

- **Backend:** yawgpu native Vulkan (NVIDIA RTX 5060 Ti, Windows). Deterministic. **Metal-green.**
- **Found by:** `shader,execution,expression,call,builtin,{fwidth,fwidthFine,fwidthCoarse}:f32:*` — **8 fail
  each, 24 total, 0 pass** (every `vectorize` ∈ {0,2,3,4} × `non_uniform_discard` ∈ {false,true}). Confirmed
  post-rebuild 2026-06-21 (yawgpu `97b4827`).
- **Two distinct sub-causes (each is half the cases):**
  1. **`non_uniform_discard=true` (12 cases) → `uncaptured error: queue submit cannot use an error command
     buffer`** (the pipeline becomes an error object). **Root cause:** naga lowers WGSL `discard` to SPIR-V
     **`OpKill`** (`back/spv/block.rs:3964`), which *terminates* the invocation; computing a derivative
     (`fwidth`) after a non-uniform discard then fails on Vulkan. WGSL `discard` is **demote-to-helper**
     semantics (the invocation must stay alive so neighbours' derivatives are well-defined) — i.e. naga
     should emit **`OpDemoteToHelperInvocation`** (SPIR-V 1.6 / `SPV_EXT_demote_to_helper_invocation`).
     Metal's `discard_fragment()` already keeps the invocation alive, which is why Metal passes. **naga
     SPIR-V backend conformance gap.**
  2. **`non_uniform_discard=false` (12 cases) → value mismatch** near the denormal boundary, e.g.
     `inputs=(-10, -1.17549e-38, …)` (±`FLT_MIN`), `expected[3]=[3.52648e-38, 4.70198e-38]; got
     4.70198e-38`. **CLASSIFIED — driver/CTS-interval artifact, NOT a yawgpu defect** (see Dawn-Vulkan
     oracle below): the got value is the acceptance interval's own *upper endpoint* (`4.70198e-38`), i.e.
     the CTS-computed interval is marginally too tight for the NVIDIA Vulkan denormal `fwidth` result. F-104/
     F-090-class artifact. → **xfail**, and a CTS-side acceptance-interval issue.
- **Cross-check — Dawn-Vulkan oracle now available (2026-06-24, this host).** Built Dawn CTS
  (`CTS_BACKEND=dawn`) and ran it on the same machine — adapter `backendType: vulkan`, NVIDIA GeForce RTX
  5060 Ti (driver 610.62), i.e. the *same API + GPU* as yawgpu's Vulkan HAL. **Dawn fails all 8
  `fwidth:f32:*` cases with the byte-identical `got 4.70198e-38` vs `expected[3]=[3.52648e-38, 4.70198e-38]`
  — exactly yawgpu's value.** Because Dawn handles `discard` correctly, *both* `non_uniform_discard` ∈
  {false,true} reduce to the same denormal value mismatch on Dawn (so the value defect is independent of the
  discard handling). The oracle reproducing yawgpu's exact value on the same hardware is definitive: the
  denormal `fwidth` mismatch is **not** a yawgpu/naga defect. (wgpu-native still gives no `fwidth` verdicts
  on this host; Dawn is now the Vulkan oracle.)
- **Status:** Sub-cause (1) **FIXED in naga fork `f82aa6a83`** (`fix(naga): discard is demote-to-helper, not
  a uniformity disruptor`) + yawgpu `b968d76`; naga now emits `OpDemoteToHelperInvocation` instead of
  `OpKill`. **Metal smoke test (2026-06-23, this host) confirms no regression and a net improvement:** the
  naga change is shared frontend code, so it was re-run on Metal — `shader,execution,statement,discard:*`
  went `6→4 fail` (the 2 `discard:derivatives` cases now pass), and all `{dpdx,dpdy,fwidth}*` execution +
  `validation,…,derivatives:*` stayed green (`pass=300` over the combined derivative/discard set, the only
  fails being the 4 in F-136).
  **CLOSED (2026-06-24).** Sub-cause (1) is a genuine yawgpu/naga defect, **fixed** (above). Sub-cause (2)
  is **not a yawgpu defect** — the Dawn-Vulkan-NVIDIA oracle (built this day, same GPU) produces the
  byte-identical `got 4.70198e-38` and fails all 8 `fwidth:f32:*` cases identically; the mismatch is a
  CTS acceptance-interval-too-tight / NVIDIA-denormal artifact (F-104/F-090-class). **Net effect on the
  suite:** after the sub-cause (1) fix, yawgpu's `non_uniform_discard=true` cases no longer become error
  pipelines — they reduce to the *same* sub-cause-(2) denormal value mismatch as `discard=false` (and as
  Dawn), so all `{fwidth,fwidthFine,fwidthCoarse}:f32:*` cases now fail on the value check, identically to
  the Vulkan oracle. → **xfail** these in `expectations/yawgpu-vulkan.txt` (non-defect, Dawn-Vulkan-equal),
  and track the denormal `fwidth` acceptance-interval as a **CTS-side interval issue** (verify against
  upstream gpuweb/cts). No further yawgpu action.
- **Uncovered (separate, pre-existing on Metal):** the Metal smoke re-run surfaced **4 yawgpu Metal fails not
  from this fix** — `discard:{three_quarters,function_call}` (`useStorageBuffers ∈ {false,true}`). Present
  with **both** the pre-F-129 naga (`a98f6d3fc`) and the new rev, so independent of F-129. → see **F-136**.

---

## F-136 — yawgpu Metal: `discard:{three_quarters,function_call}` → error command buffer — Metal

- **Backend:** yawgpu Metal (macOS, this host). Deterministic; reproduces under `--isolate` (not collateral).
- **Found by:** `shader,execution,statement,discard:*` — **4 fail** (`pass=10`):
  `three_quarters` and `function_call`, each `useStorageBuffers ∈ {false,true}`. Symptom:
  `uncaptured error: queue submit cannot use an error command buffer` (no `compilationInfo` error surfaced —
  the failure is at encode/submit, not shader compile).
- **Oracle:** Dawn (Metal) passes all 14 `discard:*`. **yawgpu-only.**
- **Independent of F-129:** reproduces identically with the pre-F-129 naga rev (`a98f6d3fc`) and the current
  rev (`f82aa6a83`) — so the F-129 demote-to-helper change neither caused nor fixed it. Pre-existing; only
  now caught because the F-129 Metal smoke re-ran `discard:*` (prior sweeps were Dawn-only).
- **Status:** OPEN (2026-06-23). Not yet root-caused. `all`/`loop`/`continuing`/`derivatives`/
  `uniform_read_loop` discard variants pass on Metal; only the `three_quarters` (partial-quad discard) and
  `function_call` (discard inside a called fn) shapes fail.

---

## F-130 — Dawn: override shift-amount range check skipped when `lhs` const-folds to 0 (Metal)

- **Backend:** Dawn (Metal, macOS — local `out/Release` build). Deterministic. Surfaced while porting
  `shader,validation,expression,binary,bitwise_shift` (phaseSV2).
- **Found by:** `shader,validation,expression,binary,bitwise_shift:partial_eval_errors` — **48 fail**
  (`pass=208`). Every failing subcase is `lhs="const"; stage="pipeline"; value ∈ {32,33,64}` (i.e. shift
  amount ≥ bit width), across `op ∈ {<<,>>}`, `type ∈ {i32,u32}`, `vectorize ∈ {_undef_,2,3,4}`. The
  `value=31` pipeline subcases and all `stage="shader"` subcases pass.
- **Shader (upstream, verbatim):** `override o = 0u; fn foo() -> T { const v : T = 0; return v << o; }`
  with `o` supplied as a pipeline-override constant = `value`. Per WGSL §8.7, `e1 << e2` is a
  **pipeline-creation error** when `e2` is an override-expression and `e2 ≥ bitwidth(e1)`, independent of
  `e1`. Dawn const-folds `0 << o → 0` and **skips the override range check**, so no error is raised.
- **Port is faithful (proven, not a port bug):**
  - The `stage="shader"` siblings — `const v = 0; v << 32u` (const-expression shift) — **do** error on
    Dawn and pass our test, so Dawn correctly range-checks const-expression shifts; only the *override*
    (partial-evaluation) path is lenient.
  - The `lhs="var"` siblings — `var v = 0; v << o` — **do** error at pipeline creation and pass, so our
    `expectPipelineResult` plumbing (override constant application, error-scope capture) is correct.
  - Therefore the divergence is specifically: Dawn drops the override shift-range diagnostic when the LHS
    folds to the additive identity. A genuine Dawn spec-conformance gap.
- **Cross-check:** Dawn's own `webgpu-cts/expectations.txt` skips this test only on `android-pixel-10`
  (compat), not on desktop Metal — so upstream expects desktop Dawn to pass it; our local Dawn build
  diverges. yawgpu/wgpu-native not cross-checked (campaign is Dawn-only per current directive).
- **Status:** OPEN (2026-06-22). The port is left **unmasked** (48 documented subcase divergences) rather
  than added to `expectations/dawn.txt`: the failing case queries also contain passing subcases
  (`stage="shader"`, `value=31`), so a case-level expectation would create xpass noise
  (see [[expectations-are-case-level]]). Re-check on a newer Dawn build.

---

## F-131 — yawgpu: `bitcast` from a non-numeric type CRASHES the WGSL frontend (signal 6) — Metal

**RESOLVED** (yawgpu naga fork) — `unwrap()` panic lowering `bitcast` from a non-numeric type; Dawn + wgpu-native always handled cleanly (yawgpu-specific).

## F-132 — yawgpu: override-evaluated negative out-of-bounds array/matrix index not flagged at pipeline creation — Metal

**RESOLVED** (yawgpu naga fork) — negative override-evaluated OOB array/matrix index not flagged at pipeline creation; Dawn + wgpu-native always flagged it (yawgpu-specific).

## F-133 — upstream-naga (shared yawgpu+wgpu-native): WGSL-frontend validation/const-eval gaps vs tint — NOT yawgpu-specific

- **Backend:** yawgpu (Metal) **and** wgpu-native (upstream naga `01e4e71`) — **identical** behavior and
  byte-identical error messages; both diverge from Dawn/tint. Classified **shared-naga / upstream-naga**, NOT
  a yawgpu defect (per [[naga-fix-crosscheck-wgpu-native]]).
- **Found by:** the 2026-06-22 yawgpu cross-check of the phaseSV2 `shader/validation` port (Dawn-oracle
  green). ~**77k** yawgpu validation fails decompose into these upstream-naga gaps (each verified
  yawgpu==wgpu-native, Dawn passes):
  1. **Builtin const-eval not implemented (~76k)** — naga errors `"Not implemented as constant expression:
     <Builtin>"` on valid `const`-stage builtin calls: `mix`, `faceForward`, `refract`, `reflect`,
     `transpose`, `fma`, `pow`, `extractBits`, `unpack2x16float`, etc. (dominant: faceForward 23848, mix
     21461). Plus **premature abstract-float concretization** (`length`, `distance`, `normalize`: "the
     concrete type f32 cannot represent the abstract value … accurately" on large abstract-float const args
     tint keeps abstract). Same family as **F-124**.
  2. **`@diagnostic(...)` directive (parse, 384)** — `"@diagnostic(…) attribute(s) not yet implemented"`
     (gfx-rs/wgpu#5320); no duplicate/conflicting/scope validation
     (`parse,diagnostic:{duplicate_attribute_same_location, conflicting_attribute_different_location,
     valid_locations, diagnostic_scoping, warning_unknown_rule}`).
  3. **binary operators (569)** — `div_rem` const div/rem-by-zero in compound-assign (512),
     `short_circuiting_and_or` non-bool operands (45), `comparison` relational-on-bool (8), `and_or_xor`
     naga wrongly rejects valid `bool & bool` (4).
  4. **precedence (74)** — WGSL mixed-precedence "requires parentheses" rule (e.g. `mul` vs `shl`) unenforced.
  5. **early_evaluation (7)** — infinite float literal in override context.
  6. **statement (16)** — loop/behavior analysis (non-terminating loop, missing break).
  7. **insertBits (30)** `offset+count > bitwidth`; **textureSample/textureGather (22/38)** const texel-offset
     outside `[-8,7]` — range checks unenforced.
- **Cross-check:** representative cases for every family give matching yawgpu+wgpu-native fail vs Dawn pass
  (e.g. `mix:values:stage="constant";type="vec2<abstract-int>"` Dawn 125 pass / yawgpu 125 fail / wgpu 125
  fail identical; `precedence:binary_requires_parentheses:op1="mul";op2="shl"` pass/fail/fail).
- **Status:** **MOSTLY FIXED on yawgpu** (its naga fork advanced to `f887a4097`), **still open on
  wgpu-native** (older upstream naga). yawgpu fixed the bulk in its fork — const-eval builtins, binary-op
  validation, and `@diagnostic(...)` directive surfacing — taking `shader/validation` from **~77k → 6612**
  fails on Metal. The remaining 6612 are the not-yet-landed naga slices. wgpu-native (upstream naga) still
  shows the full ~73.7k. This stays classified **upstream-naga** (the gaps are in naga, not yawgpu-specific
  code); yawgpu simply leads upstream in closing them. CTS ports stay faithful (unmasked) and Dawn-oracle
  green. **execution** subgroup/quad correctly **skip** on yawgpu (no `subgroups` feature); `texture_utils` +
  texture execution pass.

---

## F-134 — upstream-naga (shared): `non_zero:concrete_vector_mix` bool-vector constructor const-eval CRASHES — Metal

**RESOLVED on yawgpu** (naga fork `a98f6d3fc`) — naga `select` const-eval hit `unreachable!()` (`constant_evaluator.rs:5138`) on a nested-`Compose` bool-vector condition (a bool vector built from a mixed scalar+subvector constructor, serialized via `select` for const-eval). The 16 `type=bool;inputSource=const` crashes are now `pass=320 crash=0`, Dawn-equal; full `non_zero` 2144/0. Still present on wgpu-native (older upstream naga). (The `ptr_params` "fails" co-seen at discovery were degradation collateral — `fail=0` isolated.)

---

## F-135 — CTS harness: fixture device handles leaked on `SkipTestCase` (surfaced as a yawgpu Vulkan device-creation ceiling) — RESOLVED

**RESOLVED** (CTS `4cacc03`) — CTS-harness defect, NOT yawgpu: `runner.cpp` skipped `fixture->finalize()` on `SkipTestCase`, leaking device handles into yawgpu's ~72-VkDevice ceiling; fix runs `finalize()` on skip; yawgpu exonerated.

---

_Add new findings as `F-00N` with the same fields._
