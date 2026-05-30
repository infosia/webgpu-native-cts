# 00 — Overview

## Purpose

`webgpu-native-cts` is a conformance test suite for the **WebGPU C API** (`webgpu.h`),
written in **C++**. It ports the upstream TypeScript [WebGPU CTS](https://github.com/gpuweb/cts)
("the upstream CTS" hereafter) so that native implementations can be validated without a
JavaScript runtime. The tests are C++ that drive the WebGPU **C** API directly.

## Goals

1. **Test the C API directly.** Tests call `wgpuDeviceCreateBuffer`, `wgpuQueueSubmit`, etc.,
   and assert on observable behavior — no IDL layer, no JS engine.
2. **Faithful port.** Test semantics, structure, and *test identity* (query strings) should
   match the upstream CTS as closely as the C API allows, so coverage can be tracked against
   upstream and tests can be ported mechanically.
3. **Backend-agnostic.** A single test binary builds against either **wgpu-native** or
   **Dawn**, selected at build time, by writing only against the canonical `webgpu.h`.
4. **Incremental.** The suite grows test-by-test; partial coverage is useful and the harness
   must run any subset selected by a query string.
5. **Self-checking harness.** The harness has its own unit tests (mirroring upstream
   `src/unittests/`) so we trust the framework before trusting the GPU tests.

## Non-goals (initially)

- **Not** a reimplementation of WebGPU; only tests.
- **Not** running the TypeScript CTS through a binding (that is what wgpu's `cts_runner` and
  Dawn's `dawn_node` already do). This project is a *parallel*, C-native suite.
- **Not** 100% upstream coverage on day one. Shader **execution** tests (which need a large
  expected-value / ULP-comparison infrastructure) are explicitly deferred — see
  [07-roadmap](07-roadmap.md).
- **Not** a browser/web-platform test surface (canvas, `importExternalTexture` from
  `HTMLVideoElement`, workers). Those depend on the web platform and are out of scope for a
  native C suite.

## Key decisions (agreed)

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Target implementation | **Three backends** — wgpu-native, yawgpu, and Dawn — link-agnostic against canonical `webgpu.h`; differences behind a backend shim. Bring up on wgpu-native, then make yawgpu the primary subject, then Dawn | Maximizes value; the C API is shared across all three. yawgpu ([github.com/infosia/yawgpu](https://github.com/infosia/yawgpu)) is the implementation this suite is chiefly built to validate |
| Language | **C++20 throughout** (tests and harness); tests call the WebGPU **C** API | The upstream framework is OO/fluent/closure-based; C++ ports to it almost 1:1, maximizing fidelity. (Originally scoped as C; relaxed to C++ for the test surface.) |
| Harness style | **Custom C++ framework mirroring the upstream framework 1:1**; **not** hosted on GoogleTest/Catch2/doctest/Criterion | The CTS case/subcase split + query-string identity + the params builder are not provided by any general framework and conflict with their models; porting *is* re-implementing that framework. General frameworks are reused only for self-tests and CI output formats |
| Initial scope | **Harness + vertical slice**: build the framework (registry, params, query, fixtures, runner), then port a handful of representative `api/validation` tests end-to-end | Proves the architecture before scaling out |
| Harness fidelity | **Query-compatible**: the query string format (`suite:file:test:params`) and the case/subcase parameter model match upstream exactly, so the same query selects the same tests and listings/CI logic carry over | Tracks coverage against upstream; lets ports be near-mechanical |

## Why "vertical slice" first

The upstream suite is ~683 `.spec.ts` files and ~233k LoC, but ~9.9k LoC of that is the
*framework* (`src/common/`). Almost all of the architectural risk is in the framework:
parameter expansion, the query/tree system, fixture lifecycle, and — unique to a native port — the
**async→sync** problem (the C API is Future/callback based; the upstream tests are
`async/await`). We de-risk by building the framework and proving it against a small number of
real validation tests, before porting tests in bulk.

A vertical slice means: pick ~3–5 representative tests from `api/validation` (e.g.
`createBuffer` validation, `createTexture` validation, a `vertex_state` or `bind_group`
validation case), and make them run, pass/fail correctly, and be selectable by query — through
the full pipeline (registration → listing → tree → run → report).

## Scope of "conformance" here

A test is one of:

- **Validation test**: perform an operation expected to (not) raise a *validation error*, and
  assert via an error scope. No GPU execution result is read back. These are the bulk of
  `api/validation/` and the easiest to port (no readback, deterministic).
- **Operation test**: execute GPU work (copies, compute, render) and read back buffers/textures
  to assert results. Needs the readback + comparison utilities. Comes after the slice.
- **Shader validation test**: compile WGSL expected to (not) produce a creation error.
  Tractable; mid-term.
- **Shader execution test**: run WGSL and compare numeric results with ULP tolerances.
  Large infrastructure; deferred.

See [07-roadmap](07-roadmap.md) for phasing.

## Glossary

- **Backend** — the WebGPU implementation under test (wgpu-native, yawgpu, or Dawn).
- **Fixture** — per-test object providing the device/queue and helpers; upstream `GPUTest`.
- **Case / Subcase** — a parameterized instance of a test; cases are addressable by query,
  subcases are inner iterations sharing one fixture setup. (Mirrors upstream.)
- **Query** — the string that addresses a set of tests, e.g.
  `webgpu:api,validation,createBuffer:size_alignment:*`.
- **Listing** — the static catalog of test files/tests/subcase-counts used to build the tree
  without executing tests.
