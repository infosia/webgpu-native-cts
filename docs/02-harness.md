# 02 — Harness Design

This is the core design document. It specifies the test harness: how tests register, how
parameters are built and expanded, how queries address tests, how the test tree is built, the
fixture lifecycle, the runner loop, and listing generation.

The harness mirrors the upstream `src/common/` framework as a **custom C++ framework**. Upstream
is TypeScript classes + closures; the C++ realization keeps the same shape (fluent builders,
lambdas) so ported tests read almost identically. The **query string grammar and parameter
model are kept compatible** with upstream so the same query selects the same tests.

We deliberately do **not** host this on GoogleTest/Catch2/doctest — see
[00-overview](00-overview.md) for why. Those are reused only for the harness self-tests (§9) and
CI output formats.

---

## 1. Registration

### Problem

Upstream discovers tests by importing `.spec.ts` modules; each exports a `TestGroup` object
`g`. A compiled binary has no dynamic import. We need every `.spec.cpp` translation unit to
contribute its tests to a global registry.

### Design

Each spec file declares one group and N tests:

```cpp
#include "cts/test.h"
#include "cts/gpu.h"
using namespace cts;

namespace {
// Binds this file to its query path + description, and registers the group.
TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,createBuffer",                 // query "file" path
    "Validation tests for GPUDevice.createBuffer."); // file description

// One test. `.fn(...)` (or `.unimplemented()`) commits it into `g`.
CTS_TEST(g, "size_alignment")
    .desc("Buffer size must be a multiple of 4 when a MAP_* usage is set.")
    .params([](ParamsBuilder u) {                  // see §2
        return u.combine("usage", {BufferUsage::MapRead, BufferUsage::MapWrite})
                .beginSubcases()
                .combine("size", {0, 2, 4, 6, 8});
    })
    .fn([](GpuTest& t) {                            // see §5/§6
        auto usage = t.param<WGPUBufferUsage>("usage");
        auto size  = t.param<uint64_t>("size");
        t.expectValidationError([&] {
            t.createBufferTracked({ .size = size, .usage = usage });
        }, /*shouldError=*/ size % 4 != 0);
    });
} // namespace
```

How registration actually fires:

- `MakeTestGroup<F>(path, desc)` constructs a `TestGroup<F>` whose constructor **registers
  itself** into the global `Registry` under `path`. Because `g` is a namespace-scope object, its
  constructor runs at static-init time.
- `CTS_TEST(g, "name")` expands to a uniquely-named `static const` registrar initialized from
  `g.test("name")`. The fluent chain ends at `.fn(lambda)` / `.unimplemented()`, whose side
  effect appends a `TestSpec` to `g`. (Using a `static const` initializer means the chain is a
  valid namespace-scope declaration — no need for a wrapping function.)

### Linkage: make sure every TU's initializers run

Static initializers in a TU pulled from a **static archive** can be dropped by the linker if
nothing references the TU. To avoid this entirely:

- **Compile all `src/webgpu/**/*.spec.cpp` directly into the `cts` executable** (object files
  linked into the binary, not bundled into an intermediate static library). Object files linked
  directly always contribute their static initializers.

If we later need the tests as a static library (e.g. for multiple executables), fall back to one
of: a **generated index TU** that references each file's group symbol, `--whole-archive` /
`/WHOLEARCHIVE`, or `+load`-style anchors. The generated-index approach is the portable fallback
(MSVC included) and is the same scan that feeds listing generation (§8). For now, direct
compilation keeps it simple.

### Registry data model

```cpp
struct TestSpec {
    std::string                          name;          // "size_alignment"
    std::string                          desc;
    std::function<void(Fixture&)>        fn;            // null if .unimplemented()
    std::function<ParamsBuilder(ParamsBuilder)> params; // null if no params
    const FixtureVTable*                 fixture;        // from TestGroup<F>
};

struct SpecFile {
    std::string           queryPath;   // "api,validation,createBuffer"
    std::string           desc;
    std::vector<TestSpec> tests;
};

class Registry {              // global singleton
    std::vector<SpecFile> files_;
  public:
    static Registry& get();
    SpecFile& addFile(std::string path, std::string desc);
    // iteration for runner / listing / tools ...
};
```

The registry is the single source of truth at runtime; the listing (§8) is a cached projection
for tools that must not link the GPU code.

---

## 2. Parameters

Mirrors `framework/params_builder.ts`. Two scopes:

- **Case params** — part of the test's identity; appear in the query string; each combination is
  a separately-addressable case.
- **Subcase params** — inner iterations within a single case that share one fixture `init()`;
  not in the query; failures reported per-subcase but the case is the addressable unit.

### Value model

A parameter value is a `std::variant` (`cts::Value`) covering exactly what upstream query
strings can encode:

```cpp
struct Undefined {};                         // upstream `undefined`
struct Null {};                              // upstream `null`
using Value = std::variant<
    Undefined, Null, bool, int64_t, double, std::string,
    std::vector<Value>,                      // arrays
    std::map<std::string, Value>>;           // small objects
```

`Value` has implicit constructors from `bool`/integer/`double`/`const char*` so authors write
`{0, 2, 4}` and `{"rgba8unorm", "rgba16float"}` naturally.

> **Parity requirement**: `stringifyValue` must match upstream
> `internal/query/stringify_params.ts` (and `json_param_value.ts`) byte-for-byte for the value
> kinds we support, because these strings appear in query identities. Numbers, `NaN`,
> `Infinity`, `-0`, booleans, `null`, `undefined`, and small JSON objects each have specific
> encodings upstream; we replicate them and cover them with unit tests
> (`src/unittests/query_test.cpp`). Param values that upstream cannot stringify are rejected.

### Builder API (fluent C++)

```cpp
ParamsBuilder ParamsBuilder::combine(std::string key, std::initializer_list<Value> values);
ParamsBuilder ParamsBuilder::combineWithParams(std::vector<ParamRecord> records);
ParamsBuilder ParamsBuilder::expand(std::string key,
        std::function<std::vector<Value>(const ParamRecord&)> fn);
ParamsBuilder ParamsBuilder::filter(std::function<bool(const ParamRecord&)> pred);
ParamsBuilder ParamsBuilder::unless(std::function<bool(const ParamRecord&)> pred);
ParamsBuilder ParamsBuilder::beginSubcases();   // case → subcase boundary
```

Each returns the builder (by value/ref) so calls chain, mirroring upstream:

```ts
u.combine('usage', [MAP_READ, MAP_WRITE])
 .combine('size', [0, 4, 6, 16])
 .expand('aligned', p => [...])
 .filter(p => ...)
 .beginSubcases()
 .combine('offset', [0, 4, 8])
```
```cpp
u.combine("usage", {MapRead, MapWrite})
 .combine("size", {0, 4, 6, 16})
 .expand("aligned", [](const ParamRecord& p){ return std::vector<Value>{...}; })
 .filter([](const ParamRecord& p){ return ...; })
 .beginSubcases()
 .combine("offset", {0, 4, 8})
```

The builder records operations and expands them lazily into the cartesian product (cases ×
subcases), exactly like upstream. `expand`/`filter` receive the partial record.

### Reading params

```cpp
template <class T> T   Fixture::param(std::string_view key) const;  // typed read
bool                   Fixture::hasParam(std::string_view key) const; // `undefined` handling
const ParamRecord&     Fixture::params() const;
```

`param<WGPUBufferUsage>("usage")` reads the current case+subcase record (subcase shadows case on
key collision, matching upstream merge order) and converts the stored `Value` to `T`.

---

## 3. Query system

Mirrors `internal/query/`. The grammar (unchanged from upstream):

```
suite ':' fileFragment ':' testFragment ':' paramsFragment
```

Four query kinds, by specificity:

| Kind | Example | Selects |
|------|---------|---------|
| MultiFile | `webgpu:api,validation,*` | all files under a path |
| MultiTest | `webgpu:api,validation,createBuffer:*` | all tests in a file |
| MultiCase | `webgpu:api,validation,createBuffer:size_alignment:*` | all cases of a test |
| SingleCase | `webgpu:api,validation,createBuffer:size_alignment:usage=2` | one case |

- File path segments are comma-separated (`api,validation,createBuffer`) and map to
  `api/validation/createBuffer.spec.cpp` (the `MakeTestGroup` path argument).
- Test path may itself contain `,` for nested test names.
- Params fragment is `key=value;key=value`, ordered to match the params builder order, using the
  stringified values from §2.
- `*` is the wildcard tail.

Components (all pure, no GPU; unit-tested in `src/unittests/query_test.cpp`):

- `cts/query.h` + `query/parse.cpp` — parse a string into a `cts::TestQuery`.
- `query/compare.cpp` — ordering/containment (`compareQueries`, "does query A contain B").
- `query/stringify.cpp` — the inverse, including param stringification (§2 parity).

---

## 4. Test tree

Mirrors `internal/tree.ts`. Given a query and the listing/registry, build a `cts::TestTree`:

- Internal nodes correspond to file path prefixes, files, and tests.
- Leaf nodes are **cases** (case-params bound). Subcases live inside a leaf and are expanded at
  run time, not as tree nodes (matching upstream).
- Operations: iterate leaves under a query, count subcases, render a human-readable outline
  (`--list`, `--list-cases`).

Subtree expansion can be lazy (upstream supports chunked expansion for the web UI); for the CLI
runner we expand eagerly. Laziness is an optimization deferred until needed.

---

## 5. Fixtures and lifecycle

Mirrors `framework/fixture.ts` (base) and `webgpu/gpu_test.ts` (`GPUTest`). These are plain C++
classes; the test lambda receives a reference (`Fixture&` / `GpuTest&`).

### Base fixture (`cts::Fixture`)

```cpp
class Fixture {
  public:
    virtual void init() {}        // setup (override)
    virtual void finalize() {}    // teardown (override)

    template <class T> T param(std::string_view key) const;
    bool hasParam(std::string_view key) const;

    template <class H> H trackForCleanup(H handle, void(*release)(H));  // auto-release at finalize
    void eventually(std::function<void()> check);  // async expectation, run at finalize

    // assertions (see §6)
    void expect(bool cond, std::string msg = {});
    [[noreturn]] void skip(std::string reason);
    void fail(std::string msg);
    void warn(std::string msg);
    // ...
  private:
    ParamRecord params_;
    SubcaseBatchState* state_;
    // cleanup list, eventual list, result sink, source-location of current assertion ...
};
```

### Subcase batch state

Mirrors upstream `SubcaseBatchState`: state shared across all subcases of one case (e.g. the
acquired device). Lifecycle per case:

```
SubcaseBatchState state;
state.init();                         // e.g. acquire device provider
for (each subcase params) {
    Fixture fx(state, subcaseParams);
    fx.init();
    runTestLambda(fx);                // the test body, once per subcase
    fx.finalize();                    // drain eventual checks, run cleanup
}
state.finalize();                     // release device
```

A subcase that throws `SkipTestCase`/`TestFailed` (from `skip`/`fail`) is caught here so the
remaining subcases still run; the failure is recorded against that subcase.

### How a test sees subcases

The runner invokes the test lambda **once per subcase**; the body just reads params and asserts.
There is no explicit subcase loop in author code (matching upstream, which re-runs `fn` per
subcase).

```cpp
.fn([](GpuTest& t) {
    auto size   = t.param<uint64_t>("size");    // case param
    auto offset = t.param<uint64_t>("offset");  // subcase param
    // ... one subcase's worth of work ...
});
```

### GPU fixture (`cts::GpuTest`)

Mirrors `GPUTestBase`. Methods (see [04-authoring-tests](04-authoring-tests.md) for the full
list):

- `WGPUDevice device()`, `WGPUQueue queue()`, `WGPUInstance instance()`, `WGPUAdapter adapter()`.
- Device selection / skip: `selectDeviceOrSkip(...)`, `skipIfFeatureUnsupported(feature)`,
  `skipIf(cond, reason)` — mirror `selectDeviceOrSkipTestCase` and the `skipIf*` family.
- Tracked creators: `createBufferTracked(desc)`, `createTextureTracked(desc)`.
- Readback + comparison (operation tests): `expectBufferValuesEqual(...)` (later phase).
- Error-scope assertions: `expectValidationError(lambda, shouldError)` (see
  [03-webgpu-c-abstraction](03-webgpu-c-abstraction.md)).

Device acquisition goes through a `DevicePool` (mirrors `util/device_pool.ts`) keyed by the
requested feature/limit set, so most cases reuse one device. Uncaptured errors on a pooled device
fail the *current* case (routing in the abstraction doc).

---

## 6. Assertions / expectations

Mirror `framework/fixture.ts` and `gpu_test.ts`. Methods on the fixture; source location is
captured with `std::source_location` (C++20) or a `CTS_EXPECT(...)` macro fallback (C++17).

```cpp
t.expect(cond, "msg");                 // boolean assertion
t.expectEq(a, b);                      // values shown in the message
t.fail("message");
t.warn("message");
t.skip("reason");                      // throws SkipTestCase

// GPU validation — closures, exactly like upstream:
t.expectValidationError([&] {
    /* operations expected to (not) raise a validation error */
}, /*shouldError=*/ true);

// OOM / internal-error scopes:
t.expectOutOfMemoryError([&]{ ... }, shouldError);

// async / eventual (use the sync wrappers from doc 03 and assert on status):
t.expectAsyncStatus(actualStatus, expectedStatus, "what");
```

`skip`/`fail` throw exceptions (`SkipTestCase` / `TestFailed`) caught at the subcase boundary —
the clean C++ analog of upstream throwing `SkipTestCase`. (No `setjmp` needed; C++ is allowed
throughout.) `expectValidationError` wraps `wgpuDevicePushErrorScope` /
`wgpuDevicePopErrorScope` synchronously around the lambda (see abstraction doc §error scopes);
nested scopes are supported (a stack), mirroring nested `expectValidationError` upstream.

---

## 7. Runner

Mirrors `common/runtime/cmdline.ts`. The `cts` executable:

```
cts [options] <query> [<query> ...]
cts --list <query>            # print matching test/case paths, do not run
cts --list-cases <query>      # include case params
cts --gen-listing <out>       # (tool mode) emit listing for the linked tests
```

Loop:

1. Touch the registry (static init already populated it).
2. Parse queries; build a combined `cts::TestTree`.
3. For each leaf case: construct the fixture + subcase-batch state, run subcases, collect a
   `CaseResult`.
4. Aggregate and print: per-case status and a summary (`pass/skip/warn/fail`, timings).
5. Exit non-zero if any case failed (skips/warns do not fail the run by default; configurable).

Options to support early: backend is fixed at link time, but the runner exposes
`--adapter-name`, `--power-preference`, `--force-fallback-adapter`, `--enable-feature`,
`--verbose`, `--quiet`, and `--expectations <file>` for known-failures (so CI can track
expected-fail lists like upstream).

### Result / logging model

Mirrors `internal/logging/`. A case result has a status and a list of log entries
(info/warn/error with source location and message). Output formats: human text (default) and a
machine format (JSON / JUnit XML) for CI aggregation and sharding merges (reusing standard
formats rather than inventing one).

---

## 8. Listing generation

Mirrors `common/tools/{crawl,gen_listings}.ts`. Upstream crawls `.spec.ts` files, imports each,
and records `(file, testName, subcaseCount)` into `listing.js`.

Our equivalent (`tools/gen_listings`, or `cts --gen-listing`):

1. Link the harness **and** all `.spec.cpp` files (the same direct-compilation as the runner).
2. Static init populates the registry.
3. Iterate the registry; for each test, expand its params builder to count cases/subcases
   (without running `fn` and without a GPU).
4. Emit `src/webgpu/listing.json` — a static catalog: file paths, test names, subcase counts.

Because params expansion is GPU-free (no device needed to compute the cartesian product), this
runs anywhere. The listing lets non-GPU tools (web viewers, CI planners) reason about the suite
without executing it, exactly as upstream's `listing.js` does.

> Note: a few upstream params builders touch device limits at list time. We forbid that for
> ported tests (params must be statically computable). Where upstream computes params from
> limits, we port it as a runtime `skipIf*` inside the test instead. Called out in the
> [porting guide](05-porting-guide.md).

---

## 9. Harness self-tests

Mirrors `src/unittests/`. Before trusting GPU results we test the framework itself, with no GPU.
These **may use an existing C++ test framework** (doctest — single header, fast compile) since
they are internal and unconstrained by the CTS query/subcase model:

- `params_test.cpp` — cartesian expansion, `expand`/`filter`, case/subcase split.
- `query_test.cpp` — parse/stringify/compare round-trips; **param-value stringification parity**
  against a table of upstream-derived expected strings.
- `tree_test.cpp` — tree building and query containment.
- `loading_test.cpp` — registry/listing agreement.

These compile into a separate `cts_unittests` executable.

---

## 10. Summary of the author-facing surface

The complete author-facing surface is just C++ classes/templates from `cts/*.h`:

- Registration: `MakeTestGroup<F>(path, desc)`, `CTS_TEST(g, "name")` + builder setters
  (`.desc`, `.params`, `.fn`, `.unimplemented`, `.paramsSubcasesOnly`).
- Params: `ParamsBuilder` + `combine/expand/filter/unless/beginSubcases` + the `Value` variant +
  `t.param<T>(...)`.
- Fixtures: `Fixture` / `GpuTest` references with accessors, helpers, and assertions.

Everything else (params expansion, query, tree, runner, pool, logging) is harness-internal C++.
See [04-authoring-tests](04-authoring-tests.md) for the full author API and a worked example.
