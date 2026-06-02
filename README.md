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
  maximally-ported `buffer/mapping` — and **6 `api/operation` (Phase 4)** files: `command_buffer/`
  `{clearBuffer, copyBufferToBuffer, basic, image_copy}`, `queue/writeBuffer`, and `onSubmittedWorkDone`.
  These add the buffer-readback foundation (`makeBufferWithContents` + `expectGPUBufferValuesEqual`), the
  `writeBuffer`/`writeTexture` upload paths, the texture-copy foundation (`copyBufferToTexture`/
  `copyTextureToBuffer`/`copyTextureToTexture`), and the **TexelView decode-value comparison stack** that
  backs the color `image_copy` port (137256 subcases). See [COVERAGE](docs/COVERAGE.md).

**Conformance outcome.** The suite has surfaced 27 cross-backend findings to date; the full per-finding
record (what, which backend, current status) lives in [FINDINGS](docs/FINDINGS.md). Current state on
real-GPU Metal:

- **yawgpu** — the primary conformance subject — passes **every ported test through T23** (all
  `api,validation` plus the buffer / `writeBuffer` / `basic` / `onSubmittedWorkDone` operation tests). Phase 4
  surfaced two execution findings it has since fixed — **F-023** (0-size clear/copy abort + `clearBuffer`
  zero-fill bug, fixed in `e56f30a`) and **F-024** (an `rgba8uint` texture-copy roundtrip read back zeros —
  its HAL was missing the format; fixed in `c893eac`, which also expanded HAL coverage to all uncompressed
  color formats). The new color `image_copy` port (T24b) then surfaced two **open** ones: **F-025**
  (`queueWriteTexture` writes zeros) and **F-026** (`copyBufferToTexture`/`copyTextureToBuffer` mishandle
  non-default buffer layout + mip levels) — the full `image_copy` run is `pass=21860 fail=115396` pending the
  yawgpu fix (no expectations masked). `api,validation` is unchanged at `pass=4332`. (It also *runs* the
  `immediate_data_size` cases that Dawn/wgpu-native skip.)
- **Dawn** — the oracle — passes everything.
- **wgpu-native** — open findings: eager-panics on invalid input (F-001–F-004, F-007, F-013, F-017,
  F-019, F-021), missing validation (F-012 — `createView` on a destroyed texture; F-015 — the
  view-usage subset rule), and **F-027** (a 3D whole-subresource `image_copy` readback after a
  non-zero-origin copy — `origins_and_extents` 3D `FullCopyT2B`).

Every divergence the suite surfaces is reported, fixed upstream, and re-confirmed on hardware.

### Test results

Over the ported `api,validation` surface — **4715 cases** across 10 files, each case in its own
subprocess (`--isolate`), at the [pinned backend revisions](docs/UPSTREAM.md).

**Real-GPU Metal** (Apple Silicon):

| Backend | pass | skip | fail | crash | |
|---------|-----:|-----:|-----:|------:|--|
| **Dawn** | 4324 | 391 | 0 | 0 | C++ reference implementation — the conformance oracle |
| **yawgpu** | 4332 | 383 | 0 | 0 | primary subject — **zero failures**; runs the 8 `immediate_data_size` cases Dawn skips |
| **wgpu-native** | 3407 | 756 | 338 | 214 | 214 crashes are eager-panics on invalid input (F-001–F-004 incl. F-003 mapping, F-007, F-013, F-017, F-019, F-021); 338 fails are missing-validation / uncaptured-error divergences (F-015 view-usage subset ≈ 324, F-012, F-003 mapping) |

**Real-GPU Vulkan** (Windows 11, NVIDIA GeForce RTX 5060 Ti; `--isolate --expectations`, exit 0;
the 4331-case surface as of T13, before the T14 BGL storage/multisampled tests, 2026-06-01):

| Backend | pass | skip | xfail | xpass | fail | crash |
|---------|-----:|-----:|------:|------:|-----:|------:|
| **yawgpu** | 4131 | 200 | 0 | 0 | 0 | 0 |
| **wgpu-native** | 2260 | 1579 | 274 | 218 | 0 | 0 |

**yawgpu posts the same `pass=4131 skip=200`, zero failures, on both platforms** (and matches Dawn) —
a clean cross-platform result. (Dawn is not yet built on Windows.) The `wgpu-native` row differs from
its Metal numbers because the expectations file (`expectations/wgpu-native.txt`) was tuned on Metal and
this NVIDIA Vulkan driver diverges: 218 cases the file lists as failures (202 `createTexture`, 16
`createView`) actually **pass** here (reported as `xpass`), while 274 expected divergences are still
contained (`xfail`, dominated by 229 `createView` view-usage-subset cases — F-015 — plus the 16
`createBindGroupLayout` storage-texture aborts — F-017); feature-gated formats also skip differently
because each adapter exposes a different optional-feature set. All `wgpu-native` aborts are contained by
`--isolate` and reclassified via the expectations file, so an `--isolate --expectations` run still
exits 0 on both platforms.

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
