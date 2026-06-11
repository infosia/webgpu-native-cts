# webgpu-native-cts

A WebGPU Conformance Test Suite (CTS) written in **C++**, targeting the **WebGPU C API**
(`webgpu.h`) directly — no JavaScript engine, no language bindings.

> The *tests* are C++ that call the WebGPU **C** API. C++ is used because the upstream CTS is
> object-oriented, fluent, and closure-based; a C++ port maps to it almost 1:1, which is the
> whole point of a faithful port.

The goal is to port the upstream TypeScript [WebGPU CTS](https://github.com/gpuweb/cts)
as faithfully as practical, so that native WebGPU implementations can be checked for
conformance by linking a small native test binary instead of embedding a full JS runtime.

## Why

The canonical WebGPU CTS is written in TypeScript and runs in a browser or against native
implementations through a JS engine bound to native code:

- **wgpu** runs the CTS via Deno (`deno_webgpu` ops → `wgpu_core`).
- **Dawn** runs the CTS via Node.js NAPI bindings (`dawn_node` → `dawn::native`).

Both approaches require shipping and maintaining a JavaScript runtime and a binding layer.
This project takes a different path: the tests are written directly against the C API that
implementations such as [wgpu-native](https://github.com/gfx-rs/wgpu-native),
[yawgpu](https://github.com/infosia/yawgpu), and
[Dawn](https://dawn.googlesource.com/dawn) export, so the test binary links straight against
the implementation under test.

## Target implementations

Tests are written against the **canonical** `webgpu.h` from
[webgpu-native/webgpu-headers](https://github.com/webgpu-native/webgpu-headers), which all of the
target backends implement. The suite is **link-agnostic**: a build-time option (`CTS_BACKEND`)
selects which implementation to link against. Three backends are targeted, each playing a distinct
role in the cross-implementation comparison:

- **yawgpu** ([github.com/infosia/yawgpu](https://github.com/infosia/yawgpu)) — a from-scratch
  Rust implementation of `webgpu.h` (Metal/Vulkan backends; vendor extensions in a companion
  `yawgpu.h`). The **primary conformance subject** — the implementation this suite exists to validate.
- **Dawn** — Google's C++ reference implementation. It passes the ported suite, so it serves as the
  **conformance oracle**: the ground-truth behaviour against which any backend disagreement is judged.
- **wgpu-native** — the mature Rust implementation (`wgpu_core`); the backend the harness was first
  brought up against, and a third independent data point that keeps the comparison from being a
  two-way tie.

Implementation-specific differences (native feature enums, instance-creation extras like
yawgpu's `YaWGPUInstanceBackendSelect`) are isolated behind a thin backend shim.

## Language

- The entire suite — tests and harness — is **C++20**.
- Tests call the WebGPU **C** API (`webgpu.h`) directly; they do not depend on any C++ wrapper
  for WebGPU itself.
- The harness is a **custom C++ framework that mirrors the upstream CTS framework 1:1**
  (`MakeTestGroup` / `g.test().desc().params().fn()`, fluent parameter builders —
  `combine`/`combineWithParams`/`filter`/`expand`/`beginSubcases` with per-case subcase expansion —
  lambda test bodies, `expectValidationError([&]{ ... }, shouldError)`, the `suite:file:test:params`
  query system, and a case/subcase tree). It is **not** built on GoogleTest, Catch2, doctest, or
  Criterion — those impose a test model that conflicts with the CTS case/subcase split and
  query-string identity. The harness's own logic (params expansion, query parsing, format tables,
  expectation matching) is checked by a small self-test binary, `cts_unittests`, with no third-party
  test framework.

## Repository layout

```
webgpu-native-cts/
├── README.md  CLAUDE.md  LICENSE  CMakeLists.txt
├── docs/                  # Design docs + UPSTREAM / COVERAGE / FINDINGS
├── specs/                 # Per-slice task specs + reference/ (workflow, templates)
├── include/cts/           # Public C++ test-author API (gpu.h, test.h, webgpu.h)
├── src/
│   ├── common/            # Harness: registry, params, query, runner, runtime, webgpu/ (async→sync, backend shim)
│   ├── webgpu/            # Ported tests (.spec.cpp) + capability_info / texture_format tables + listing.json
│   └── unittests/         # Harness self-tests (cts_unittests)
├── expectations/          # Per-backend known-failure lists ({wgpu-native,yawgpu,dawn}.txt)
└── tools/gen_listings/    # Listing generator → src/webgpu/listing.json
```

The build directories (`build-*/`) are git-ignored. The canonical `webgpu.h` is **not** vendored —
each backend supplies its own `webgpu-headers/webgpu.h` (Dawn its generated header), selected by
`CTS_BACKEND` at configure time.

## Status

**Active — the harness runs and is porting tests across all three backends on real hardware
(Apple Metal, and Windows/Vulkan via wgpu-native and yawgpu).** What works today:

- A complete custom harness: registry, fluent params (`combine`/`combineWithParams`/`filter`/`expand`
  with per-case subcase expansion), the `suite:file:test:params` query system, fixtures, error scopes,
  async→sync helpers, a listing generator, and `cts_unittests` self-tests.
- **Per-case crash isolation** (`--isolate` runs each case in a child process — `fork`+`exec` on
  POSIX, `CreateProcess` on Windows — so a backend that *aborts* becomes a contained `crash` result
  instead of killing the run) and a **per-backend expectations** file (`--expectations`, with `:*`
  test-prefix lines) so a run with known divergences still exits 0.
- All three backends — **wgpu-native, yawgpu, Dawn** — build link-agnostically and run on a real GPU.
  Verified on macOS (Metal) and Windows (MSVC + Vulkan, for wgpu-native and yawgpu).
- Ported so far: **65 `api/validation` files** (56 complete, 9 partial) — the original hand-ported
  vertical slice (`createTexture`, `createView`, `createBindGroupLayout`, `createPipelineLayout`,
  `clearBuffer`, `copyBufferToBuffer`, plus a maximally-ported `buffer/mapping`) **plus 55 files from
  four parallel Workflow bulk-port batches** (`query_set/*`, `texture/*`, `render_pipeline/*` state,
  `encoding/*` + `encoding/cmds/*`, `shader_module/*`, `getBindGroupLayout`, `queue/*`,
  `resource_compatibility`, `render_bundle`, `inter_stage`, `error_scope`, `image_copy/buffer_related`,
  `layout_shader_compat`, … — see [COVERAGE](docs/COVERAGE.md) for the full list) — and **48
  `api/operation`** files: `command_buffer/`
  `{clearBuffer, copyBufferToBuffer, basic, image_copy, copyTextureToTexture, render/render_bundle,
  programmable/state_tracking, queries/occlusionQuery}`, `queue/writeBuffer`,
  `onSubmittedWorkDone`, `rendering/{basic, draw, color_target_state, depth, stencil, depth_bias,
  indirect_draw, 3d_texture_slices, depth_clip_clamp}` (the color render-to-texture + draw-call +
  blend-state + depth-test + stencil-test + depth-bias + indirect-draw + 3D-texture-slice + viewport
  depth-clamp foundations),
  `render_pipeline/{culling_tests, primitive_topology, pipeline_output_targets, overrides,
  vertex_only_render_pipeline}` (the face-culling + primitive-topology + fragment-output + override-constant
  foundations),
  `compute/basic` + `compute_pipeline/overrides` (the compute + override-constant foundations),
  `sampling/{filter_mode, sampler_texture, anisotropy}` (the texture-sampling foundations),
  `texture_view/{format_reinterpretation, texture_component_swizzle}` (the view-reinterpretation +
  component-swizzle foundations; swizzle is 52k subcases, Dawn-only oracle until yawgpu/wgpu-native gain
  the feature),
  `memory_sync/buffer/{single_buffer, multiple_buffers}` (the buffer-synchronization foundations),
  `buffers/{map, map_oom, createBindGroup, threading}` (the buffer-mapping foundation),
  `render_pass/{resolve, storeop2, clear_value, storeOp, transient_attachment}` (the multisample-resolve +
  store-op + stencil-clear-value + storeOp + transient-attachment foundations),
  `storage_texture/{read_only, read_write}` (the storage-texture read + write foundations),
  `resource_init/texture_zero` (the texture zero-init foundation), and
  `vertex_state/{index_format, correctness}` (the indexed-draw index-format + vertex-format-decode
  foundations) — and **11 `shader/execution`** structural files (`stage`, `float_parse`, `override`,
  `value_init`, `shadow`, `limits`, `padding`, `robust_access`, `robust_access_vertex`, `zero_init`,
  `memory_layout` — the WGSL zero-init / robustness / memory-layout / workgroup-memory execution
  foundations) plus the **`flow_control/` tree (10 files + shared harness)**, the **`memory_model/`
  tree (6 files + stress harness)** — control-flow and barrier/atomics/coherence execution coverage —
  and the **`statement/` (5) + `shader_io/` (6) trees** including the `fragment_builtins` /
  `compute_builtins` giants (inter-stage interpolation, sample_mask, subgroup builtins, workgroup_size
  — `shader/validation` and the expression precision tables stay deferred).
  These add the buffer-readback foundation (`makeBufferWithContents` + `expectGPUBufferValuesEqual`), the
  `writeBuffer`/`writeTexture` upload paths, the texture-copy foundation (`copyBufferToTexture`/
  `copyTextureToBuffer`/`copyTextureToTexture`), the **TexelView decode-value comparison stack** that
  backs the color `image_copy` port (137256 subcases), the **render-to-texture path** (depth via
  `copyTextureToTexture:copy_depth_stencil`; color via `rendering/basic`), and the **compute path**
  (`compute/basic` — compute pipeline + `dispatchWorkgroups` + storage readback). See
  [COVERAGE](docs/COVERAGE.md).

**Conformance outcome.** The suite has surfaced 82 cross-backend findings to date; the full per-finding
record (what, which backend, root cause, current status) lives in [FINDINGS](docs/FINDINGS.md). Current
state:

- **yawgpu** — the primary conformance subject — passed the entire ported suite on Vulkan as of the
  2026-06-08 confirmation (Mac via MoltenVK and native Windows / NVIDIA RTX 5060 Ti).
  `expectations/yawgpu.txt` carries no expected failures — nothing is masked. (It also *runs* the
  `immediate_data_size` cases Dawn/wgpu-native skip.) The render-path findings F-051 (multisampled-texture-view
  crash; `sample_mask`), F-054 (sparse / `null` color attachment; `pipeline_output_targets`), F-055 (read-only
  DS attachment + concurrent depth/stencil sampling; `memory_sync/texture`) and F-053 (multi-attachment
  3D-slice render; `3d_texture_slices`) are all fixed; the `api/validation` Workflow
  bulk-port findings **F-057** (`texture_cube_array<f32>` shader error), **F-058** (depth-stencil state
  over-validation), **F-059** (storage-texture-format support gap), **F-060** (WGSL errored on
  `texture_external`; `render_pipeline/misc`), **F-061** (over-rejects compatible pipeline-layout binding
  kinds; `resource_compatibility`), **F-062** (over-rejects compatible render-bundle attachment signatures;
  `render_bundle`), and **F-063** (inter-stage interpolation-sampling mis-validated; `inter_stage`) are now
  **all fixed and re-verified on both HALs**, as are the ten findings surfaced by the batch-4 and Y-1..Y-3
  bulk ports — **F-064–F-067** (validation), **F-069** (workgroup-memory loads), **F-072/F-073**
  (zero-size map ranges, OOM `mappedAtCreation` abort), **F-074** (`queue.writeBuffer` ordering),
  **F-076** (`maxAnisotropy` clamping) and **F-077** (max-bindings shader panic) — all re-verified
  2026-06-11 on Metal + MoltenVK, as is **F-068** (indirect-draw vertex robustness — Metal vertex pulling
  + Vulkan robustBufferAccess; native Windows/Vulkan confirmed green). The three same-day regressions
  from that update (**F-079/F-080/F-081**) were fixed and re-verified the same day — the full
  `api,validation` sweep is green on Metal (`pass=107608 fail=0`). Currently **open** (all
  MoltenVK-surfaced, native-Vulkan confirm pending): **F-083** (`workgroupBarrier` does not order
  non-atomic storage-texture accesses; `memory_model/barrier` — reproducible at 17k–25k disallowed
  observations per run), **F-085** (per-sample interpolation / sample_mask wrong on Vulkan;
  `shader_io/fragment_builtins`, 92 cases), and **F-086** (compound-assignment eval order, discard
  derivatives, IO-struct-in-buffer — 3 single cases). The Y-4b batch (statement + shader_io) is
  otherwise green on Dawn (2929/0), yawgpu Metal and wgpu-native.
  (`texture_external` on the Vulkan backend rejects honestly per the documented `fa97027`
  limitation; see F-081.) The Y-4a batch also surfaced **F-082** (naga-MSL: storage-texture
  intra-invocation coherence — shared with wgpu-native) and **F-084** (wgpu-native: disallowed
  weak-memory behaviors); `flow_control` is green on all four targets. Naga-lineage residuals are
  tracked as **F-078** (`robust_access`: the
  validator treats `let`-propagated indices as const-expression OOB — Tint is correct; yawgpu's earlier
  "green" on this group was a false pass exposed by the F-065 error wiring, so this is **not** a yawgpu
  regression) and **F-070** (Metal: `struct_inner_align` + matCx3 padding + `shadow:loop`; MoltenVK
  additionally ~54 `memory_layout` layout cases pending the SPIR-V-side fix). Four **Mac-only
  MoltenVK
  residuals** remain — **F-033** (color `copyTextureToTexture`), **F-045** (`frag_depth` not
  viewport-clamped), the **F-053** MoltenVK residual (an explicit `vkCreateImageView`
  2D-view-on-3D-image `[mvk-error]`), and the **F-068** MoltenVK residual (125 indirect-draw
  vertex-robustness cases) — all confirmed MoltenVK Vulkan→Metal translation limitations, **absent
  on native Vulkan**, not yawgpu defects; the **GLES** HAL is the only untested follow-up. See
  [FINDINGS](docs/FINDINGS.md) for the per-finding record.
- **Dawn** — the oracle — passes everything.
- **wgpu-native** — open findings: eager-panics on invalid input (F-001–F-004, F-007, F-013, F-017,
  F-019, F-021), missing validation (F-012 — `createView` on a destroyed texture; F-015 — the
  view-usage subset rule), and the wgpu-native 3D multi-slice copy/readback family **F-027** (a 3D
  whole-subresource `image_copy` readback after a non-zero-origin copy) and **F-028** (3D
  `copyTextureToTexture` leaves depth slices z≥1 zero; 2D-array copies pass), plus **F-036** (aborts
  when a constant-factor blend draws without `setBlendConstant`, which should default to `[0,0,0,0]`;
  `color_target_state`, contained via `--isolate` + expectations), **F-045** (`frag_depth` is not
  clamped to the viewport depth range before the depth test — `depth_clip_clamp`; shared with yawgpu, Dawn
  passes), **F-048** (the stencil reference value is not masked to the stencil aspect's bit width —
  `clear_value`; shared with yawgpu, Dawn passes), **F-052** (ignores the pipeline `multisample.mask` —
  `sample_mask`; Dawn passes), and **F-056** (aborts on a **mixed read-only/written** depth-stencil
  attachment that is also sampled — over-strict per-texture usage-conflict validation;
  `memory_sync/texture/readonly_depth_stencil`; Dawn + yawgpu pass).

Every divergence is reported and surfaced — never masked to make a test pass; yawgpu's were fixed and
re-confirmed on hardware, wgpu-native's are contained via `--isolate` + expectations.

### Test results

Per-backend comparison, each case isolated — `--isolate` (one subprocess per case) for
`api,validation`, in-process readback for `api,operation` — at the [pinned
revisions](docs/UPSTREAM.md).

#### `api,validation` — 4715 cases across 10 files

> The full per-backend **isolated** measurement table below covers the original hand-ported
> vertical-slice files. The **40 files added by the three Workflow bulk-port batches** are verified
> separately — every batch is **Dawn-green** (the oracle passes all cases, 0 fail) and **swept on
> yawgpu across both HALs** (Metal + MoltenVK), which is how findings **F-057…F-063** were surfaced.
> They are folded into the full per-backend isolated/expectations table on the next Windows regen.

**Real-GPU Metal** (Apple Silicon):

| Backend | pass | skip | fail | crash | |
|---------|-----:|-----:|-----:|------:|--|
| **Dawn** | 4324 | 391 | 0 | 0 | C++ reference implementation — the conformance oracle |
| **yawgpu** | 4332 | 383 | 0 | 0 | primary subject — **zero failures**; runs the 8 `immediate_data_size` cases Dawn skips |
| **wgpu-native** | 3407 | 756 | 338 | 214 | 214 crashes are eager-panics on invalid input (F-001–F-004 incl. F-003 mapping, F-007, F-013, F-017, F-019, F-021); 338 fails are missing-validation / uncaptured-error divergences (F-015 view-usage subset ≈ 324, F-012, F-003 mapping) |

**Real-GPU Vulkan** (Windows 11, NVIDIA GeForce RTX 5060 Ti) — both backends re-measured **2026-06-08**
via `--isolate --expectations` (the same per-case methodology as the Metal table above):

| Backend | pass | skip | xfail | xpass | fail | crash |
|---------|-----:|-----:|------:|------:|-----:|------:|
| **yawgpu** | 3257 | 1458 | 0 | 0 | 0 | 0 |
| **wgpu-native** | 2507 | 1770 | 438 | 218 | 0 | 0 |

**yawgpu posts `pass=3257 skip=1458`, zero failures** — a clean cross-platform result. The pass/skip
split differs from its Metal row (`4332 / 383`) only because this NVIDIA Vulkan driver feature-gates more
optional texture formats than Apple Metal, so more format cases skip; the zero-failure outcome is
identical on both platforms. (Dawn is not yet built on Windows.)

The per-backend case totals differ (yawgpu 4715, wgpu-native 4933) because each adapter exposes a
different optional-feature set, so the format-swept tests parametrize to a different number of cases. The
`wgpu-native` row differs from its Metal numbers because the expectations file
(`expectations/wgpu-native.txt`) was tuned on Metal and this NVIDIA Vulkan driver diverges: **218** cases
the file lists as failures actually **pass** here (reported as `xpass`), while **438** expected
divergences are still contained (`xfail`, dominated by the `createView` view-usage-subset cases — F-015 —
and the `createBindGroupLayout` storage-texture aborts — F-017). All `wgpu-native` aborts are contained by
`--isolate` and reclassified via the expectations file, so an `--isolate --expectations` run still exits 0
(`fail=0 crash=0`) on both platforms.

#### `api,operation` — Metal (in-process readback)

**Full ported suite (28 files = 10 `api,validation` + 18 `api,operation`), 2026-06-06, real-GPU Metal.**
Run end-to-end, **Dawn (oracle) and yawgpu are green across the whole suite** —
`pass=247751 / 247579 fail=0 crash=0` (per-**subcase** leaf totals; the small pass/skip delta is
feature-gating, not failures). The `api,operation` half now includes the V1–V9 foundations —
`rendering/{basic, draw, color_target_state, depth, stencil}`, `compute/basic`, `sampling/filter_mode`,
`memory_sync/buffer/single_buffer`, `render_pass/{resolve, storeop2}`, and
`storage_texture/{read_only, read_write}` — most of which surfaced a yawgpu finding since fixed and
re-verified (F-034/035/037/038/039/040/041, all resolved; `basic`/`compute`/`sampling`/`storeop2`/
`read_write` were clean first-time). **wgpu-native** is run per-**case**
via `--isolate --expectations` (the only mode that contains its aborts): its `api,operation` half is
`pass=3322 fail=78 xfail=3 crash=0` — the 78 fails are the 3D copy/readback findings **F-027**/**F-028**,
the 3 `xfail` the contained **F-002** (`clearBuffer`) + **F-036** (`color_target_state`) aborts; its
`api,validation` half is the table above. (The Metal Dawn run is per-file: the `AllFeaturesMaxLimitsGpuTest`
fixture requests a fresh all-features device per test and Dawn caps live devices per process —
`--isolate`/per-file resets the count; yawgpu has no such cap, so its combined run is clean.)

The large *structural* ports sweep the format axis densely; on Dawn and yawgpu they are clean, while
wgpu-native surfaces the 3D multi-slice copy/readback findings F-027 and F-028.

The buffer/queue operation tests (`clearBuffer`, `copyBufferToBuffer`, `basic`, `writeBuffer`,
`onSubmittedWorkDone`) pass on Dawn and yawgpu; on wgpu-native `clearBuffer:clear`'s `size=0` subcase
aborts (F-002, contained by `--isolate`). Run in one process, the command_buffer operation files show
**no cross-test interference** — yawgpu is clean across the combined run (`pass=32785 skip=5 fail=0`); no
test poisons another.

**yawgpu on Windows/Vulkan** (NVIDIA RTX 5060 Ti, native — not MoltenVK; 2026-06-08): the operation ports
**match Metal exactly**, including the depth/stencil aspects fixed on the Vulkan HAL (F-031 `cac328a`,
F-032 `3c847ac`; see
[F-032](docs/FINDINGS.md#f-032--yawgpu-returns-zeros-for-depthstencil-aspect-buffertexture-copies-except-plain-stencil8)).
The full ported suite on native Vulkan is green — every ported case passes or skips, **`fail=0
crash=0`**. A fresh end-to-end run (yawgpu `e70d18d`, all 48 file-level queries via `--workers 16`,
`--expectations expectations/yawgpu.txt` with nothing masked) posts
`pass=214039 skip=85698 fail=0 crash=0 xfail=0 xpass=0`, exit 0 in ~2m (a per-**subcase** leaf count;
the same suite is `pass=7208 skip=388` at per-**case** granularity). The only Mac-only artifact (F-033 color
`copyTextureToTexture` under MoltenVK) is a confirmed MoltenVK translation limitation, absent on native
Vulkan.

Design and roadmap live in [`docs/`](docs/) — start with [`docs/00-overview.md`](docs/00-overview.md)
and [`docs/07-roadmap.md`](docs/07-roadmap.md).

## Documentation map

| Document | Contents |
|----------|----------|
| [00-overview](docs/00-overview.md) | Goals, non-goals, scope, key decisions |
| [01-architecture](docs/01-architecture.md) | Component architecture; TS-CTS → C mapping |
| [02-harness](docs/02-harness.md) | Registry, fixtures, params, query, tree, runner, listing |
| [03-webgpu-c-abstraction](docs/03-webgpu-c-abstraction.md) | Async→sync helpers, error scopes, backend shim |
| [04-authoring-tests](docs/04-authoring-tests.md) | Test-author API (C++) and worked example |
| [05-porting-guide](docs/05-porting-guide.md) | How to port a `.spec.ts` to a `.spec.cpp` |
| [06-build-and-run](docs/06-build-and-run.md) | CMake build, backend selection, running, filtering |
| [07-roadmap](docs/07-roadmap.md) | Phased milestones |
| [UPSTREAM](docs/UPSTREAM.md) | Pinned upstream CTS / header / backend revisions |
| [COVERAGE](docs/COVERAGE.md) | Per-area / per-file port status |
| [FINDINGS](docs/FINDINGS.md) | Per-backend conformance defects the suite surfaced |

## License

**BSD-3-Clause** (see [`LICENSE`](LICENSE)), matching the upstream CTS so that ported test logic —
a derivative work — stays license-compatible. Each ported file must preserve upstream attribution;
see [05-porting-guide §0](docs/05-porting-guide.md).
