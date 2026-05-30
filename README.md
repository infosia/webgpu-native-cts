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
selects which implementation to link against. The three initial targets:

- **wgpu-native** — mature Rust implementation; used as the harness bring-up reference.
- **yawgpu** ([github.com/infosia/yawgpu](https://github.com/infosia/yawgpu)) — a from-scratch
  Rust implementation of `webgpu.h` (Metal/Vulkan backends; vendor extensions in a companion
  `yawgpu.h`); the primary conformance subject this suite is built to validate.
- **Dawn** — the C++ reference implementation; added after the vertical slice.

Implementation-specific differences (native feature enums, instance-creation extras like
yawgpu's `YaWGPUInstanceBackendSelect`) are isolated behind a thin backend shim.

## Language

- The entire suite — tests and harness — is **C++20**.
- Tests call the WebGPU **C** API (`webgpu.h`) directly; they do not depend on any C++ wrapper
  for WebGPU itself.
- The harness is a **custom C++ framework that mirrors the upstream CTS framework 1:1**
  (`makeTestGroup` / `g.test().desc().params().fn()`, fluent parameter builders, lambda test
  bodies, `expectValidationError([&]{ ... }, shouldError)`). It is **not** built on GoogleTest,
  Catch2, doctest, or Criterion — those impose a test model that conflicts with the CTS
  case/subcase split and query-string identity. Existing frameworks are reused only for the
  harness's own self-tests (doctest) and for standard CI output (JUnit/JSON).

## Repository layout (planned)

```
webgpu-native-cts/
├── README.md
├── docs/                     # Design documents (this is where the plan lives)
│   ├── 00-overview.md
│   ├── 01-architecture.md
│   ├── 02-harness.md
│   ├── 03-webgpu-c-abstraction.md
│   ├── 04-authoring-tests.md
│   ├── 05-porting-guide.md
│   ├── 06-build-and-run.md
│   └── 07-roadmap.md
├── include/cts/              # Public C++ test-author API (headers)
├── src/
│   ├── common/               # Harness: registry, params, query, tree, runner (C++)
│   └── webgpu/               # Ported tests (.spec.cpp), mirrors upstream src/webgpu/ layout
├── third_party/
│   └── webgpu-headers/       # Canonical webgpu.h (vendored or submodule)
├── tools/
│   └── gen_listings/         # Listing generator (mirrors upstream gen_listings)
└── CMakeLists.txt
```

## Status

**Planning / documentation phase.** No implementation has started yet. The design is captured
in [`docs/`](docs/). Start with [`docs/00-overview.md`](docs/00-overview.md).

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
