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
- Ported so far: 10 `api/validation` files, including a fully worked `createTexture` (17 tests) and a
  **complete `createView`** (all 10 tests) backed by the uncompressed + compressed format-capability
  tables, plus the BindGroupLayout binding-entry taxonomy. See [COVERAGE](docs/COVERAGE.md).

**Conformance outcome.** The suite has surfaced 17 cross-backend findings (see [FINDINGS](docs/FINDINGS.md)).
Acting on them, **yawgpu — the primary conformance subject — passes every ported `api,validation` test
on real-GPU Metal** (`pass=4131 skip=200 fail=0 crash=0`, identical to Dawn): all eight of its findings
(F-005/006/008/009/010/011/014/016) were reported here, fixed upstream, and confirmed resolved — most
recently F-016 (read-write storage on the core `r32*` formats), surfaced by the first
`createBindGroupLayout` slice and fixed in `4292f76`. Dawn — the oracle — passes everything.
wgpu-native's findings remain open: eager-panics on invalid input (F-001–F-004, F-007, F-013, F-017) and
missing validation (F-012 — `createView` on a destroyed texture; F-015 — the view-usage subset rule).
This is the suite working as intended: report a divergence → fix upstream → confirm on hardware.

### Test results

Over the ported `api,validation` surface — **4331 cases** across 10 files, each case in its own
subprocess (`--isolate`), at the [pinned backend revisions](docs/UPSTREAM.md).

**Real-GPU Metal** (Apple Silicon):

| Backend | pass | skip | fail | crash | |
|---------|-----:|-----:|-----:|------:|--|
| **Dawn** | 4131 | 200 | 0 | 0 | C++ reference implementation — the conformance oracle |
| **yawgpu** | 4131 | 200 | 0 | 0 | primary subject — **identical to Dawn**; all findings fixed |
| **wgpu-native** | 3378 | 565 | 327 | 61 | 61 crashes are eager-panics on invalid input (F-001–F-004, F-007, F-013, F-017); 327 fails are missing validation (F-015 view-usage subset ≈ 324 cases, F-012) |

**Real-GPU Vulkan** (Windows 11, NVIDIA; `--isolate --expectations`, exit 0; over the 2610-case surface, before the createView tests were added):

| Backend | pass | skip | xfail | fail | crash |
|---------|-----:|-----:|------:|-----:|------:|
| **yawgpu** | 2594 | 16 | 0 | 0 | 0 |
| **wgpu-native** | 1584 | 787 | 239 | 0 | 0 |

(Dawn is not yet built on Windows.) `skip` / `xfail` counts differ across platforms because each
adapter exposes a different optional-feature set (feature-gated formats skip where unsupported) and the
crashing wgpu-native cases differ by driver. The `wgpu-native` crashes are contained by `--isolate`
and listed in `expectations/wgpu-native.txt` (→ `xfail`), so an `--isolate --expectations` run exits 0
on both platforms.

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
