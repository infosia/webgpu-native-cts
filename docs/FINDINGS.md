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

## naga → Tint frontend migration — yawgpu (2026-06-28)

Between yawgpu `97b4827` (2026-06-21) and `05bf865` (2026-06-28, 84 commits), yawgpu **removed naga and
made Tint — Dawn's shader compiler — the sole WGSL frontend** (`64fe785`/`7fda995`/`b0dad39`). Consequence:
yawgpu's `shader/execution` and `shader/validation` are now byte-equivalent to the Dawn oracle, and the
entire naga-lineage finding class no longer manifests on yawgpu.

**Full yawgpu/Metal sweep under Tint (this baseline) — `fail=0 crash=0` everywhere:**

| area | pass | skip | fail | crash |
|---|---:|---:|---:|---:|
| `api/*` (`api/validation` + `api/operation`) | 450,926 | — | **0** | 0 |
| `shader/execution` | 725,445 | 119,141 | **0** | 0 |
| `shader/validation` | 500,375 | 166,767 | **0** | 0 |
| **total** | **1,676,746** | — | **0** | **0** |

Only carried Metal item: the 2 `draw,index_buffer_format_dirtying` cases (a CTS port-oracle quirk — the Dawn oracle fails them identically; `xfail`), so effective `fail=0`.
**Native Vulkan (Windows / NVIDIA RTX 5060 Ti) is confirmed identical** — same `fail=0 crash=0`, modulo the
documented non-defect `xfail`s (F-085, F-111, F-129-denormal, F-141; see `expectations/yawgpu-vulkan.txt`).

> **Re-swept on Metal (2026-07-02), still `fail=0`.** A later yawgpu update (real hardware-limit reporting
> + `subgroups` exposure) raised Metal coverage to Dawn's level and briefly surfaced 19 fails, now all
> resolved: **F-142** (5, a real yawgpu over-strict limit check — fixed in yawgpu `e7eba41`) and **F-143**
> (14, a CTS-harness `hasLanguageFeature` probe gap — fixed in the harness; yawgpu was correct). yawgpu-Metal
> remains byte-identical to the Dawn oracle (the 2 shared `index_buffer_format_dirtying` `xfail`s).

**Findings resolved on yawgpu by the migration** (all were naga-lineage; Tint compiles them correctly —
**all still present on wgpu-native**, the one remaining naga-based backend):

| finding | what it was (naga) | under Tint |
|---|---|---|
| **F-124** | abstract-float / composite-result / f16-struct const-eval readback (`transpose`/`determinant`/`smoothstep` abstract, `modf`/`frexp` struct) | `fail=0` — Tint const-evals the readback snippets |
| **F-129 (1)** | `discard`+derivative lowered to SPIR-V `OpKill` (terminates) instead of demote-to-helper | `fail=0` — Tint emits `OpDemoteToHelperInvocation` |
| **F-133** | naga WGSL-frontend validation/const-eval gaps vs tint (~6.6k builtin const-eval, `@diagnostic`, binary/precedence/range checks) | `fail=0` — N/A under Tint |
| **F-134** | `non_zero:concrete_vector_mix` bool-vector const-eval crash | `fail=0` — N/A under Tint |
| **F-136** | `discard:{three_quarters,function_call}` encode/submit error | `fail=0` — N/A under Tint |

The same migration also fixed the Vulkan-path defects **F-127** (uniform-buffer robust reads) and **F-138**
(`bgra8unorm` `textureStore`) — see those entries (yawgpu `bd21cfb`). F-129's sub-cause (2), the denormal
`fwidth` value mismatch, was never a yawgpu defect (Dawn-Vulkan-equal) and remains a CTS-interval `xfail`.

The individual finding entries below are condensed to a one-line current-state record (RESOLVED on yawgpu,
still open on wgpu-native); see git history for the full naga-era root-cause analysis.

---

## Cross-backend sweep — current (Tint baseline; yawgpu-Metal re-swept 2026-07-02, others 2026-06-28)

The full ported suite (642 files) at the current backends. yawgpu's WGSL frontend is now Tint (see the
migration section above), so its shader behaviour is Dawn-equivalent. **yawgpu-Metal was re-swept 2026-07-02
after a yawgpu update** (real hardware-limit reporting + `subgroups` exposure), which briefly surfaced 19
fails — both root-caused and resolved (**F-142** in yawgpu, **F-143** in the CTS harness); the row below is
the post-fix state.

| Backend | fail | crash | verdict |
|---------|-----:|------:|---------|
| **Dawn** (oracle, Tint) | **2**¹ | 0 | fully green — pass **1,990,994** / skip 104,942. ¹the only fails are the 2 `index_buffer_format_dirtying` cases (see ¹ below) |
| **yawgpu — native Metal** (Tint, **re-swept 2026-07-02, post-fix**) | **2**¹ | 0 | green — `fail=0` on `api/operation` (228,600) + `shader/validation` (646,773) + `shader/execution` (822,209)³; the only 2 fails are the shared `index_buffer_format_dirtying` `xfail`s in `api/validation` (292,960 pass). **Fail profile byte-identical to the Dawn oracle again** after resolving **F-142** (5, fixed in yawgpu `e7eba41`) + **F-143** (14, a CTS-harness probe gap — yawgpu was correct). |
| **yawgpu — native Vulkan** (Windows / NVIDIA RTX 5060 Ti, Tint) | **0** | 0 | green — same `fail=0 crash=0` as Metal, modulo documented **non-defect** `xfail`s: **F-085** (per-sample), **F-111** (external-texture), **F-129** (denormal `fwidth`), **F-141** (NVIDIA memory-model). The last two real Vulkan defects **F-127** + **F-138** are resolved (`bd21cfb`) |
| **wgpu-native** (naga, Metal, `--isolate`²) | **6,858** | **38,565** | bring-up reference — fresh sweep 2026-06-28: pass 154,932 / skip 67,138. **Panic-dominated** crash by area: `api` 7,028 + `shader/execution` 31,537; fail by area: `api` 4,967 + `shader/validation` 1,693 (naga-lineage). Carries the F-001…F-021 panics + naga-lineage **F-124/F-129/F-133/F-134/F-136**; not triaged to `fail=0` |

¹ The 2 fails on **both** Dawn and yawgpu are `draw,index_buffer_format_dirtying`: the CTS oracle expects the
dirtied-format draw to succeed, but every backend (**including the Dawn oracle**) rejects it — a CTS-port-oracle
quirk (Dawn-confirmed), not a backend defect, carried as `xfail`. (The former **F-130** `bitwise_shift`
Dawn divergence **no longer reproduces** on the current Dawn build — `fail=0`; see F-130.) ² wgpu-native is
panic-heavy, so its sweep must run under `--isolate` (per-**case** granularity) to contain the process
aborts — its counts are **not** subcase-for-subcase comparable to the yawgpu/Dawn rows above (whole-suite
per-**subcase**). Per-area (per-case): `api/validation` pass 20,193 / fail 4,759 / crash 6,857;
`api/operation` pass 4,028 / fail 208 / crash 171; `shader/execution` pass 101,087 / fail 198 / crash
31,537; `shader/validation` pass 29,624 / fail 1,693 / crash 0.

³ yawgpu Metal was **re-swept 2026-07-02, post-fix** (the other rows are from 2026-06-28). The update's
coverage increase briefly exposed 19 fails, now all resolved: **F-142** (5 `requestDevice:limits,supported`)
was a real yawgpu over-strict limit-relationship check, fixed in yawgpu (`e7eba41`); **F-143** (14 subgroup
`requires` + `uniform_subgroup_ops`) was a CTS-harness gap (the non-Dawn `hasLanguageFeature` probe lacked a
`subgroup_id`/`subgroup_uniformity` case → returned `false` → inverted the expected result), fixed in the
harness — yawgpu was correct. `shader/execution` is unaffected by the fixes and stays `fail=0 crash=0` — the
whole-suite sweep on `e7eba41` is pass 822,209 / skip 22,377, identical to the pre-fix count.

MoltenVK (non-authoritative Vulkan coverage on macOS) still shows translation artifacts (**F-104**, **F-070**,
**F-139**) that are green on both native Metal and native Vulkan — see those findings.

---

## Status — current state & open findings

**yawgpu (primary subject): no open implementation defects (2026-07-02, both re-sweep findings resolved).**
A yawgpu update (real hardware-limit reporting + `subgroups` exposure) raised Metal coverage to Dawn's
level (skip 419,190 → 105,394) and, with the wider coverage, briefly surfaced 19 fails that are now all
resolved: **F-142** (5 `requestDevice:limits,supported`) was a **real yawgpu over-strict limit-relationship
check**, fixed in yawgpu (commit `e7eba41`); **F-143** (14 subgroup `requires` + `uniform_subgroup_ops`)
was a **CTS-harness gap** (the non-Dawn `hasLanguageFeature` probe lacked a `subgroup_id`/`subgroup_uniformity`
case → returned `false` → inverted the expected result), fixed in the harness — yawgpu was correct and
unchanged. With both fixed, yawgpu-Metal is **byte-identical to the Dawn oracle** again (only the 2 shared
`draw,index_buffer_format_dirtying` `xfail`s). The residual **coverage** (skip-count) gap vs Dawn was also
root-caused and closed: 427 of the 452 extra skips were **F-144**, a harness artifact (three
`shader/execution` language-feature gates compiled out on non-Dawn builds) — fixed 2026-07-02 and verified
per-file to Dawn parity; yawgpu was again correct and unchanged. Native **Vulkan** last swept 2026-06-28: `fail=0` modulo the
documented `xfail`s. Carried **non-defects**: the 2 `draw,index_buffer_format_dirtying` cases (a CTS
port-oracle quirk the Dawn oracle fails identically; Metal + Vulkan) and the Vulkan-only `xfail`s **F-085**,
**F-111**, **F-129** (denormal `fwidth`), **F-141**, all in `expectations/yawgpu-vulkan.txt`. No yawgpu
defect is masked.

**Open — Dawn (oracle):** none. The former **F-130** override-shift divergence **no longer reproduces** on
the current Dawn build (`fail=0`); Dawn now fails only the 2 shared `index_buffer_format_dirtying`
port-oracle cases.

**Open — wgpu-native (naga-based bring-up reference, not triaged to `fail=0`):** the panic-heavy validation
surface (F-001–F-004, F-007, F-012, F-013, F-015, F-017, F-019, F-021, F-027, F-028, F-036, F-052, F-056,
F-097, F-113), the `zero_init`/`robust_access`/`memory_layout` naga gaps (F-070, F-071, F-075, F-078,
F-084, F-088), and the naga-lineage shader findings that yawgpu resolved by moving to Tint (F-124, F-129,
F-133, F-134, F-136).

**MoltenVK** (non-authoritative — development / reference / Tier-2 Vulkan coverage on macOS only): a few
Vulkan→Metal translation artifacts (F-033, F-045, F-053, F-083, F-086, F-104, F-139), all green on both
native Metal and native Vulkan. Not yawgpu defects; not tracked as open.

---

## F-150 — hasvk: vertex-stage read-only storage-texture loads return 0/garbage — driver defect, NOT yawgpu

- **Backend/host:** yawgpu native Vulkan, Linux / Intel Iris 5100 (Haswell GT3, Mesa hasvk).
  Deterministic. Found on the 2026-07-04 full sweep; triaged 2026-07-05.
- Every `shader,execution,...,builtin,textureLoad:storage_textures_*` case with `stage="v"`
  fails (146 case queries, ~2,300 fail records at `--workers 1` in the 2026-07-05 re-runs) —
  vertex-stage loads from read-only storage textures return 0 (or garbage for some
  metadata queries). Deterministic at `--workers 1`; under worker concurrency individual
  subcases occasionally pass (the flake direction is fail-at-w1 → sometimes-pass-at-w4).
  The same subcase class also accounts for the per-stage-mixed fails in
  `textureDimensions:storage` / `textureNumLayers` (~585 records, 13 vertex-stage subcases
  per case), where the stage is a subcase parameter.
- **yawgpu is API-clean**: under `VK_LAYER_KHRONOS_validation` on native ANV the repro
  emits zero VUID / validation lines (`rerun-0705-swizzle/diag-vtxstorage.log`). Tint
  correctly decorates the read-only storage variables `NonWritable`, so
  `vertexPipelineStoresAndAtomics=false` (which hasvk reports) does not apply — NonWritable
  vertex-stage storage-image *reads* are spec-legal without that feature
  (VUID-RuntimeSpirv-NonWritable-06341 governs writes, and it does not fire).
- All cases pass on lavapipe with the identical yawgpu build; pre-existing across yawgpu fix
  rounds (identical set on the pre-round-2 HAL). Attribution: hasvk vertex-stage
  storage-image read defect (consistent with the GL-era Haswell
  `GL_MAX_VERTEX_IMAGE_UNIFORMS=0` hardware posture, which core Vulkan cannot express).
- **Expectations:** the failing subcase set is **nondeterministic run-to-run** (an
  exact-set xfail list flipped between fail and xpass across consecutive `--workers 1`
  runs), so `expectations/yawgpu-vulkan-intel-anv.txt` lists the **complete
  `textureLoad:storage_textures_*` `stage="v"` class** (160 case queries, from
  `--list-cases`): whichever subcases fail on a given run stay xfail and the rest
  surface as xpass (benign — xpass does not affect the runner exit code; ~740 xpass at
  `--workers 4`, fewer at `--workers 1`). Verified: the full textureLoad cluster is
  deterministically `fail=0 rc=0` with this file while out-of-class regressions stay
  visible. A future mass-xpass here means hasvk got fixed — then remove the block. The
  stage-mixed cases (`textureDimensions:storage`, `textureNumLayers:storage`, ... —
  the stage is a subcase parameter there) are NOT xfail'd — subcase granularity would
  xpass the passing fragment/compute-stage subcases (F-145 precedent); they stay
  visible (~585 fail records).

## F-149 — hasvk: alpha-to-coverage yields nonzero coverage at alpha <= 0 — driver-suspect, NOT yawgpu

- **Backend/host:** yawgpu native Vulkan, Linux / Intel Haswell (hasvk). Deterministic.
- `api,operation,render_pipeline,sample_mask:alpha_to_coverage_mask:*` — 90 fail records /
  30 fully-failing case queries ("alpha <= 0 result did not match zero coverage"). This is
  the residual after yawgpu's 2026-07-04 MSAA fix rounds took the sample_mask cluster from
  1,398 to 90; the `fragment_output_mask` subtree is fully green.
- **yawgpu is API-clean** (zero validation-layer lines on native ANV,
  `rerun-0705-swizzle/diag-alpha2cov.log`); passes on lavapipe. Haswell hardware
  alpha-to-coverage behavior suspected. A Dawn-oracle run on this host would finalize the
  attribution but Dawn is not built here yet.
- **Expectations:** all 30 case queries xfail'd in `expectations/yawgpu-vulkan-intel-anv.txt`.

## F-148 — hasvk: textureGather on rg32float/rg32uint/rg32sint selects wrong texels — driver-suspect, NOT yawgpu

- **Backend/host:** yawgpu native Vulkan, Linux / Intel Haswell (hasvk). Deterministic.
- All rg32* (8-byte texel) `textureGather` cases mismatch with plausible-but-wrong texel
  values (a different texel gets selected, not garbage): 621 fail records in
  `builtin,textureGather` plus 201 in `texture_view,texture_component_swizzle` — 822
  records over 129 case queries, every one of them subcase-mixed (sibling offsets/components
  pass). No other format width is affected.
- **yawgpu is API-clean** (zero validation-layer lines, `rerun-0705-swizzle/diag-rg32gather.log`);
  passes on lavapipe. Consistent with a Haswell 64-bit-texel gather selection quirk.
  Dawn-oracle comparison on this host pending (Dawn not built here).
- **Expectations:** NOT xfail'd — all 129 case queries are subcase-mixed, so case-level
  entries would generate hundreds of xpass records (F-145 precedent). Stays visible in
  sweeps until a Dawn oracle or raw-Vulkan repro finalizes attribution.

## F-147 — yawgpu created multisampled integer textures without a capability check (UB on hasvk); Haswell cannot do integer MSAA — yawgpu RESOLVED, cases remain host-limited

- **Backend/host:** yawgpu native Vulkan, Linux / Intel Haswell (hasvk). Deterministic.
  This is the 2026-07-04 sweep's "finding 4" (multisampled sint `textureLoad` → `queue
  submit cannot use an error command buffer`, exclusively r8/rg8/rgba8/r16/rg16/rgba16 sint).
- **Root cause (two layers):**
  1. **Hardware:** hasvk reports `sampledImageIntegerSampleCounts = SAMPLE_COUNT_1_BIT` —
     Haswell cannot sample multisampled integer images at all. WebGPU mandates
     `sampleCount=4` support for these formats, so the cases are unimplementable on this
     host (lavapipe supports 1|4, which is why they pass there).
  2. **yawgpu defect (RESOLVED):** yawgpu called `vkCreateImage` with `samples=4` anyway —
     invalid API usage (24× `VUID-VkImageCreateInfo-samples-02258` on the repro under the
     validation layer). Fixed in yawgpu `37f6a70` (2026-07-05): texture creation now queries
     `vkGetPhysicalDeviceImageFormatProperties` for the exact (format, type, tiling, usage,
     flags) and returns a clean `HalError` naming the format and sample count. Re-verified
     on native ANV: zero VUID lines; the cases now fail via a clean creation-time device
     error instead of UB.
- **Expectations:** the 24 fully-failing case queries (`textureLoad:multisampled` sint ×
  stages, `textureDimensions` sint MSAA) are xfail'd in
  `expectations/yawgpu-vulkan-intel-anv.txt` as a host hardware limitation. The 6
  `texture_component_swizzle:read_swizzle` sint×textureLoad cases carry the same signature
  but only their `texture_multisampled_2d` input subcases fail (57 of 114 per case) — NOT
  xfail'd (subcase granularity, F-145 precedent).

## F-146 — harness: POSIX `--workers` fork-without-exec breaks concurrent Metal children (backend-independent fake fails) — RESOLVED

- **RESOLVED** (`401108c`, 2026-07-03): POSIX workers now `fork`+`execv` (mirroring the
  `--isolate` child) and load the parent's ordered case plan (`--case-plan`, the phaseW4
  mechanism) — no re-enumeration. Verified: `command_buffer,*` `--workers 2` =
  170,202/0 (was fail=314); `immediate` `--workers 6` = 252/0 on yawgpu **and** Dawn;
  `kill -9` of a worker mid-run → 1 contained case-level `crash`, respawned worker
  completes the remainder. **Full yawgpu/Metal re-sweep at `--workers 6` (2026-07-03)
  is clean across all four areas** — 1,991,818 pass / 104,493 skip / 2 fail (the known
  port-oracle pair) / 0 crash, with zero `MTLCompilerService` incidents — so parallel
  sweep numbers are authoritative again.
- **Harness defect (ours), not a backend finding.** `--workers N` (N >= 2) on macOS
  mass-fails cases that are serial-green, `--workers 1`-green, and green when the same
  shard halves run as concurrently launched standalone processes. Every failure is the
  downstream `queue submit cannot use an error command buffer`. Reproduced on **yawgpu
  and the Dawn oracle identically** (yawgpu bisection: yawgpu `490743e` — `immediate`
  252/252 under workers, `sample_mask` ~500–666, `texture_component_swizzle` 6.8k–9.3k
  nondeterministic, `labels` ~8; all serial-green on both backends).
- Isolation (yawgpu/Metal, `webgpu:api,operation,command_buffer,*`): fork workers
  `--workers 2` → fail=314; standalone `--shard 0/2` → 85134/0; both shards standalone
  **concurrent** → 0 fail. So neither ordering nor GPU contention — the delta is the
  spawn mechanism.
- Cause: `phaseW3-fork-worker-no-reenum.md` made POSIX workers `fork()` **without
  exec**; its "parent never initializes WebGPU/Metal" safety precondition is invalid on
  macOS (framework/ObjC state initialized at dyld load in the parent is inherited; >= 2
  forked children doing concurrent Metal work misbehave; Apple does not support system
  frameworks between fork and exec). One child (`--workers 1`) happens to work.
- Consequence: all historical macOS `--workers >= 2` non-isolate fail counts (including
  the compile-canary spec's "75k–92k fake fails under workers" measurements) are
  dominated by this harness artifact; skip counts remain reliable.
- Fix task: `specs/fix-posix-worker-fork-exec.md` — return POSIX workers to
  fork+`execv` and reuse the phaseW4 `--case-plan` mechanism (already proven on
  Windows, `e19b37d`) so the phaseW3 re-enumeration cost does not come back.

## F-145 — yawgpu: immediate-data set-state under-validated (`pipeline_immediate`) — native Vulkan (Dawn-confirmed real defect) — RESOLVED

- **RESOLVED** (yawgpu `80bab07`, immediates required-slots + executeBundles-invalidation
  fix, 2026-07-03; verified Windows / NVIDIA RTX 5060 Ti). The fix reflects the pipeline's
  statically-used immediate words as a per-4-byte-word REQUIRED mask (via Tint's
  `GetImmediateBlockInfo`, excluding struct padding), stores it on the render/compute
  pipeline, checks `(written & required) == required` at draw/dispatch (encoder-
  invalidating validation error on shortfall), and clears the pass's immediate
  written-state on `executeBundles`. Re-run:
  `webgpu:api,validation,encoding,programmable,pipeline_immediate:*` on yawgpu native
  Vulkan = **181/181 (fail=0 crash=0)**, byte-identical to the Dawn oracle. No
  regression: `setImmediates` 378/0, operation `immediate` 252/0, `pipeline,immediates`
  30/0, `api,operation,render_pass` 70/0, `encoding,render_bundle` 113/0. yawgpu-core
  unit tests 439/0 (new required-slots / unused-variable / padding-excluded /
  executeBundles-invalidation coverage).

- **Was** (surfaced 2026-07-03 on Windows / NVIDIA RTX 5060 Ti after un-stubbing
  `api,validation,encoding,programmable,pipeline_immediate` — the port was stubbed
  when no backend exported `wgpuXxxSetImmediates`; yawgpu Block 94 now does, so the
  stub was lifted). yawgpu's brand-new immediate-data (push-constant) implementation
  **accepts encoder state the WebGPU spec requires be rejected**. Two sub-behaviors,
  both "expected validation error, got none":
  - **`required_slots_set` / `usage="partial"` (42 subcases)** — when a pipeline
    statically uses an immediate-data variable, every required 4-byte slot must be
    set via `setImmediates` before draw/dispatch. yawgpu does not enforce
    completeness: a partial set (missing required bytes) is wrongly accepted at
    finish/submit. All encoderTypes (compute pass / render pass / render bundle),
    all scenarios (scalar, vector, struct_padding, dynamic_indexing, mixed_types,
    multiple_variables), all stages.
  - **`render_bundle_execution_state_invalidation` (1 case, the `resetImmediates=false`
    subcase)** — `executeBundles` must invalidate the render pass's current
    immediate-data state; yawgpu leaves it valid, so a draw after `executeBundles`
    without re-setting immediates is wrongly accepted.
- **Dawn oracle confirms it is a real yawgpu defect, not a CTS port-oracle bug.**
  The same un-stubbed spec rebuilt against Dawn passes the full test **181/181
  (fail=0)**, including every `usage="partial"` case and both
  `render_bundle_execution_state_invalidation` subcases. yawgpu = `pass=138 fail=43
  crash=0`. Since Dawn (the reference impl) rejects exactly what yawgpu accepts, the
  port is correct and the gap is in yawgpu's immediate-data validation layer (not
  naga/Tint — this is set-state validation, distinct from the resolved F-064 which
  was WGSL frontend errors on immediate-data modules).
- **Fix is in yawgpu** (add the required-slots-complete check + the executeBundles
  immediate-state invalidation). Not yet `xfail`'d in
  `expectations/yawgpu-vulkan.txt`: the 42 `partial` cases are cleanly xfailable
  (one case each), but `render_bundle_execution_state_invalidation` runs both
  subcases under a single `:*` case query (subcase-granularity), so xfailing it
  would xpass the passing `resetImmediates=true` subcase — hence fixing yawgpu is the
  clean resolution rather than masking. The un-stub itself is correct and stays.

---

## F-144 — CTS-harness gap: three `shader/execution` language-feature gates compiled out on non-Dawn builds (427 subcases skipped vs Dawn) — RESOLVED, not a yawgpu defect

- **Backend/host:** yawgpu native **Metal**, Apple M2 (macOS). Deterministic (skip-vs-run divergence,
  not a fail). Found 2026-07-02 while attributing the residual whole-suite skip delta vs Dawn
  (105,394 vs 104,942): per-area, `shader/execution` accounted for 427 of the 452 subcases.
- **What:** three exec spec files query a WGSL language feature to decide whether their gated cases
  run, but wrapped the query in `#if defined(CTS_BACKEND_DAWN)` with a hard `return false` fallback,
  on the (stale) premise that only Dawn's lib exports `wgpuInstanceHasWGSLLanguageFeature` / defines
  the enum values. yawgpu exports the query and its `webgpu-headers/webgpu.h` defines the needed
  values, and it advertises all three features — so the gated cases skipped on yawgpu while Dawn ran
  them:
  - `expression/access/array/index.spec.cpp` — `uniform_buffer_standard_layout` (12 subcases)
  - `expression/call/user/ptr_params.spec.cpp` — `unrestricted_pointer_parameters` (85 subcases)
  - `expression/unary/address_of_and_indirection.spec.cpp` — `pointer_composite_access` (330 subcases)
- **Fix (harness):** gate on `#if defined(CTS_BACKEND_DAWN) || defined(CTS_BACKEND_YAWGPU)` so both
  robust backends query the real instance-level answer; wgpu-native (which genuinely lacks the
  export) keeps the unsupported fallback. Same class as **F-143** (harness under-modeled yawgpu's
  language-feature surface; yawgpu itself was correct and unchanged).
- **Verification (real Metal, post-fix):** yawgpu `index` 226/0, `ptr_params` 121/0,
  `address_of_and_indirection` 780/0 — pass counts and skip=0 **byte-identical to the Dawn build**
  (re-run same day); `fail=0 crash=0` on all newly-unskipped subcases. `parse,requires` stays 26/0.
  `statement/swizzle_assignment` (kept gated: the feature is unsafe-experimental in Tint and yawgpu's
  header has no enum value) skips 162 on **both** backends — no delta.
- **Not fixed here (both-backend skips, no Dawn delta):** `memory_layout` / `memory_model` runtime-skip
  their `uniform_buffer_standard_layout` subcases on Dawn too; `compute_builtins`
  `subgroup_size_attribute` needs Dawn-only `WGPUFeatureName_SubgroupSizeControl`. The remaining
  whole-suite delta (`api/validation` net 28 Dawn-more, `api/operation` net 3 yawgpu-more) is
  unattributed pending a case-level diff; the whole-suite tables predate this fix.
- **Status:** RESOLVED (CTS-harness gap, not a yawgpu defect), 2026-07-02.

---

## F-143 — CTS-harness gap: non-Dawn `hasLanguageFeature` lacked a subgroup probe (14 cases) — RESOLVED, not a yawgpu defect

- **Backend/host:** yawgpu native **Metal**, Apple M2 (macOS), yawgpu `3a30443`. Reliable (validation,
  deterministic), not flaky. Sweep 2026-07-02.
- **Newly exposed:** these cases only run now that yawgpu **exposes the `subgroups` feature** (the recent
  real-limits/subgroups update). Under the earlier baseline they were skipped, so the suite reported
  `fail=0`; the wider coverage surfaced the gap. yawgpu shares Dawn's **Tint** frontend, so the shader
  compiles identically — the divergence is in how yawgpu drives Tint's validation.
- **Found by (14 fails, all "expected a validation error for invalid shader, got none"):**
  - `shader,validation,parse,requires:wgsl_matches_api` — `feature="subgroup_id"`, `feature="subgroup_uniformity"`
    (2). A `requires subgroup_id;` / `requires subgroup_uniformity;` directive for a feature the API has
    **not** enabled must be a validation error; yawgpu raises none.
  - `shader,validation,uniformity,uniformity:uniform_subgroup_ops` `scope="subgroup"` (12) — a subgroup
    builtin (`subgroupAdd`, `subgroupMul`, `subgroupMax`, `subgroupMin`, `subgroupAll`, `subgroupAny`,
    `subgroupAnd`, `subgroupOr`, `subgroupXor`, `subgroupBallot`, `subgroupBroadcast`,
    `subgroupBroadcastFirst`) reached under **non-uniform** control flow must be a uniformity validation
    error; yawgpu accepts it.
- **Re-attribution (2026-07-02): NOT a yawgpu defect — a CTS-harness gap.** The "Dawn passes / yawgpu
  fails" split is a harness code-path artifact, not a real divergence. This suite's
  `ShaderValidationTest::hasLanguageFeature` (`src/webgpu/shader/validation/shader_validation_test.h`)
  sets the *expected* compile result and has two paths: the Dawn build queries the real
  `wgpuInstanceHasWGSLLanguageFeature` (→ true for `subgroup_id`/`subgroup_uniformity`), while the
  **non-Dawn (yawgpu) path behaviorally trial-compiles a canonical snippet per feature — and had NO
  case for `subgroup_id`/`subgroup_uniformity`, so it hit the `else` and returned `false`.** That false
  turned both tests' expectations upside down: `requires:wgsl_matches_api` expected `requires subgroup_*;`
  to fail (yawgpu correctly compiles it → "got none"), and `uniform_subgroup_ops` computed
  `isUniform=false` so it expected an error on a shader yawgpu correctly accepts. yawgpu reports both WGSL
  language features unconditionally and drives Tint's uniformity analysis **identically to Dawn** (same
  compiler) — it is correct.
- **Disposition:** **RESOLVED in the harness** (yawgpu unchanged). Added `subgroup_id` /
  `subgroup_uniformity` `requires`-directive probes to the non-Dawn `hasLanguageFeature` path (mirroring
  the existing `linear_indexing` / `texture_formats_tier1` probes). After rebuild: `requires:wgsl_matches_api`
  11/0 (was 9/2), `uniform_subgroup_ops` 52/0 (was 40/12); whole `uniformity` tree 181031/0, whole
  `parse,requires` 26/0 — no regression. yawgpu-Metal returns to byte-identical-to-Dawn on these trees.
- **Status:** RESOLVED (CTS-harness gap, not a yawgpu defect), 2026-07-02.

---

## F-142 — yawgpu Metal advertises supported limits it then rejects on `requestDevice` (5 cases)

- **Backend/host:** yawgpu native **Metal**, Apple M2 (macOS), yawgpu `3a30443`. Reliable (validation,
  deterministic), not flaky. Sweep 2026-07-02.
- **Newly exposed:** introduced by yawgpu's recent **real hardware-limit reporting** (Block 92). The
  adapter now advertises real Metal maxima, but requesting one of them at `requestDevice` time fails an
  internal cross-limit check.
- **Found by (5 fails, `api,operation,adapter,requestDevice:limits,supported:limit="…"`, each *"requestDevice
  should succeed"* but errored):**
  - `maxUniformBufferBindingSize` — *"required max_uniform_buffer_binding_size exceeds max_buffer_size"*
  - `maxStorageBufferBindingSize` — *"required max_storage_buffer_binding_size exceeds max_buffer_size"*
  - `maxComputeWorkgroupSizeX` / `…Y` / `…Z` — *"required max_compute_workgroup_size_{x,y,z} exceeds
    max_compute_invocations_per_workgroup"*
- **Root cause (hypothesis):** the `limits,supported` case requests a device requiring a single limit at
  its **advertised supported (adapter-max)** value with the other limits left at default. yawgpu's
  device-creation validation then compares that value against a *default* companion limit
  (`maxBufferSize`, `maxComputeInvocationsPerWorkgroup`) rather than the adapter-supported one and rejects
  a set it advertised as supported — the advertised maxima are mutually inconsistent under yawgpu's own
  validation.
- **Cross-check (attribution):** **Dawn passes all 5 on the same M2** (`fail=0` — verified this sweep). ⇒
  **real yawgpu defect**, not a CTS-port-oracle quirk.
- **Disposition:** **RESOLVED in yawgpu** (commit `e7eba41`). Root cause was real: yawgpu's
  `validate_required_limit_relationships` rejected a singly-raised supported limit against a *default*
  companion. `maxUniform/StorageBufferBindingSize` vs `maxBufferSize` and `maxComputeWorkgroupSize{X,Y,Z}`
  vs `maxComputeInvocationsPerWorkgroup` are NOT requestDevice constraints in WebGPU (buffer-fit is a
  per-binding bind-time check; the per-axis/product workgroup rule is enforced at `createComputePipeline`).
  Dropped the 5 rejection branches; the device now reports the requested (adapter-validated) values verbatim.
  After fix: `requestDevice:limits,supported` 105/0 (was 5 fail), full `requestDevice` 289/0,
  `capability_checks,limits` unchanged 9290/1795/0.
- **Status:** RESOLVED in yawgpu (`e7eba41`), 2026-07-02.

---

## F-141 — `memory_model,coherence:corr` (atomic_storage, intra_workgroup) — NVIDIA HW weak behavior, NOT yawgpu

- **Backend/host:** yawgpu native Vulkan **and Dawn**, NVIDIA RTX 5060 Ti (Windows). Reliable (4/4 on
  each), not flaky.
- **Found by:** `shader,execution,memory_model,coherence:corr` — **1 fail**,
  `memType="atomic_storage";testType="intra_workgroup"`: *"memory model test failed … (disallowed weak
  behavior observed)"* — `testResults[3] != 0` (e.g. yawgpu behaviors `[951,6897,0,2136]`, Dawn
  `[263,9587,0,134]`). The other 5 `corr` cases pass.
- **Cross-check (attribution):** **Dawn (the reference impl) fails the SAME case identically on the SAME
  GPU** (the counts differ because it is a statistical stress test, but both reliably observe the
  disallowed weak behavior). ⇒ **NOT a yawgpu defect** and **not** a CTS-port oracle bug — this NVIDIA
  GPU/driver reliably exhibits a storage-atomic intra-workgroup memory-ordering relaxation that WebGPU's
  memory model disallows. Same posture as F-085/F-139 (config-level, all impls agree).
- **Distinct from F-112** (`atomic_WORKGROUP` corr — a real yawgpu `Restrict`-bounds defect, RESOLVED
  `b602ff2`). This is `atomic_STORAGE`, and yawgpu == Dawn.
- **Not changed by yawgpu `bd21cfb`** (the `VK_KHR_vulkan_memory_model` enablement): failed before and
  after — it is a hardware/driver memory-model property, not a robustness/memory-model-output toggle.
- **Disposition:** `xfail` on yawgpu-vulkan with rationale (likely a known NVIDIA memory-model stress
  sensitivity / driver conformance gap). Revisit if the upstream CTS `corr` tuning or the NVIDIA driver
  changes; drop the entry if it starts passing (xpass).
- **Status:** xfail — not a yawgpu defect (Dawn-confirmed), 2026-06-28.

---

## F-140 — (unused finding number)

Skipped — never assigned. The sequence runs F-138 → F-139 → **F-141**; no F-140 was ever filed. Recorded here so the gap is intentional, not a lost entry.

---

## F-139 — yawgpu: `depth_clip_clamp` frag_depth viewport clamp — MoltenVK-only artifact, NOT a yawgpu defect

**MoltenVK-only translation artifact** (`1b2bd92`, 2026-06-27) — `api,operation,rendering,depth_clip_clamp:{depth_test_input_clamped:unclippedDepth=false, depth_clamp_and_clip:writeDepth=true}` (2 cases) fail **only through MoltenVK**. WebGPU requires shader-written `frag_depth` clamped to the viewport `[minDepth,maxDepth]` (and out-of-`[0,1]` primitives clipped); yawgpu's Vulkan HAL does this correctly on **native Vulkan** (`depthClampEnable` + `VK_EXT_depth_clip_enable`), and its Metal backend via an in-shader clamp transform (Tint MSL `clamp_frag_depth`). MoltenVK clamps Metal `[[depth]]` only to `[0,1]` and collapses `depthClampEnable` to `MTLDepthClipMode.Clamp` (defeating the clip), so these fail under translation only. Green on native Metal **and** native Vulkan. `xfail` in `expectations/yawgpu-vulkan.txt`; optional follow-up = port the in-shader clamp to the SPIR-V path. See memory `f045-frag-depth-clamp`.

---

## F-138 — yawgpu Vulkan: `textureStore` to `bgra8unorm` writes wrong/zero bytes — native Vulkan

**RESOLVED** (yawgpu `bd21cfb`, 2026-06-28) — the Tint SPIR-V storage path emitted `bgra8unorm` without the B↔R channel handling SPIR-V's `Rgba8`-only storage requires (21 cases, `expected 51 got 0`); fixed via the `bgra8unorm` storage-view path in `yawgpu-hal/src/vulkan/texture.rs`. Dawn always passed (real defect, not an oracle bug). `xfail`s dropped from `expectations/yawgpu-vulkan.txt`.

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

**2026-07-05 update — direct dispatches fixed, INDIRECT zero-dim still wedges.** yawgpu `dfcf93a`
implemented mitigation (b) for *direct* dispatches (CPU-side early-out on any zero workgroup count;
indirect dispatches were deliberately left untouched — their dims live in a GPU buffer and cannot be
pre-checked on the CPU). A supervised re-try of the quarantined `compute_pass` file froze the box
immediately (rebooted; empty `rerun-0705-zerodim/`). `compute_pass:dispatch_sizes` combines
`dispatchType ∈ {direct, indirect}` over the same zero-dim sizes, so with direct dispatches now
skipped the first executed **`vkCmdDispatchIndirect` with a zero dimension** is the trigger — the
ANV-Haswell wedge covers indirect zero-dim dispatches too. Consequences:
- **Both files stay quarantined on this host** (`compute_pass`, `pipeline_bind_group_compat` — the
  latter's `doCall()` also runs an indirect variant), and so does any future file that submits an
  indirect zero-dim dispatch.
- A durable yawgpu-side avenue exists: hasvk exposes `VK_EXT_conditional_rendering` (rev 2), which
  predicates `vkCmdDispatchIndirect` — a tiny pre-pass could write `pred = (x && y && z)` from the
  indirect args and wrap the dispatch in a conditional block, so a zero-dim indirect dispatch is
  culled before reaching the broken hardware path. Whether the predicate cull happens early enough
  on hasvk to dodge the wedge is unverified (testing it is itself a freeze-risk supervised run).
  Tracked in yawgpu `specs/tracking/cts-full-sweep-0704-native-vulkan.md`; not yet implemented —
  weigh the complexity (predicate pipeline + barriers + quirk gating) against one EOL GPU.

**Linux freeze landscape (so a future sweep stays survivable — both are host/driver, not yawgpu):**
- **F-137 compute_pass zero-dim dispatch** — *immediate*, deterministic, no DMAR. Quarantined.
- **F-126 copy OOB DMA write** — *load-dependent*: `copyTextureToTexture`+`image_copy` run clean cold
  even at workers 1–8 (verified 2026-06-25: `image_copy` ×5, 692k subcases, only 3 survivable DMAR
  faults), but a long *warm* session accumulates i915/IOMMU state until `image_copy` storms (~8 DMAR
  faults in ~40 s) and freezes. Quarantined for warm sweeps; cold results are clean
  (`copyTextureToTexture pass=31126 fail=0`, `image_copy pass=138408 fail=0`).

---

## F-136 — yawgpu Metal: `discard:{three_quarters,function_call}` → error command buffer — Metal

**RESOLVED on yawgpu** via the **naga→Tint migration** (yawgpu `05bf865`, 2026-06-28) — the partial-quad `three_quarters` and `function_call` discard shapes errored at encode/submit (4 cases, naga shader-lowering artifact; Dawn-Metal passed all 14). Tint lowers them correctly → `fail=0`. **Still present on wgpu-native** (naga).

---

## F-135 — CTS harness: fixture device handles leaked on `SkipTestCase` (surfaced as a yawgpu Vulkan device-creation ceiling) — RESOLVED

**RESOLVED** (CTS `4cacc03`) — CTS-harness defect, NOT yawgpu: `runner.cpp` skipped `fixture->finalize()` on `SkipTestCase`, leaking device handles into yawgpu's ~72-VkDevice ceiling; fix runs `finalize()` on skip; yawgpu exonerated.

---

## F-134 — naga-lineage: `non_zero:concrete_vector_mix` bool-vector const-eval CRASHES — Metal

**RESOLVED on yawgpu** via the **naga→Tint migration** (yawgpu `05bf865`, 2026-06-28; Tint has no such crash) — naga's `select` const-eval hit `unreachable!()` on a nested-`Compose` bool-vector condition (16 `type=bool;inputSource=const` crashes). `non_zero` `pass=2144 crash=0`, Dawn-equal. **Still present on wgpu-native** (naga).

---

## F-133 — naga-lineage: WGSL-frontend validation/const-eval gaps vs tint

**RESOLVED on yawgpu** via the **naga→Tint migration** (yawgpu `05bf865`, 2026-06-28) — naga's WGSL frontend had broad gaps vs tint (builtin const-eval "not implemented" ~76k: `mix`/`faceForward`/`refract`/`fma`/etc.; `@diagnostic` directive; binary-op/precedence/statement/`insertBits`/texel-offset range checks), surfaced as ~77k `shader/validation` fails byte-identical on yawgpu and wgpu-native (Dawn-green). With Tint as the frontend, `shader/validation` is `fail=0` on yawgpu (Metal + native Vulkan), Dawn-equal. **Still open on wgpu-native** (older upstream naga, full ~73.7k). CTS ports were always faithful + Dawn-oracle green.

---

## F-132 — yawgpu: override-evaluated negative out-of-bounds array/matrix index not flagged at pipeline creation — Metal

**RESOLVED** (yawgpu naga fork) — negative override-evaluated OOB array/matrix index not flagged at pipeline creation; Dawn + wgpu-native always flagged it (yawgpu-specific).

---

## F-131 — yawgpu: `bitcast` from a non-numeric type CRASHES the WGSL frontend (signal 6) — Metal

**RESOLVED** (yawgpu naga fork) — `unwrap()` panic lowering `bitcast` from a non-numeric type; Dawn + wgpu-native always handled cleanly (yawgpu-specific).

---

## F-130 — Dawn: override shift-amount range check skipped when `lhs` const-folds to 0 (Metal)

**NO LONGER REPRODUCES** (re-verified 2026-06-28 on the rebuilt `build-dawn`) — `shader,validation,expression,binary,bitwise_shift:partial_eval_errors` is now `pass=256 fail=0` on Dawn. The original 48 fails were a Dawn override-shift-range-check gap (`0 << o` const-folded to 0, skipping the WGSL §8.7 `e2 ≥ bitwidth(e1)` pipeline-creation diagnostic) on the then-current local Dawn build; a newer Dawn build enforces it. The port was always faithful and left unmasked; yawgpu/Tint always got it right. Was the only Dawn-oracle divergence — Dawn is now fully green except the 2 shared `index_buffer_format_dirtying` port-oracle cases.

---

## F-129 — yawgpu Vulkan: `fwidth*` `discard`+derivative + denormal interval — native Vulkan — CLOSED

**CLOSED** — two sub-causes. **(1) `discard`+derivative** (`non_uniform_discard=true` errored the pipeline): naga lowered WGSL `discard` to SPIR-V `OpKill` (terminate) instead of `OpDemoteToHelperInvocation` (demote-to-helper), breaking derivatives after a non-uniform discard — **resolved on yawgpu by the naga→Tint migration** (Tint emits demote-to-helper; was also fixed in the naga fork `f82aa6a83`). Still a defect on wgpu-native (naga). **(2) denormal `fwidth` value mismatch** near `±FLT_MIN` — **not a yawgpu defect**: the Dawn-Vulkan oracle on the same GPU produces the byte-identical `got 4.70198e-38` (the CTS acceptance interval is marginally too tight for the NVIDIA denormal result). Carried as `xfail` in `expectations/yawgpu-vulkan.txt`; tracked as a CTS-side interval issue.

---

## F-128 — `textureStore` to `rgb10a2unorm` "wrong pack" was a CTS oracle bug — RESOLVED (not a yawgpu defect)

**RESOLVED** (CTS oracle fix `833954c`, 2026-06-28) — not a backend defect: Dawn failed the same 20 `rgb10a2unorm` cases identically. Root cause = the port's oracle demanded byte-exact equality on an exact 10-bit quantization tie (`0.5×1023=511.5`, spec-permits 511 *or* 512); fix compares normalized store components with **±1 ULP** (`texture_utils.cpp` `normalizedStoreTexelMatches`). **Lesson:** "Metal-green, Vulkan-fail" does not imply a HAL defect — cross-check Dawn first. (The sibling `bgra8unorm` cases *were* a real yawgpu defect → [[F-138]].)

---

## F-127 — yawgpu Vulkan: robust-access OOB not zeroed (`robust_access`) — native Vulkan

**RESOLVED** (yawgpu `bd21cfb`, 2026-06-28) — the naga→Tint migration left Tint's single whole-shader `robust` flag wired to the F-112 robustBufferAccess2 toggle, so on NVIDIA robustness was turned **off shader-wide** and workgroup/function/private/uniform OOB reads + writes lost clamping (widened to 216 fail). Fix decouples them: Tint SPIR-V robustness stays **always on**, and the F-112 toggle instead drives `VK_KHR_vulkan_memory_model` (a memory-model concern, not robustness). `robust_access:linear_memory` `fail=0` (was 216); Dawn always passed. `xfail`s dropped. (The separate `memory_model,coherence:corr` NVIDIA-HW case is F-141, not this.)

---

## F-126 — yawgpu: texture-copy GPU out-of-bounds DMA write (whole-machine freeze) — native Vulkan (Intel/VT-d), cross-OS

**RESOLVED for yawgpu (exonerated)** — multi-slice copy OOB DMA write; emitted `VkImageCopy` proven in-bounds, root cause attributed to Mesa ANV-Haswell execution; Windows/NVIDIA freeze still unconfirmed as the same cause.

---

## F-125 — yawgpu: `atanh` f32 const-eval returns out-of-interval values (Metal)

**RESOLVED** (yawgpu `653ca12`, yawgpu-only) — `atanh` f32 const-eval out-of-interval near ±1; Dawn + wgpu-native always passed.

---

## F-124 — naga-lineage: abstract-float const-eval readback snippet fails (blocks `abstract_float` math)

**RESOLVED on yawgpu** via the **naga→Tint migration** (yawgpu `05bf865`, 2026-06-28) — naga could not const-evaluate the upstream `abstractFloatSnippet` (low/high-u32 split via `frexp`/`ldexp`/`select`/`floor`), erroring every `abstract_float` math builtin (scalar/vector/matrix-result + f16 `frexp`/`modf` struct). Tint const-evals it, so the whole fan-out is `fail=0` on yawgpu (Metal + native Vulkan), Dawn-equal. **Still open on wgpu-native** (older upstream naga, panics signal 6). Port was always faithful (Dawn-green).

---

## F-123 — yawgpu: `sub_neg` operator-precedence const-eval errors the pipeline (Metal)

**RESOLVED** (yawgpu `653ca12`, yawgpu-only) — `sub_neg` precedence const-eval errored the pipeline; Dawn + wgpu-native always passed.

---

## F-122 — yawgpu: `<<` (shift left) abstract-int const-eval errors the pipeline (Metal)

**RESOLVED** (yawgpu `653ca12`, yawgpu-only) — abstract-int `<<` const-eval errored the pipeline; Dawn + wgpu-native always passed.

---

## F-121 — yawgpu: newly-landed `shader-f16` errors the pipeline for f16 in access/bitcast (Metal)

**RESOLVED** (yawgpu `c937a32`+`a900cf8`) — newly-landed f16 const-eval / MSL half lowering errored pipelines (access/bitcast); Dawn-green throughout (wgpu-native skips, no shader-f16).

---

## F-120 — shader/validation under/over-validation — **RESOLVED** (yawgpu naga-fork: structural validation + full graph uniformity analysis)

**RESOLVED** (yawgpu naga-fork: structural fixes `8157263`/`f502b19`/`9bc23d6`/`b9d4393`/`c80b32f` + graph uniformity `0320944`/`66bee46`) — shared-naga under/over-validation; entire `shader/validation` area now yawgpu-clean on Metal (22781→0); wgpu-native still fails (upstream naga has no uniformity analysis).

---

## F-119 — yawgpu: `pack2x16float` / `unpack2x16float` error (Metal)

**RESOLVED** (yawgpu `bc1d44b`) — internal-f16 path disabled for `pack/unpack2x16float`; Dawn + wgpu-native always passed (yawgpu-specific feature-gating).

---

## F-118 — yawgpu/naga: `insertBits` const-eval returns 0 (Metal)

**RESOLVED** (yawgpu `ee77bf3`) — `insertBits` const-eval returned 0; Dawn-green throughout; wgpu-native still fails (upstream naga).

---

## F-117 — yawgpu/naga: `firstLeadingBit(u32)` of `0xFFFFFFFF` returns `0xFFFFFFFF` instead of 31 (Metal)

**RESOLVED** (yawgpu `ee77bf3`) — `firstLeadingBit(u32)` all-ones; Dawn-green throughout; wgpu-native still fails (upstream naga).

---

## F-116 — yawgpu: `arrayLength` off-by-one when binding size isn't a whole multiple of element stride (Metal)

**RESOLVED** (yawgpu `94694e2`, yawgpu-core) — `arrayLength` over-counted by one when binding size wasn't a stride multiple; Dawn-green throughout.

---

## F-115 — yawgpu: `textureLoad` on combined depth-stencil formats errors (Metal)

**RESOLVED** (yawgpu `baa0c81`, yawgpu-core/HAL) — combined depth-stencil aspect load path errored; Dawn-green throughout.

---

## F-114 — yawgpu: `textureSampleGrad` on 3D / cube textures errors (vec3 gradients) — cross-HAL (Metal)

**RESOLVED** (yawgpu `2d2594f`, naga fork `430e6e3c8`) — naga MSL emitted `gradient2d` regardless of texture dimension; Dawn-green throughout.

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

## F-112 — yawgpu Vulkan: workgroup-class atomics violate read-read coherence (`corr`) — native Vulkan

- **RESOLVED** (yawgpu `b602ff2`, 2026-06-16): `shader,execution,memory_model,coherence:corr` (`atomic_workgroup;intra_workgroup` non-RMW, 1 subcase) — workgroup-atomic read-read coherence violated (WebGPU-disallowed `r0==1 && r1==0`) on native Vulkan (NVIDIA RTX 5060 Ti); wgpu-native passed the same case on the same GPU. Not a naga defect: yawgpu and wgpu-native emit byte-identical workgroup-atomic SPIR-V (GLSL450, `scope=Workgroup`, `semantics=0`, no `Coherent`), verified by reassembling wgpu-native's `VK_APIDUMP_SHOW_SHADER` capture. Cause was yawgpu's SPIR-V `buffer` bounds-check policy = `Restrict`, whose software clamp (`OpArrayLength`+`OpISub`+`UMin`) on storage-buffer accesses breaks the NVIDIA driver's coherence; SPIR-V/Vulkan version and zero-init mode ruled out. Fixed by gating `buffer` on `VK_EXT_robustness2`/`robustBufferAccess2` (→ `Unchecked` when present; `index`/`image_load` stay `Restrict`; Metal/MSL unchanged); design in yawgpu `specs/blocks/60-real-backends.md` § "CTS finding F-112". Re-verified native Vulkan: `coherence:*` 27/27, `weak`/`atomicity`/`barrier`/`adjacent`/`texture_intra_invocation_coherence` no failures, validation-layer clean. Never added to `expectations/yawgpu-vulkan.txt`.

---

## F-111 — yawgpu Vulkan: external textures unsupported (uncaptured error where validation expected) — native Vulkan

**RESOLVED** (feature gap, 2026-06-15) — naga SPIR-V doesn't lower `texture_external` on Vulkan; capability-gated skip, 2 cases xfail in `expectations/yawgpu-vulkan.txt`; Metal has full support.

---

## F-110 — yawgpu Vulkan HAL: `triangle-strip` primitive restart not applied — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `41751d0`): `api,operation,render_pipeline,primitive_topology:basic:topology="triangle-strip";primitiveRestart=true` — the Vulkan input-assembly state hardcoded `primitiveRestartEnable=false`, so the strip was not cut at the sentinel (`expected 0, got 255`; 2 cases, indirect=false/true). Fix enables primitive restart iff the topology is a strip. Native-Vulkan-only (Apple masks it; Metal strips restart implicitly); re-verified native Vulkan `primitive_topology 20/0`. Metal/Noop unaffected.

---

## F-109 — yawgpu Vulkan HAL: depth clip/clamp wrong — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `1a0f9b4`): `api,operation,rendering,depth_clip_clamp:*` — WebGPU always clamps fragment depth to the viewport before the test, but the HAL mapped `depthClampEnable=unclippedDepth`, so the default `unclippedDepth=false` path only clamped to `[0,1]` (`expected 0, got 255`; 2 cases). Fix enables `VK_EXT_depth_clip_enable`+`depthClamp`, sets `depthClampEnable=TRUE` always, and controls clipping independently via `depthClipEnable=!unclippedDepth`. Native-Vulkan-only (Apple masks it); re-verified native Vulkan `depth_clip_clamp 3/1skip/0` (`unclippedDepth=true` intentionally skips). Metal/Noop unaffected.

---

## F-108 — yawgpu Vulkan HAL: srgb→non-srgb `viewFormat` reinterpretation applies wrong gamma on render+resolve — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `7b3a05c`): `api,operation,texture_view,format_reinterpretation:render_and_resolve_attachment:*` — rendering+resolving through a non-srgb `viewFormat` of an srgb texture stored the wrong gamma (`expected 179 +/- 2, got 218`; 4 cases). Fix threads the reinterpreted view format core→HAL and uses it for the Vulkan color/resolve attachment descriptions, image views, and clear values. Native-Vulkan-only (Apple masks it); re-verified native Vulkan `format_reinterpretation 6/0`. Metal/GLES/Noop unaffected.

---

## F-107 — yawgpu Vulkan HAL: `storeOp: "discard"` not honored (content stored instead of discarded) — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `3a10aa7`): `api,operation,render_pass,storeOp:*` + `storeop2:*` — the Vulkan HAL mapped `"discard"` to `VK_ATTACHMENT_STORE_OP_DONT_CARE`, which kept the drawn value on the immediate-mode NVIDIA path (`expected 0 got 255`; 18 cases, all `storeOperation="discard"`). Fix explicitly clears every discarded attachment subresource to zero after `vkCmdEndRenderPass`. Native-Vulkan-only (Apple tilers drop tile content, masking it); re-verified native Vulkan `storeOp 26/0`, `storeop2 2/0`. Metal/Noop unchanged.

---

## F-106 — yawgpu Vulkan HAL: missing write→read barrier for indirect-args / index / copy-source reads — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `cts(F-106)` `858de27`): `api,operation,memory_sync,buffer,multiple_buffers:wr` — Vulkan HAL omitted the write→read barrier when the read uses the buffer as indirect-args / index / copy-source (18 cases; `expected 1, got 0`). Fix adds the missing dst access/stage (INDIRECT_COMMAND_READ / INDEX_READ / TRANSFER_READ). Latent on Apple (coherent memory masked it), exposed on NVIDIA native Vulkan. Verified native Vulkan; `multiple_buffers 263/0` Metal + MoltenVK (no regression).

---

## F-105 — yawgpu: robust-access write to a `bool` workgroup array not clamped — native Vulkan

- **RESOLVED 2026-06-15** (yawgpu `cts(F-105)` `87cc2c6`, naga fork `7dd824389`): `shader,execution,robust_access:linear_memory` — OOB write to a `bool` workgroup array not clamped (`expected 0, got 1`; 3 cases, bool-only). Native-Vulkan-only (NVIDIA exposed it, Apple masked it); the naga SPIR-V backend emitted the wrong `bool` array stride. Verified native Vulkan; `robust_access 1068/0` on Metal + MoltenVK (no regression).

---

## F-104 — MoltenVK translation artifact: `copyTextureToTexture` wrong data — Metal AND native Vulkan green

**MoltenVK translation artifact** (not a yawgpu defect; native-Vulkan-green + Metal-green) — `copyTextureToTexture` color (2D + 3D, ~14.5k cases) reads wrong data only under MoltenVK.

---

## F-103 — yawgpu Vulkan-HAL: 3D image-copy loses/corrupts non-zero depth slices (+ stencil8 stencil-only) — Vulkan-specific, native-confirmed

- **RESOLVED 2026-06-14** (yawgpu `cts(F-103)` `e7db246` — "fix Vulkan HAL 3D/multi-slice copy slice stride"): `api,operation,command_buffer,image_copy:{rowsPerImage_and_bytesPerRow,offsets_and_sizes,origins_and_extents}` — yawgpu Vulkan-HAL read back wrong data at non-zero 3D z-slices (7450 cases across 43 formats) + stencil8 stencil-only (96); Metal always green. Native-Vulkan-confirmed; re-verified `image_copy 138408/0` (was `fail=7546`).

---

## F-102 — yawgpu: default/auto bind-group-layout compatibility validation diverges (both directions) — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu `cts(F-093d)` auto-layout BGL exclusive-pipeline compat, re-verified green Metal + MoltenVK): `pipeline_bind_group_compat:default_bind_group_layouts_never_match,{compute,render}_pass` — default/auto BGL compatibility mis-keyed in both directions (18 cases, cross-HAL; Dawn + wgpu-native pass). `pipeline_bind_group_compat 2520/0`.

---

## F-101 — yawgpu: per-stage resource binding limits not enforced at auto-layout pipeline creation — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `capability_checks,limits,*PerShaderStage/*Stage:createPipeline,at_over` — per-stage resource binding limits not enforced at **auto-layout** pipeline creation (312 cases, cross-HAL; explicit-layout paths were fine). Separate MoltenVK-only residual: `maxComputeWorkgroupStorageSize` at-limit SPIR-V compile (30, artifact). wgpu-native crashes/fails heavily (bring-up reference).

---

## F-100 — yawgpu (naga frontend): out-of-range `@binding` rejected at `createShaderModule`, not pipeline creation — cross-HAL

- **RESOLVED on yawgpu 2026-06-14** (yawgpu `cts(F-100)` `16ee140`): `capability_checks,limits,maxBindingsPerBindGroup:createPipeline,at_over` — naga frontend rejected an out-of-range `@binding` at `createShaderModule` instead of pipeline creation (validation-timing divergence, 12 cases, cross-HAL). `maxBindingsPerBindGroup 43/0`. wgpu-native (upstream naga) may still crash.

---

## F-099 — yawgpu: `rgba16unorm`/`rgba16snorm` not gated behind `texture-formats-tier1` — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `capability_checks,features,{texture_formats,texture_formats_tier1}` — `rgba16unorm`/`rgba16snorm` treated as core, not gated behind `texture-formats-tier1` (28 cases, cross-HAL). wgpu-native is worse here (crashes on tier1 16-bit-norm formats; bring-up reference).

---

## F-098 — yawgpu: `texture-component-swizzle` feature gating not enforced — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `capability_checks,features,texture_component_swizzle:only_identity_swizzle` — a non-identity swizzle view wasn't rejected on a device without the feature (18 cases, cross-HAL; wgpu-native shares the gap).

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

## F-096 — yawgpu: texture subresource usage-scope conflicts not detected — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu `cts(F-096)` `5ed5ada`, re-verified green Metal + MoltenVK): `resource_usages,texture,*` — texture subresource usage-scope hazards not tracked (851 cases, cross-HAL; the texture analog of F-095). `resource_usages/texture/* 6556/0`.

---

## F-095 — yawgpu: buffer usage-scope conflicts not detected in a render pass — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu `cts(F-095)` `c0e5ba7`, re-verified green Metal + MoltenVK): `resource_usages,buffer,{in_pass_encoder,in_pass_misc}` — same buffer used as read-only + writable-storage in one render-pass scope not rejected (296 cases, cross-HAL; Dawn + wgpu-native pass). `resource_usages/buffer/* 1422/0`.

---

## F-094 — yawgpu: image-copy buffer/layout validation gaps — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu `cts(F-094)`): `api,validation,image_copy/*` buffer/layout validation gaps (required-bytes under-validation, offset-alignment over-validation, offset+bytesPerRow, d/s aspect; 3513 cases, cross-HAL). Re-verified green Metal+MoltenVK (`image_copy/* 65794/0`).

---

## F-093 — yawgpu: encoding-validation gaps (compressed copy / encoder-state / pipeline-layout / vertex-OOB) — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu `cts(F-093a-d)`, re-verified green Metal + MoltenVK): `encoding,{copyTextureToTexture,render/draw,encoder_open_state,pipeline_bind_group_compat}` — compressed-copy over-validation [dominant], vertex-buffer OOB, encoder-open-state error timing, auto-vs-explicit pipeline-layout compat (cross-HAL). `copyTextureToTexture 9254/0`, `encoder_open_state 119/0`, `draw 15708/2`† (the 2 are the `index_buffer_format_dirtying` port-oracle cases the Dawn oracle fails identically, not a defect), `pipeline_bind_group_compat 2520/0`.

---

## F-092 — yawgpu: render-pass descriptor & attachment-compatibility validation gaps — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `render_pass,{render_pass_descriptor,attachment_compatibility}` — depth/stencil loadOp-vs-readOnly under-validation [864], pipeline-vs-pass depth read-only/format compat, bytes-per-sample, snorm-16 resolve (1082 cases, cross-HAL). `render_pass/* 12095/0`.

---

## F-091 — naga-MSL lineage: MSL writer panics on generated vertex shaders during render-pipeline creation

- **RESOLVED on yawgpu 2026-06-14** (naga-fork rev bump): `render_pipeline,vertex_state` — naga MSL writer panicked (signal 6) on generated vertex shaders during render-pipeline creation (518 crashes on Metal, naga-MSL lineage; MoltenVK was already green). `vertex_state 28151/0 crash=0` on Metal. wgpu-native (upstream naga) may still crash.

---

## F-090 — yawgpu: render-pipeline fragment-state validation gaps — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `render_pipeline,fragment_state` — color-target/blend/bytes-per-sample validation gaps (146 cases: maxColorAttachmentBytesPerSample under-validation, no-target/blend/blendable over-validation; cross-HAL). `fragment_state 10754/0`.

---

## F-089 — yawgpu: filtering sampler not rejected for a non-filtering sampler binding — cross-HAL

- **RESOLVED 2026-06-14** (yawgpu, re-verified green Metal + MoltenVK): `createBindGroup:binding_must_contain_resource_defined_in_layout` — a filtering sampler bound to a `non-filtering` BGL entry wasn't rejected (1 case, cross-HAL). `createBindGroup 2358/0`.

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

## F-087 — yawgpu: requestDevice limit & adapter-lifecycle conformance gaps — cross-HAL

- **RESOLVED** (yawgpu `0be6c55`; re-verified 2026-06-12): `api,operation,adapter,requestDevice` — defaults not honored, adapter not single-use, better-than-supported not rejected, advertised-vs-delivered limit mismatch (73 cases, cross-HAL). `requestDevice 289/0` Metal + MoltenVK, matching Dawn.

---

## F-086 — yawgpu/naga-SPIR-V: three single-case Vulkan divergences (compound eval order, discard derivatives, IO-struct-in-buffer) — MoltenVK-only (native Vulkan green)

**MoltenVK translation artifact** (not a yawgpu defect; native-Vulkan-green + Metal-green) — three single-case SPIR-V divergences (compound eval order, discard derivatives, IO-struct-in-buffer) only under MoltenVK.

---

## F-085 — Vulkan per-sample dispatch fragment builtins: `sample_mask` xfail (spec in flux) + `position` RESOLVED on yawgpu

**Split 2026-07-01** after a native-Vulkan re-run (NVIDIA RTX 5060 Ti) showed the two halves are not the same kind of finding:

- **`inputs,sample_mask` (88 subcases) — still xfail, NOT an implementation defect.** Under Vulkan per-sample dispatch the `sample_mask` input is the current sample's single bit (Vulkan semantics) where the current CTS oracle expects the full coverage mask. yawgpu, wgpu-native **and Dawn** diverge identically on the same GPU (this port reproduces Dawn `fail=88`); spec in flux (gpuweb#5457 / cts#4510 pending). Kept xfail in `expectations/{yawgpu,wgpu-native}-vulkan.txt`.
- **`inputs,position` (4 cases: `sampleCount=4; interpolation={perspective,linear},sample`) — RESOLVED on yawgpu (`90a269a`), no longer xfail.** WebGPU requires `@builtin(position)` to always be the pixel-center (fragment) coordinate, never a sample position (`fragment_builtins.spec.cpp:1011`); under Vulkan sample-rate shading the SPIR-V `FragCoord` builtin instead reflects the covered sample's location (the 4× MSAA offsets `0.375/0.125/…` vs expected `0.5`). The **current Dawn oracle passes these 4** (it reconstructs the pixel center via a polyfill), so this was a real yawgpu gap, not spec-in-flux — the earlier bundling with `sample_mask` (gpuweb#4777) was too conservative. Fixed by wiring Tint's `polyfill_pixel_center` option (+ a viewport-depth-range fragment push constant for the NDC-space z reconstruction) through the yawgpu-tint shim and the Vulkan HAL, matching Dawn's `RenderPipeline::NeedsPixelCenterPolyfill`. Re-verified native Vulkan: `inputs,position` **pass=32 fail=0** (was 28/4); full `fragment_builtins` `fail=0 xfail=88 xpass=0` with the updated expectations. The 4 position entries were dropped from `expectations/yawgpu-vulkan.txt`. (yawgpu ledger: `specs/tracking/cts-coverage.md` → F-085 position sub-part.)

---

## F-084 — wgpu-native: disallowed weak-memory behaviors on Metal (barrier/coherence/weak)

- **Backend:** wgpu-native only (yawgpu Metal passes these; Dawn passes).
- **Found by:** `shader,execution,memory_model`: `barrier:workgroup_barrier_load_store` (2),
  `coherence:corw1`/`corw2` (3), `weak:load_buffer` (1) — beyond the 12 F-082 cases it shares with yawgpu.
- **Observed:** disallowed weak behaviors observed in the stress harness — barrier/coherence guarantees
  violated on Metal.
- **Status:** **OPEN**; tracked as a **wgpu-native defect** (bring-up reference). Not masked.

---

## F-083 — yawgpu: workgroupBarrier does not order non-atomic storage-texture accesses — MoltenVK-only (native Vulkan green)

**MoltenVK translation artifact** (not a yawgpu defect; native-Vulkan-green + Metal-green) — `workgroupBarrier` storage-texture ordering lost only under MoltenVK.

---

## F-082 — naga-MSL lineage: storage-texture intra-invocation coherence broken on Metal

- **RESOLVED** (yawgpu `0be6c55`, naga rev bump; re-verified 2026-06-12): `memory_model,texture_intra_invocation_coherence` — storage-texture write→same-texel read within one invocation returned stale/zero on the MSL path (12 cases, naga-MSL lineage). `12/0` Metal + MoltenVK.

---

## F-081 — yawgpu regression: external-texture pipelines error "missing params buffer slot" — cross-HAL

- **RESOLVED** (yawgpu `4770131` — fragment-only external textures regained their params buffer slot; re-verified 2026-06-11): `render_pipeline,misc:external_texture` — external-texture pipelines errored "missing params buffer slot" (2 cases, regression). Metal `external_texture 2`. On Vulkan/MoltenVK the 2 now fail with the deliberate "not supported on the Vulkan backend" rejection (documented limitation, not a defect).

---

## F-080 — yawgpu regression: filtering-sampler + unfilterable-float texture no longer rejected — cross-HAL

- **RESOLVED** (yawgpu `9382206` — layout-aware filterable check; re-verified 2026-06-11 Metal + MoltenVK): `non_filterable_texture` — filtering sampler + unfilterable-float texture no longer rejected (32 cases, regression). `non_filterable_texture` green (160).

---

## F-079 — yawgpu regression: destroyed-resource errors fire outside the expected validation point — cross-HAL

- **RESOLVED** (yawgpu `9382206` — submit-time destroyed validation; re-verified 2026-06-11 Metal + MoltenVK): `setBindGroup:state_and_binding_index` + `queue,destroyed,query_set` — destroyed-resource errors fired outside the expected validation point (7 cases, regression). green.

---

## F-078 — naga lineage: validator treats `let`-propagated indices as const-expression OOB (robust_access) — NOT a yawgpu regression

- **RESOLVED** (yawgpu `0be6c55`, naga rev bump; re-verified 2026-06-12): `shader,execution,robust_access:linear_memory` — naga const-propagated a `let` index and raised a static-OOB validation error (over-validation; per WGSL a `let` is a runtime value), erroring every non-f16 compute pipeline (1068, naga-lineage). `linear_memory 1068/0` Metal + MoltenVK (genuine pass, not the earlier false pass).

---

## F-077 — shared-naga: max-bindings shader invalid; yawgpu panics in the MSL writer instead of erroring

- **RESOLVED** (yawgpu `d376a1b` — naga storage-access fix + Metal per-kind/per-stage binding slots; re-verified 2026-06-11): `sampling,sampler_texture:sample_texture_combos` — yawgpu panicked in the naga MSL writer (`module is not valid`) instead of erroring gracefully on a max-bindings generated shader. `sampler_texture` green Metal + MoltenVK, no panic. (Same commit introduced F-078/F-081.)

---

## F-076 — yawgpu: anisotropic filtering broken — both HALs, differently

- **RESOLVED** (yawgpu `726e8aa`; re-verified 2026-06-11): `api,operation,sampling,anisotropy` — anisotropic filtering broken (Metal: out-of-range `maxAnisotropy` not clamped consistently; MoltenVK: error command buffer; 3 cases). `anisotropy 3/3` green Metal + MoltenVK.

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

## F-074 — yawgpu: queue.writeBuffer ordering vs prior submits broken — MoltenVK-only (native-Vulkan confirm pending)

- **RESOLVED** (yawgpu `a034b24`; re-verified 2026-06-11): `memory_sync,buffer,multiple_buffers` `rw`/`ww` — `queue.writeBuffer` not ordered behind previously submitted command buffers (21 cases, MoltenVK-only). `multiple_buffers 260` green Metal + MoltenVK.

---

## F-073 — yawgpu: panic-abort on OOM-sized mappedAtCreation buffer — cross-HAL

- **RESOLVED** (yawgpu `726e8aa`; re-verified 2026-06-11): `buffers,map_oom:mappedAtCreation` — panic-abort (signal 6) on a ~9 PB `mappedAtCreation` buffer (cross-HAL). `map_oom` green, no abort.

---

## F-072 — yawgpu: zero-size map ranges fail — Metal-only

- **RESOLVED** (yawgpu `726e8aa`; re-verified 2026-06-11): `api,operation,buffers,map` — zero-size buffer / zero-length map ranges rejected on yawgpu Metal (~93 cases, Metal-only). `buffers,map` green on Metal + MoltenVK.

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

## F-070 — shared-naga (yawgpu + wgpu-native): workgroup write_layout, struct_inner_align, matCx3 padding, loop shadowing

- **RESOLVED for yawgpu 2026-06-14** (naga fork: `ebec34ae4` shadow:loop, `197a3ddd` matCx3/struct padding-preserving MSL stores, `ee37a074` struct_inner_align IR alignment): shared-naga `memory_layout` / `padding` / `shadow:loop` defects (workgroup write_layout, struct_inner_align, matCx3 padding, loop shadowing) — fail identically on yawgpu + wgpu-native (Dawn green). Metal-green (`memory_layout 434/0`, `padding 18/0`, `shadow 7/0`); native-Vulkan-confirmed resolved 2026-06-14. Remaining MoltenVK-only `memory_layout`/`zero_init` residue = SPIRV-Cross translation artifact. wgpu-native (upstream naga) still shares the defects.

---

## F-069 — yawgpu: workgroup-memory loads read zeros (memory_layout) — Metal-dominant

**RESOLVED** (yawgpu `a034b24`) — `var<workgroup>` round-trips read back zeros (55 cases, Metal-dominant); remaining `memory_layout` tracked under F-070.

---

## F-068 — yawgpu: vertex-buffer OOB robustness broken for indirect draws — cross-HAL

- **RESOLVED** (yawgpu `f857f3f` — Metal vertex pulling + Vulkan robustBufferAccess; re-verified 2026-06-11): `robust_access_vertex:vertex_buffer_access` — vertex-buffer OOB robustness broken for indirect draws (Metal 89 / MoltenVK 129 cases). Metal green (1856) + native Windows/Vulkan green; a 125-case MoltenVK-only residual is a confirmed MoltenVK artifact.

---

## F-067 — yawgpu: under-validates depth/stencil buffer copies & buffer device-mismatch — cross-HAL

- **RESOLVED** (yawgpu `f9a076e`; re-verified 2026-06-11 Metal + MoltenVK): `image_copy,buffer_related` — under-validated combined-DS aspect-`all` buffer copies, mismatched-device buffers, and [Metal] non-256 `bytesPerRow` for single-aspect DS (Metal 15 / MoltenVK 8). `image_copy/buffer_related` green.

---

## F-066 — yawgpu: setViewport rejects an in-bounds viewport as out-of-bounds — cross-HAL

- **RESOLVED** (yawgpu `f9a076e`; re-verified 2026-06-11 Metal + MoltenVK): `dynamic_state:setViewport,xy_rect_contained_in_bounds` — rejected an in-bounds viewport as out-of-bounds (2 cases). `dynamic_state` green.

---

## F-065 — yawgpu: error-scope out-of-memory type / filter handling — cross-HAL

- **RESOLVED** (yawgpu `f9a076e` + `de7bae3`/`ef43eae`; re-verified 2026-06-11 Metal + MoltenVK): `api,validation,error_scope` — OOM reported as validation (`type=1`) not out-of-memory, and OOM/internal filtered scopes didn't catch (7 cases). `error_scope` green.

---

## F-064 — yawgpu: WGSL frontend errors immediate-data shader modules — cross-HAL

- **RESOLVED** (yawgpu `f9a076e`; re-verified 2026-06-11 Metal + MoltenVK): `pipeline,immediates:pipeline_creation_immediate_size_mismatch` — WGSL frontend errored on immediate-data shader modules (4 cases). `pipeline/immediates` green.

---

## F-063 — yawgpu: inter-stage interpolation-sampling compatibility mis-validated — cross-HAL

- **RESOLVED** (yawgpu, 2026-06-09): `render_pipeline,inter_stage` — inter-stage interpolation-sampling compatibility both over- and under-validated (12 cases). `inter_stage 26/0` both HALs.

---

## F-062 — yawgpu: render-bundle over-rejects compatible attachment signatures — cross-HAL

- **RESOLVED** (yawgpu, 2026-06-09): `encoding,render_bundle` — over-rejected compatible render-bundle attachment signatures (30 cases). `render_bundle 21/0` both HALs.

---

## F-061 — yawgpu: render-pipeline over-rejects compatible pipeline-layout binding kinds — cross-HAL

- **RESOLVED** (yawgpu, 2026-06-09): `render_pipeline,resource_compatibility` — over-rejected compatible pipeline-layout binding kinds (80 cases). `resource_compatibility 123/0` both HALs.

---

## F-060 — yawgpu: WGSL compiler errors on `texture_external` (external-texture type) — cross-HAL

- **RESOLVED** (yawgpu `fa97027`, 2026-06-09): `render_pipeline,misc:external_texture` — WGSL frontend errored on `texture_external` (2 cases). `external_texture 2/0` both HALs (full external-texture support on Metal; honest operation-level rejection on Vulkan).

---

## F-059 — yawgpu: storage-texture-format support gap in render-pipeline validation + WGSL — cross-HAL

- **RESOLVED** (yawgpu `8b42e5d`-era): `render_pipeline,misc:storage_texture,format` — storage-texture-format support narrower than spec in pipeline-layout validation + WGSL (~366 cases). `storage_texture,format 720/0` both HALs.

---

## F-058 — yawgpu: render-pipeline depth-stencil state over-requires depthCompare + depthWriteEnabled — cross-HAL

- **RESOLVED** (yawgpu `8b42e5d`-era): `render_pipeline,depth_stencil_state` — over-required `depthCompare`/`depthWriteEnabled` for a depth format even when the depth aspect is unused (10 cases). `depth_stencil_state 1600/0` both HALs.

---

## F-057 — yawgpu: WGSL compiler errors on `texture_cube_array<f32>` (float cube-array sampled texture) — cross-HAL

- **RESOLVED** (yawgpu `8b42e5d`-era): `api,validation,non_filterable_texture` — WGSL frontend errored on `texture_cube_array<f32>` (8 float cube-array cases → error module). `non_filterable_texture 160/0` both HALs (Metal + MoltenVK).

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

## F-055 — yawgpu: wrong values sampling a depth/stencil aspect while it is a read-only DS attachment — cross-HAL

- **RESOLVED** (yawgpu `79c4968`-era): `memory_sync/texture/readonly_depth_stencil:sampling_while_testing` — wrong values sampling a depth/stencil aspect while it's a read-only DS attachment (check wrote 0). `1/0` both HALs + native Windows/Vulkan.

---

## F-054 — yawgpu: a render pass with a sparse / null color attachment renders nothing — cross-HAL

- **RESOLVED** (yawgpu `793fc6d`-era): `render_pipeline/pipeline_output_targets:color,attachments` — a render pass with a sparse/null color attachment rendered nothing (non-null slot read back zero). `color,attachments 2/0` both HALs (Metal + Vulkan/MoltenVK).

---

## F-053 — yawgpu: cannot render to multiple color attachments targeting different slices of one 3D texture — cross-HAL

- **RESOLVED** (yawgpu `c29dc78`-era): `rendering/3d_texture_slices:multiple_color_attachments,same_mip_level` — couldn't render to 4 color attachments each bound to a different 3D `depthSlice` (read back zero). Green on Metal + native Windows/Vulkan; residual MoltenVK `VK_ERROR_FEATURE_NOT_PRESENT` (2D-view-on-3D) is a confirmed MoltenVK-only artifact.

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

## F-051 — yawgpu Metal HAL: crash creating a default view of a multisampled texture — Metal-HAL-only

**RESOLVED** (yawgpu `c29dc78`-era, Metal-HAL-only) — hardcoded `MTLTextureType2D` instead of propagating multisample-ness; Dawn/Vulkan always passed.

---

## F-050 — yawgpu: occlusion query returns zero even when samples pass — cross-HAL

**RESOLVED** (yawgpu `37d36e6`+`e70d18d`, cross-HAL) — occlusion query never counted passing samples; Dawn/wgpu-native always passed.

---

## F-049 — yawgpu: render-bundle execution mishandles the viewport rect, bundle draw-args, and repeated/blended replay — cross-HAL

**RESOLVED** (yawgpu `f82c2d6`, cross-HAL) — viewport rect ignored + bundle draw-args/blend mis-applied; Dawn/wgpu-native always passed.

---

## F-048 — yawgpu and wgpu-native: the stencil reference value is not masked to the stencil aspect's bit width

- **RESOLVED for yawgpu** (yawgpu `9bc49dc`): `render_pass/clear_value:stencil_clear_value` — the stencil reference wasn't masked to the 8-bit aspect width before the `equal` compare (6 unmasked-out-of-range cases failed). `stencil_clear_value 30/0` both HALs. **wgpu-native still affected.**

---

## F-047 — yawgpu: pipeline-overridable constants are ignored (read as zero) — cross-HAL

**RESOLVED** (yawgpu `fff8634`, cross-HAL) — WGSL `override` constants ignored in render + compute pipelines; Dawn/wgpu-native always passed.

---

## F-046 — yawgpu: face culling / `front_facing` winding is mishandled — cross-HAL

**RESOLVED** (yawgpu `f82c2d6`+`d6e700a`, cross-HAL) — `@builtin(front_facing)` winding wrong, breaking color + `cullMode`; Dawn/wgpu-native always passed.

---

## F-045 — yawgpu and wgpu-native: `frag_depth` is not clamped to the viewport depth range before the depth test

- **RESOLVED for yawgpu** (yawgpu `155a854`): `rendering/depth_clip_clamp:depth_test_input_clamped` — `frag_depth` not clamped to the viewport depth range `[minDepth,maxDepth]` before the depth test (out-of-range points drew). Green on Metal (`1 skip 1`) + native Vulkan; residual MoltenVK `0/1` is a confirmed MoltenVK-only artifact. **Still open on wgpu-native.**

---

## F-044 — yawgpu: non-`float32` vertex formats decode to zero in the shader — cross-HAL

**RESOLVED** (yawgpu `706087f`, cross-HAL) — vertex-format conversion not applied beyond 32-bit-float passthrough; Dawn/wgpu-native always passed.

---

## F-043 — yawgpu: render-pass `depthSlice` is ignored — always renders to slice 0 of a 3D texture — cross-HAL

**RESOLVED** (yawgpu `c6935f7`, cross-HAL) — `depthSlice` not threaded into the 3D render-target view; Dawn/wgpu-native always passed.

---

## F-042 — yawgpu: a render-stage (fragment) storage-buffer write from a point draw reads back zero — cross-HAL

**RESOLVED** (yawgpu `042902b`+`eadc2f6`, cross-HAL) — render usage scope rejected write+write + render-bundle draws not executed; Dawn/wgpu-native always passed.

---

## F-041 — yawgpu: read-only storage-texture `textureLoad` reads back zero — cross-HAL

**RESOLVED** (yawgpu `2e4edb7`, cross-HAL) — storage-texture bindings not wired to the shader; Dawn/wgpu-native always passed.

---

## F-040 — yawgpu: multisample resolve does not write the resolve target — cross-HAL

**RESOLVED** (yawgpu `bc8c280`+`3303058`, cross-HAL) — no MSAA-resolve support, `resolveTarget` never written; Dawn/wgpu-native always passed.

---

## F-039 — yawgpu: two dispatches in one compute pass lose their writes under batch execution — cross-HAL

**RESOLVED** (yawgpu `89f25df`, cross-HAL) — whole compute pass treated as one usage scope, not per-dispatch; Dawn/wgpu-native always passed.

---

## F-038 — yawgpu mishandles stencil operations, compare, and masks — cross-HAL

**RESOLVED** (yawgpu `40f5d7f`, cross-HAL) — dynamic stencil reference not threaded to the HAL; Dawn/wgpu-native always passed.

---

## F-037 — yawgpu Metal HAL: non-deterministic depth-attachment render/readback race

**RESOLVED** (yawgpu `186cd54`, Metal-only) — missing `[[point_size]]` for point-list pipelines; Dawn/wgpu-native/yawgpu-Vulkan always clean.

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

## F-035 — yawgpu ignores `GPUColorTargetState` `blend` and `writeMask` (writes the raw fragment output) — cross-HAL

**RESOLVED** (yawgpu `74f5ef2`, cross-HAL) — `blend`/`writeMask` ignored, raw fragment output written; Dawn always passed.

---

## F-034 — yawgpu: a fragment storage write is lost on **indexed / indirect** draws

**RESOLVED** (yawgpu `36a6b66`, cross-HAL) — indexed/indirect draw paths not executed; Dawn/wgpu-native always passed.

---

## F-033 — color `copyTextureToTexture` pixel mismatches on Mac via MoltenVK (confirmed MoltenVK artifact, not a yawgpu defect)

**MoltenVK translation artifact** (not a yawgpu defect; native-Vulkan-green + Metal-green) — color `copyTextureToTexture` byte-exact pixel mismatches under MoltenVK only.

---

## F-032 — yawgpu returns zeros for depth/stencil aspect buffer⇄texture copies (except plain Stencil8)

**RESOLVED** (yawgpu `c8f15d5`+`af9ac5c`+`3c847ac`, Metal + Vulkan) — depth/stencil-aspect copies returned zeros; Dawn/wgpu-native always passed.

---

## F-031 — yawgpu diverges on the depth aspect of `copyTextureToTexture` (copied depth fails an equality re-render)

**RESOLVED** (yawgpu `f3afc31`+`cac328a`, Metal + Vulkan) — render path had no depth support; Dawn/wgpu-native always passed.

---

## F-030 — yawgpu `MAP_READ` readback reads the buffer before the GPU copy completes (intermittent zeros)

**RESOLVED** (yawgpu `1297b7e`, Vulkan) — `MAP_READ` raced ahead of the GPU copy (intermittent zeros); read-map now idles the device queue first.

---

## F-029 — yawgpu leaks Vulkan device resources across image_copy cases (later tests in the same process fail)

**RESOLVED** (yawgpu `1e67300`, Vulkan) — in-flight copy resources freed early, exhausting the VkDevice for later tests; retire ring now retains them until the fence signals.

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

## F-026 — yawgpu mishandles non-default buffer layout (and mip levels) in `copyBufferToTexture` / `copyTextureToBuffer`

**RESOLVED** (yawgpu `1e6c70b`, Metal) — non-default buffer layout / non-base mip mishandled; Dawn/wgpu-native always passed.

---

## F-025 — yawgpu `queueWriteTexture` writes zeros to color textures

**RESOLVED** (yawgpu `1e6c70b`, Metal) — `queueWriteTexture` upload path wrote zeros; Dawn/wgpu-native always passed.

---

## F-024 — yawgpu loses data in an rgba8uint texture-copy roundtrip (copyBufferToTexture → copyTextureToBuffer)

**RESOLVED** (yawgpu `c893eac`, Metal) — HAL lacked `rgba8uint`, copy silently a no-op; Dawn/wgpu-native always passed.

---

## F-023 — yawgpu aborts on a 0-size clearBuffer / copyBufferToBuffer (un-ended Metal blit encoder)

**RESOLVED** (yawgpu `e56f30a`, Metal) — 0-size clear/copy aborted via un-ended Metal blit encoder; Dawn always handled it.

---

## F-022 — yawgpu does not defer `minBindingSize` validation (rejects `minBindingSize = 0` at pipeline creation)

**RESOLVED** (yawgpu `798fc6a`) — `minBindingSize=0` rejected at pipeline creation instead of deferred to bind time; Dawn always passed.

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

## F-020 — yawgpu rejects null bind-group-layout slots in createPipelineLayout

**RESOLVED** (yawgpu `f75fc0a`) — null (unused) BGL slots not implemented; Dawn always passed.

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

## F-018 — yawgpu over-restricts BindGroupLayout storage-texture bindings

**RESOLVED** (yawgpu `925520a`) — 1D storage-texture view dim + rgba8snorm base storage over-rejected; Dawn always passed.

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

## F-016 — yawgpu rejects read-write storage textures on read-write-capable formats

**RESOLVED** (yawgpu `4292f76`) — read-write storage rejected on r32 formats; Dawn always passed.

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

## F-014 — yawgpu under-validates 3D-texture view array-layer ranges

**RESOLVED** (yawgpu `baa78cb`) — out-of-range 3D-texture view array-layer ranges accepted; Dawn always passed.

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

## F-011 — yawgpu createView view-dimension gaps (2D-multilayer, cube, cube-array square)

**RESOLVED** (yawgpu `41e007b`) — 2D-multilayer/cube/cube-array-square view-dimension gaps; Dawn/wgpu-native always passed.

---

## F-010 — yawgpu's newly-enabled compressed / feature-gated formats have validation gaps

**RESOLVED** (yawgpu `92db062`) — compressed-format block-alignment / size limits unvalidated; wgpu-native/Dawn always passed.

---

## F-009 — yawgpu over-restricts render-attachment dimension and under-validates storage usage

**RESOLVED** (yawgpu `2667b0a`+`92db062`) — 3D render-attachment over-rejected + tier1 storage gaps; Dawn always passed.

---

## F-008 — yawgpu under-validates transient texture-usage combinations

**RESOLVED** (yawgpu `2667b0a`) — 6 invalid `TransientAttachment` combos accepted; wgpu-native/Dawn always passed.

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

## F-006 — yawgpu disagrees on which texture formats are multisampleable

**RESOLVED** (yawgpu `2667b0a`) — wrong multisampleable-format set (`sampleCount=4`); wgpu-native/Dawn always passed.

---

## F-005 — yawgpu mishandles several valid uncompressed texture formats

**RESOLVED** (yawgpu `2667b0a`+`92db062`) — 12 valid color formats rejected as `Undefined` + D24S8 abort; wgpu-native/Dawn always passed.

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

_Add new findings as `F-00N` with the same fields._
