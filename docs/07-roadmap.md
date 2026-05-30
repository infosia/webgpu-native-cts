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
- Registration via direct compilation of `.spec.cpp` into the executable (no generated index; the
  generated-index TU is a later fallback only if a static-library split is ever needed).

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
- Async→sync: `pumpUntil` (ProcessEvents polling — the single wait path; `wgpuInstanceWaitAny` is
  sidestepped because wgpu-native's panics); `requestAdapterSync`, `requestDeviceSync`, error-scope
  sync helpers; uncaptured-error routing to current fixture.
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

## Phase 2 — yawgpu (primary subject) + hardening

**Goal:** the slice runs against **yawgpu**, the implementation this suite chiefly validates; the
abstraction's rough edges are settled.

- yawgpu backend shim (`backend_yawgpu.cpp`); `CTS_BACKEND=yawgpu` builds and runs the Phase 1
  tests; `--yawgpu-backend metal|vulkan` selects the GPU backend.
- Resolve WaitAny/timeout differences vs the wgpu-native bring-up; exercise the ProcessEvents
  fallback on both.
- Skip-vs-fail policy validated where backends differ (optional features/limits); since yawgpu is
  young, expect more skips/fails — capture them in a yawgpu `--expectations` file so regressions
  are visible without failing the run.
- `--expectations` file support; first per-backend known-failure lists.
- `--format json` and merge-able results.

**Exit:** Phase 1 tests run on yawgpu with results triaged (pass / explicit skip / tracked
expected-fail); CI runs wgpu-native + yawgpu.

---

## Phase 2b — Dawn — **done**

**Goal:** the slice also runs against **Dawn** (the C++ reference), completing the three-backend
matrix.

- Dawn backend shim (`backend_dawn.cpp`); `CTS_BACKEND=dawn` links the monolithic static
  `libwebgpu_dawn.a`; backend-aware `cts/webgpu.h` include (`<webgpu/webgpu.h>`).
- `expectations/dawn.txt` (empty — Dawn passes all ported files).

**Result:** all ported files pass on Dawn (Metal) under `--isolate`, 0 crashes. The 3-way run shows
**wgpu-native is the only backend that aborts** (F-001/F-002); **yawgpu and Dawn handle the same
inputs gracefully**. This differential is the suite's unique value vs yawgpu's own single-backend
Rust CTS.

**Follow-up (async cleanup) — done:** the dead `backendSupportsTimeoutWaitAny()` flag and the
placeholder `waitFuture` were removed; `pumpUntil` (ProcessEvents polling) is the single wait path
on all three backends. [docs/03 §2](03-webgpu-c-abstraction.md) now matches the code.

---

## Phase 3 — Broaden `api/validation`

**Goal:** port the bulk of `api/validation/` (the most tractable, readback-free area).

- Fill in params features as needed: `expand`, `filter`, `combineWithParams`, `unless`,
  batching.
- Add common descriptor helpers as repetition reveals them.
- Establish `docs/COVERAGE.md` and the coverage-diff tool against the pinned upstream revision.
- Port files area-by-area (buffers, textures, encoding, render pipeline, compute pipeline, bind
  groups, queue), tracking N/A tests (WebIDL TypeError cases) explicitly.

**Exit:** a substantial, tracked fraction of `api/validation` ported and green on all three backends.

---

## Phase 4 — `api/operation` (readback)

**Goal:** support tests that execute GPU work and verify results.

- Readback + comparison utilities: `expectBufferValuesEqual`,
  `expectBufferValuesPassCheck`, typed buffer reads (mirror `readGPUBufferRangeTyped`).
- Texel data + texture layout helpers (mirror `util/texture/*`, `util/texel_data.ts`).
- Port representative operation tests (buffer copies, `writeBuffer`, simple compute, simple
  render-to-texture + readback).

**Exit:** operation tests with deterministic results pass on all three backends.

---

## Phase 5 — Shader **validation**

**Goal:** WGSL that should/shouldn't compile.

- `createShaderModule` + compilation-info handling; expect creation error vs success.
- Port a slice of `shader/validation/` (parsing/type-checking cases that don't need execution).

**Exit:** a slice of shader validation green on all three backends.

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
- **Findings**: `docs/FINDINGS.md` records per-backend conformance defects the suite surfaces.
- **Crash isolation** — **done** (Phase 4): `--isolate` runs each case in a child process
  (fork+exec `--run-case`), so a backend that *aborts* (panics across the FFI — see
  [FINDINGS F-001/F-002](FINDINGS.md)) yields a contained `crash` status instead of killing the run.
  `--expectations` covers crashes (→ `xfail`); `expectations/wgpu-native.txt` lists the known
  crashers so a triaged wgpu-native run exits 0. Children run sequentially for now; concurrent
  children (parallelism) and a longer-lived worker/chunk model are the next steps here.
- **CI**: backend × OS matrix; listing-freshness check; expectations files.
- **Performance**: case-level parallelism (worker processes) once the suite is large enough to
  need it; the result model is already designed to merge shards.

---

## What we are explicitly NOT doing

- Web-platform tests (canvas, video `importExternalTexture`, workers) — no native analog.
- WebIDL/TypeError coercion tests — no native analog (marked N/A per file).
- Running the TypeScript CTS via a binding — that already exists (wgpu `cts_runner`, Dawn
  `dawn_node`); this project is the parallel C-native suite.
