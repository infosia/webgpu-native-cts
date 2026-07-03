# 01 — Architecture

This document describes the component architecture and how each piece of the upstream
TypeScript CTS maps onto the C++ implementation.

## 1. High-level picture

```
                         ┌─────────────────────────────────────────────┐
                         │                 cts (executable)             │
                         │                                              │
  query string  ──────▶  │  runtime/runner  ──▶ tree ──▶ fixtures ──▶   │
  (CLI arg / file)       │       ▲                          │          │
                         │       │                          ▼          │
                         │   listing                  test functions   │
                         │   (generated)             (.spec.cpp, C++)  │
                         │                                  │          │
                         └──────────────────────────────────┼──────────┘
                                                             │ C API calls
                                                             ▼
                          ┌──────────────────────────────────────────────┐
                          │  backend shim (selects header + entry points) │
                          └──────────────────────────────────────────────┘
                              │             │             │
                    wgpu-native (.a/.so)  yawgpu (.a/.so)  Dawn (.a/.so)
```

The test author writes C++ in `src/webgpu/**/*.spec.cpp`. Each spec file *registers* a test
group and its tests with the harness (at static-init time). The runner takes a query, expands it
against the registry/listing into a tree of concrete cases, and executes each case by
instantiating a fixture and invoking the test function (a lambda). Test functions call the
WebGPU **C** API (directly, plus async→sync helpers) and assert via the fixture.

## 2. Layers

| Layer | Language | Upstream analog | Responsibility |
|-------|----------|-----------------|----------------|
| **Test-author API** (`include/cts/*.h`) | C++20 | `common/framework/*` public surface | The classes/templates test files use: `makeTestGroup`, the test builder, `ParamsBuilder`, the `Fixture`/`GpuTest` interface, assertions |
| **Harness core** (`src/common/`) | C++20 | `common/internal/*`, `common/framework/*` | Registry, parameter expansion, query parse/compare, test tree, runner, logging, result model |
| **GPU fixtures** (`src/webgpu/fixtures/`) | C++20 | `webgpu/gpu_test.ts`, `util/device_pool.ts` | Device pool, `GpuTest` base fixture, validation/error-scope helpers, readback helpers |
| **WebGPU C abstraction** (`src/common/webgpu/`) | C++20 | (n/a — upstream uses `async/await`) | Async→sync future helpers, error-scope sync helpers, backend shim |
| **Tests** (`src/webgpu/**/*.spec.cpp`) | C++20 | `webgpu/**/*.spec.ts` | The ported tests (call the WebGPU **C** API) |
| **Tools** (`tools/gen_listings/`) | C++20 (reuses harness) | `common/tools/{crawl,gen_listings}.ts` | Emit the static listing |

### One language, no ABI seam

Tests and harness are all C++20, so there is **no C ABI boundary** between them. Test files
`#include "cts/test.h"` and use the harness's classes and templates directly. The only C
involved is the WebGPU API itself (`webgpu.h`), which the tests call.

This is what makes the port near-1:1: the upstream framework is OO/fluent/closure-based, and the
C++ realization keeps the same shape (`g.test().desc().params(lambda).fn(lambda)`), including
closures for `expectValidationError([&]{ ... }, shouldError)`. See
[04-authoring-tests](04-authoring-tests.md).

> We deliberately do **not** host the run path on GoogleTest/Catch2/doctest. Their test models
> (compile-time registration, one-param-combo-per-test, framework-specific filters) conflict
> with the CTS case/subcase split and the upstream query-string identity, which we must control.
> Those frameworks are reused only for the harness self-tests and for emitting standard CI
> report formats. See [00-overview](00-overview.md) and [02-harness](02-harness.md).

## 3. Mapping the upstream framework to C++

The upstream framework's essential concepts and their C++ realization:

| Upstream concept | File (upstream) | C++ realization |
|------------------|-----------------|-------------------|
| `makeTestGroup(Fixture)` | `internal/test_group.ts` | `MakeTestGroup<Fixture>(path, desc)` → a `TestGroup<Fixture>`; registered at static-init time |
| `g.test('name').desc().params().fn()` builder | `internal/test_group.ts` | `g.test("name")` returns a fluent `TestBuilder`; `.fn(lambda)` / `.unimplemented()` commits the test into `g` |
| Params builder (`u.combine/expand/filter`) | `framework/params_builder.ts` | `cts::ParamsBuilder` fluent class; `.combine/.expand/.filter` take `std::initializer_list<Value>` / lambdas; `Value` wraps a `std::variant` |
| Case vs subcase (`.params` vs `.paramsSubcasesOnly`) | `framework/params_builder.ts` | `.beginSubcases()` marks the boundary (mirrors `u.beginSubcases()`); `paramsSubcasesOnly` is the single-case shortcut |
| Query classes (MultiFile/MultiTest/MultiCase/SingleCase) | `internal/query/query.ts` | `cts::TestQuery` variants; same string grammar |
| Query parse/compare/stringify | `internal/query/{parseQuery,compare,stringifyParams}.ts` | `cts/query.h` + `query.cpp`; param-value stringification must match upstream byte-for-byte |
| Test tree (`TestTree`, lazy subtree expansion) | `internal/tree.ts` | `cts::TestTree` built from listing + registry |
| `Fixture` base (init/finalize, trackForCleanup, eventual expectations) | `framework/fixture.ts` | `cts::Fixture` C++ base; tests receive `Fixture&`/`GpuTest&` |
| `GPUTest` / `GPUTestBase` | `webgpu/gpu_test.ts` | `cts::GpuTest` fixture; `t.device()`, `t.queue()`, helper methods |
| `DevicePool` / `DeviceProvider` | `webgpu/util/device_pool.ts` | `cts::DevicePool` keyed by feature/limit selection |
| Assertions (`expect`, `shouldThrow`, `expectValidationError`) | `framework/fixture.ts`, `webgpu/gpu_test.ts` | `t.expect(...)`, `t.expectValidationError(lambda, shouldError)` — closures, like upstream |
| `skip()` / `SkipTestCase` control flow | `framework/fixture.ts` | `t.skip(msg)` throws a `SkipTestCase` exception caught at the (sub)case boundary |
| Listing generation (`crawl`, `gen_listings`) | `common/tools/*` | `tools/gen_listings`: links the registry, iterates, emits listing |
| Standalone/cmdline runner | `common/runtime/{standalone,cmdline}.ts` | `src/common/runtime/` → the `cts` executable |
| Logging/results model | `internal/logging/*` | `cts::Logger`, `TestCaseResult` (status: pass/skip/warn/fail) |
| Self-tests | `src/unittests/*` | `src/unittests/` (doctest-based) tests for the harness |

### The one concept with no upstream analog: async→sync

Upstream tests are `async` and `await` promises (`device.createComputePipelineAsync(...)`,
`buffer.mapAsync(...)`, `queue.onSubmittedWorkDone()`, `device.popErrorScope()`). The WebGPU C
API models these as `WGPUFuture` + callbacks driven by `wgpuInstanceWaitAny` /
`wgpuInstanceProcessEvents`. Since the test code is straight-line C++ (no `co_await`), the
harness provides **synchronous wrappers** that block until the future resolves. This is the
single biggest design addition over a literal port and is specified in
[03-webgpu-c-abstraction](03-webgpu-c-abstraction.md).

## 4. Control flow of a run

1. **Parse args**: a query (or list of queries) + options (backend already fixed at link time;
   runtime options like `--list`, `--workers`, `--expectations` — see
   [06 §Common options](06-build-and-run.md) for the complete set).
2. **Load listing**: the generated listing (catalog of files/tests). For an in-process build,
   the registry is also available directly (every linked `.spec.cpp` registered itself at
   static-init time).
3. **Build tree**: expand the query against the listing/registry into a `TestTree`; leaf nodes
   are concrete cases (case-level params bound).
4. **For each selected case**:
   a. Create the fixture (and its shared subcase-batch state).
   b. `init()` — acquire a device from the `DevicePool` matching the test's feature/limit needs
      (or `skip` if unavailable).
   c. Run the test function for the case (which may iterate subcases internally).
   d. `finalize()` — await/drain any eventual expectations, run tracked-resource cleanup,
      release the device back to the pool.
   e. Record a result (pass/skip/warn/fail + messages).
5. **Report**: aggregate counts, emit a human report and/or a machine-readable log.

## 5. Process & threading model

- Single process, primarily single-threaded. The WebGPU C API is driven on the calling thread;
  `wgpuInstanceProcessEvents`/`wgpuInstanceWaitAny` are called from the same thread that issued
  the async op.
- Parallelism (if added later) is at the *case* granularity across worker processes, mirroring
  upstream's worker model — not within a case. The result model is designed to be merge-able so
  sharded runs can be combined. (Deferred; see roadmap.)
- One `WGPUInstance` per process; devices are pooled. Uncaptured-error and device-lost
  callbacks are routed to the active fixture (see abstraction doc).

## 6. Directory layout (detailed)

```
include/cts/
  test.h            # MakeTestGroup, TestGroup, the test builder, assertions
  params.h          # ParamsBuilder + the Value variant
  query.h           # query string types (mostly internal, exposed for tools)
  gpu.h             # GpuTest fixture interface + GPU-specific assertions/helpers
  webgpu.h          # re-exports the canonical webgpu.h via the backend shim

src/common/
  registry.{h,cpp}        # global registry of groups/tests populated at static-init time
  params/                 # ParamsBuilder, value model, expansion
  query/                  # parse, compare, stringify, query variants
  tree.{h,cpp}            # TestTree
  fixture.{h,cpp}         # Fixture base, lifecycle, cleanup tracking, eventual expectations
  logging/                # Logger, result types
  runtime/                # cmdline runner → the cts executable main
  webgpu/                 # async→sync helpers, error-scope helpers, backend shim

src/webgpu/
  fixtures/gpu_test.{h,cpp}   # GpuTest fixture
  util/                       # device_pool, buffer/texture helpers, comparison utils
  api/validation/*.spec.cpp   # ported validation tests
  api/operation/*.spec.cpp    # ported operation tests (later phase)
  ...                         # mirrors upstream src/webgpu/ tree

tools/gen_listings/      # listing emitter (links harness + tests, iterates registry)
third_party/webgpu-headers/   # canonical webgpu.h
```

The `src/webgpu/` subtree intentionally mirrors the upstream `src/webgpu/` paths so that a
ported file lives at the same logical path and produces the same query prefix.

## 7. Open architectural questions (to resolve during the slice)

- **Registration linkage**: how to guarantee every `.spec.cpp` TU's static initializers run.
  Plan: compile `.spec.cpp` files **directly into the `cts` executable** (object files linked
  directly, not via a static archive), so their static initializers are never dropped. If a
  static library is ever needed, fall back to a generated index TU / `--whole-archive`. Detailed
  in [02-harness §1](02-harness.md#1-registration).
- **Param-value stringification parity**: exact rules to match upstream query strings for
  non-trivial values (numbers, booleans, undefined, small objects). Detailed in
  [02-harness §3](02-harness.md#3-query-system).
- **Device-lost / uncaptured-error routing** across the pooled-device lifecycle. Detailed in
  [03-webgpu-c-abstraction](03-webgpu-c-abstraction.md).
