# 07 — Roadmap

Phased plan from an empty repo to broad coverage. Each phase is independently useful and ends in
a runnable state. Phase 1 (the vertical slice) is the agreed first step; later phases are a
sketch to be re-planned as we learn.

Phases are sequenced by **architectural risk**, not by test count. The framework and the
async→sync layer carry almost all the risk, so they come first, proven against a few real tests.

---

## Phase 0 — Repo & build skeleton

**Goal:** an empty suite that builds, links one backend, and runs zero tests.

- CMake skeleton: `cts`, `cts_harness`, `cts_unittests`, `gen_listings` targets.
- Vendor canonical `webgpu.h` (`third_party/webgpu-headers/`).
- Backend shim for **wgpu-native** only; `cts::createInstance` + adapter request works; print the
  adapter name and exit.
- Generated-index codegen wired (empty index).

**Exit:** `build/cts --version` and adapter enumeration work against wgpu-native.

---

## Phase 1 — Vertical slice (agreed first deliverable)

**Goal:** the full pipeline working end-to-end for a handful of `api/validation` tests.

Harness pieces (minimum viable):

- Registration: `MakeTestGroup<F>` + `CTS_TEST`; `.spec.cpp` compiled directly into the executable.
- Params: `combine` + `beginSubcases` + `Value` model (ints/strings/bools) + cartesian
  expansion. (`expand`/`filter`/object-combine can follow.)
- Query: parse + stringify + compare; **param-stringification parity** unit-tested.
- Tree: build from registry; `--list` / `--list-cases`.
- Fixture: `Fixture` + `GpuTest` with pooled device; subcase iteration; cleanup tracking;
  per-subcase failure isolation (C++ exception boundary around each subcase).
- Async→sync: `waitFuture`, `requestAdapterSync`, `requestDeviceSync`, error-scope sync helpers;
  uncaptured-error routing to current fixture.
- Assertions: `t.expect`, `t.fail`, `t.skip`, `t.expectValidationError(lambda, shouldError)`.
- Runner: query → tree → run → text report; non-zero exit on failure.

Tests to port (representative, ~3–5 files):

- `api/validation/createBuffer.spec` (size/usage validation; the worked example).
- `api/validation/createTexture.spec` (a subset: dimension/format/usage validation).
- one `api/validation/encoding/...` or `vertex_state`/`bind_group_layout` validation file with
  a moderately interesting params builder.

`cts_unittests` covering params/query/tree.

**Exit criteria (definition of done for the slice):**

1. `build/cts --list 'webgpu:api,validation,createBuffer:*'` matches the upstream case count for
   the ported tests.
2. Each ported case runs and reports pass/fail/skip correctly against wgpu-native.
3. A deliberately-broken assertion fails the case (and the run) as expected.
4. `cts_unittests` passes, including query-stringification parity.
5. Query selection works at all four granularities (file/test/case/single-case).

This validates: registration linkage, params identity, the async→sync layer, error-scope
assertions, the fixture lifecycle, and the runner — i.e. all the load-bearing design.

---

## Phase 2 — Second backend + hardening

**Goal:** the same slice runs against **Dawn**; the abstraction's rough edges are settled.

- Dawn backend shim; `CTS_BACKEND=dawn` builds and runs the Phase 1 tests.
- Resolve WaitAny/timeout differences; exercise the ProcessEvents fallback on both backends.
- Skip-vs-fail policy validated where backends differ (optional features/limits).
- `--expectations` file support; first per-backend known-failure lists.
- `--format json` and merge-able results.

**Exit:** Phase 1 tests green (or explicitly skipped) on both backends; CI matrix runs them.

---

## Phase 3 — Broaden `api/validation`

**Goal:** port the bulk of `api/validation/` (the most tractable, readback-free area).

- Fill in params features as needed: `expand`, `filter`, `combineWithParams`, `unless`,
  batching.
- Add common descriptor helpers as repetition reveals them.
- Establish `docs/COVERAGE.md` and the coverage-diff tool against the pinned upstream revision.
- Port files area-by-area (buffers, textures, encoding, render pipeline, compute pipeline, bind
  groups, queue), tracking N/A tests (WebIDL TypeError cases) explicitly.

**Exit:** a substantial, tracked fraction of `api/validation` ported and green on both backends.

---

## Phase 4 — `api/operation` (readback)

**Goal:** support tests that execute GPU work and verify results.

- Readback + comparison utilities: `expectBufferValuesEqual`,
  `expectBufferValuesPassCheck`, typed buffer reads (mirror `readGPUBufferRangeTyped`).
- Texel data + texture layout helpers (mirror `util/texture/*`, `util/texel_data.ts`).
- Port representative operation tests (buffer copies, `writeBuffer`, simple compute, simple
  render-to-texture + readback).

**Exit:** operation tests with deterministic results pass on both backends.

---

## Phase 5 — Shader **validation**

**Goal:** WGSL that should/shouldn't compile.

- `createShaderModule` + compilation-info handling; expect creation error vs success.
- Port a slice of `shader/validation/` (parsing/type-checking cases that don't need execution).

**Exit:** a slice of shader validation green on both backends.

---

## Phase 6 — Shader **execution** (deferred / largest)

**Goal:** run WGSL and compare numeric outputs with tolerances.

This is the largest infrastructure investment and is explicitly deferred:

- Expected-value computation in C++ (float/int/vector/matrix), ULP-based comparison, special
  values (NaN/Inf/denormals), and the generators that upstream uses for builtin-function tests.
- A scalar/vector "harness shader" pattern to feed inputs and read outputs.

Re-planned when Phases 1–5 are solid. May remain partial indefinitely (it is enormous upstream).

---

## Cross-cutting, ongoing

- **Upstream tracking**: pin a CTS revision in `docs/UPSTREAM.md`; periodically re-baseline.
- **Coverage reporting**: `docs/COVERAGE.md` + a diff tool vs the upstream listing.
- **CI**: backend × OS matrix; listing-freshness check; expectations files.
- **Performance**: case-level parallelism (worker processes) once the suite is large enough to
  need it; the result model is already designed to merge shards.

---

## What we are explicitly NOT doing

- Web-platform tests (canvas, video `importExternalTexture`, workers) — no native analog.
- WebIDL/TypeError coercion tests — no native analog (marked N/A per file).
- Running the TypeScript CTS via a binding — that already exists (wgpu `cts_runner`, Dawn
  `dawn_node`); this project is the parallel C-native suite.
