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
- Ported so far: 10 `api/validation` files — **6 complete** (`createTexture`, `createView`,
  `createBindGroupLayout`, `createPipelineLayout`, `clearBuffer`, `copyBufferToBuffer`) plus a
  maximally-ported `buffer/mapping` — and **33 `api/operation`** files: `command_buffer/`
  `{clearBuffer, copyBufferToBuffer, basic, image_copy, copyTextureToTexture}`, `queue/writeBuffer`,
  `onSubmittedWorkDone`, `rendering/{basic, draw, color_target_state, depth, stencil, depth_bias,
  indirect_draw, 3d_texture_slices, depth_clip_clamp}` (the color render-to-texture + draw-call +
  blend-state + depth-test + stencil-test + depth-bias + indirect-draw + 3D-texture-slice + viewport
  depth-clamp foundations),
  `render_pipeline/{culling_tests, primitive_topology, pipeline_output_targets, overrides,
  vertex_only_render_pipeline}` (the face-culling + primitive-topology + fragment-output + override-constant
  foundations),
  `compute/basic` (the compute foundation), `sampling/filter_mode` (the texture-sampling foundation),
  `memory_sync/buffer/single_buffer` (the buffer-synchronization foundation),
  `render_pass/{resolve, storeop2, clear_value, storeOp, transient_attachment}` (the multisample-resolve +
  store-op + stencil-clear-value + storeOp + transient-attachment foundations),
  `storage_texture/{read_only, read_write}` (the storage-texture read + write foundations), and
  `vertex_state/{index_format, correctness}` (the indexed-draw index-format + vertex-format-decode
  foundations).
  These add the buffer-readback foundation (`makeBufferWithContents` + `expectGPUBufferValuesEqual`), the
  `writeBuffer`/`writeTexture` upload paths, the texture-copy foundation (`copyBufferToTexture`/
  `copyTextureToBuffer`/`copyTextureToTexture`), the **TexelView decode-value comparison stack** that
  backs the color `image_copy` port (137256 subcases), the **render-to-texture path** (depth via
  `copyTextureToTexture:copy_depth_stencil`; color via `rendering/basic`), and the **compute path**
  (`compute/basic` — compute pipeline + `dispatchWorkgroups` + storage readback). See
  [COVERAGE](docs/COVERAGE.md).

**Conformance outcome.** The suite has surfaced 47 cross-backend findings to date; the full per-finding
record (what, which backend, root cause, current status) lives in [FINDINGS](docs/FINDINGS.md). Current
state:

- **yawgpu** — the primary conformance subject — has **five open findings** (all **cross-HAL**, Metal ==
  Vulkan/MoltenVK): **F-044** — non-`float32` vertex formats decode to **zero** (only `float32x4` works;
  `vertex_state/correctness`); **F-045** — `frag_depth` is not clamped to the viewport depth range before the
  depth test (`rendering/depth_clip_clamp`; **also affects wgpu-native**, Dawn passes); **F-046** — face
  culling / `front_facing` winding is mishandled (`render_pipeline/culling_tests`); **F-047** —
  pipeline-overridable `override` constants are ignored / read as zero (`render_pipeline/overrides`); and
  **F-048** — the stencil reference value is not masked to the stencil aspect's bit width
  (`render_pass/clear_value`; **also affects wgpu-native**, Dawn passes). It **passes the rest of the ported
  suite** on real-GPU Metal **and** Vulkan (Mac via MoltenVK and native Windows / NVIDIA RTX 5060 Ti). Every *other* finding the suite has surfaced against yawgpu was fixed and
  re-confirmed on hardware (e.g. **F-043** — render-pass `depthSlice` ignored — fixed in `c6935f7`);
  `expectations/yawgpu.txt` carries no expected failures — nothing is masked (the open findings' cases stay
  surfaced/failing until fixed). (It also *runs* the `immediate_data_size` cases
  Dawn/wgpu-native skip.) The one Mac-only artifact — **F-033**, color `copyTextureToTexture` under MoltenVK
  — is a confirmed MoltenVK translation limitation, absent on native Vulkan; the **GLES** HAL is the only
  untested follow-up. See [FINDINGS](docs/FINDINGS.md) for the per-finding record (root cause + fix per
  `F-0NN`).
- **Dawn** — the oracle — passes everything.
- **wgpu-native** — open findings: eager-panics on invalid input (F-001–F-004, F-007, F-013, F-017,
  F-019, F-021), missing validation (F-012 — `createView` on a destroyed texture; F-015 — the
  view-usage subset rule), and the wgpu-native 3D multi-slice copy/readback family **F-027** (a 3D
  whole-subresource `image_copy` readback after a non-zero-origin copy) and **F-028** (3D
  `copyTextureToTexture` leaves depth slices z≥1 zero; 2D-array copies pass), plus **F-036** (aborts
  when a constant-factor blend draws without `setBlendConstant`, which should default to `[0,0,0,0]`;
  `color_target_state`, contained via `--isolate` + expectations), **F-045** (`frag_depth` is not
  clamped to the viewport depth range before the depth test — `depth_clip_clamp`; shared with yawgpu, Dawn
  passes), and **F-048** (the stencil reference value is not masked to the stencil aspect's bit width —
  `clear_value`; shared with yawgpu, Dawn passes).

Every divergence is reported and surfaced — never masked to make a test pass; yawgpu's were fixed and
re-confirmed on hardware, wgpu-native's are contained via `--isolate` + expectations.

### Test results

Per-backend comparison, each case isolated — `--isolate` (one subprocess per case) for
`api,validation`, in-process readback for `api,operation` — at the [pinned
revisions](docs/UPSTREAM.md).

#### `api,validation` — 4715 cases across 10 files

**Real-GPU Metal** (Apple Silicon):

| Backend | pass | skip | fail | crash | |
|---------|-----:|-----:|-----:|------:|--|
| **Dawn** | 4324 | 391 | 0 | 0 | C++ reference implementation — the conformance oracle |
| **yawgpu** | 4332 | 383 | 0 | 0 | primary subject — **zero failures**; runs the 8 `immediate_data_size` cases Dawn skips |
| **wgpu-native** | 3407 | 756 | 338 | 214 | 214 crashes are eager-panics on invalid input (F-001–F-004 incl. F-003 mapping, F-007, F-013, F-017, F-019, F-021); 338 fails are missing-validation / uncaptured-error divergences (F-015 view-usage subset ≈ 324, F-012, F-003 mapping) |

**Real-GPU Vulkan** (Windows 11, NVIDIA GeForce RTX 5060 Ti) — a **T13-era `api,validation` snapshot**
(4331 cases, 2026-06-01); yawgpu is clean on the current surface here too, matching Metal:

| Backend | pass | skip | xfail | xpass | fail | crash |
|---------|-----:|-----:|------:|------:|-----:|------:|
| **yawgpu** | 4131 | 200 | 0 | 0 | 0 | 0 |
| **wgpu-native** | 2260 | 1579 | 274 | 218 | 0 | 0 |

**At that snapshot yawgpu posted `pass=4131 skip=200`, zero failures, on both platforms** (matching
Dawn's Metal numbers at that surface) — a clean cross-platform result. (Dawn is not yet built on
Windows.) The `wgpu-native` row differs from
its Metal numbers because the expectations file (`expectations/wgpu-native.txt`) was tuned on Metal and
this NVIDIA Vulkan driver diverges: 218 cases the file lists as failures (202 `createTexture`, 16
`createView`) actually **pass** here (reported as `xpass`), while 274 expected divergences are still
contained (`xfail`, dominated by 229 `createView` view-usage-subset cases — F-015 — plus the 16
`createBindGroupLayout` storage-texture aborts — F-017); feature-gated formats also skip differently
because each adapter exposes a different optional-feature set. All `wgpu-native` aborts are contained by
`--isolate` and reclassified via the expectations file, so an `--isolate --expectations` run still
exits 0 on both platforms.

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

The large *structural* ports, where the format axis is swept densely (cells are `pass / fail`):

| Backend | `image_copy` (137256) | `copyTextureToTexture` color (30910) | `copy_depth_stencil` (216) | `image_copy` depth/stencil (1152) | findings |
|---------|----------------------:|-------------------------------------:|---------------------------:|----------------------------------:|----------|
| **Dawn** (oracle) | 137256 / 0 | 30910 / 0 | 216 / 0 | 1152 / 0 | — |
| **yawgpu** | 137256 / 0 | 30910 / 0 | 216 / 0 | 1152 / 0 | — |
| **wgpu-native** | 116772 / 1332 | 26236 / 738 | 216 / 0 | 1152 / 0 | F-027, F-028 (3D multi-slice copy/readback) |

The buffer/queue operation tests (`clearBuffer`, `copyBufferToBuffer`, `basic`, `writeBuffer`,
`onSubmittedWorkDone`) pass on Dawn and yawgpu; on wgpu-native `clearBuffer:clear`'s `size=0` subcase
aborts (F-002, contained by `--isolate`). Run in one process, the command_buffer operation files show
**no cross-test interference** — yawgpu is clean across the combined run (`pass=32785 skip=5 fail=0`); no
test poisons another.

**yawgpu on Windows/Vulkan** (NVIDIA RTX 5060 Ti, native — not MoltenVK; 2026-06-04): the operation ports
**match Metal exactly** — `image_copy` color `137256 / 0`, `copyTextureToTexture` `copy_depth_stencil`
`216 / 0` (F-031 fixed on the Vulkan HAL, `cac328a`), and `image_copy` depth/stencil `1152 / 0` (F-032
fixed on the Vulkan HAL, `3c847ac`, up from a confirmed native `352 / 800` gap — byte-identical to the
MoltenVK profile, i.e. a real HAL gap, since closed; see
[F-032](docs/FINDINGS.md#f-032--yawgpu-returns-zeros-for-depthstencil-aspect-buffertexture-copies-except-plain-stencil8)).
The full ported suite on native Vulkan is green — all 7596 ported cases pass or skip
(`pass=7208 skip=388 fail=0`, a per-case count). The only Mac-only artifact (F-033 color
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
