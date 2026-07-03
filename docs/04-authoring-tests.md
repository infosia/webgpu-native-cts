# 04 — Authoring Tests

This document is the reference for writing a test file (`.spec.cpp`). It defines the full
author-facing C++ API and walks through a complete example. The surface intentionally mirrors the
upstream builder so ported tests read almost identically.

> The API names here are the **proposed** surface, to be finalized in the vertical slice. They
> are stable targets for the porting guide and examples.

---

## 1. Anatomy of a `.spec.cpp` file

```cpp
// src/webgpu/api/validation/createBuffer.spec.cpp
#include "cts/test.h"
#include "cts/gpu.h"
using namespace cts;

namespace {

// 1) Group: bind this file to its query path + description, pick the fixture, register it.
TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,createBuffer",                       // query "file" segment
    "Validation tests for GPUDevice.createBuffer.");      // mirrors `export const description`

// 2) A test: builder chain ending in .fn(...) registers it into g.
CTS_TEST(g, "size_alignment")
    .desc("Buffer size must be a multiple of 4 when a MAP_* usage is requested.")
    .params([](ParamsBuilder u) {
        return u.combine("usage", {WGPUBufferUsage_MapRead, WGPUBufferUsage_MapWrite})
                .beginSubcases()
                .combine("size", {0, 2, 4, 6, 8});
    })
    .fn([](GpuTest& t) {                                  // runs once per subcase
        auto usage = t.param<WGPUBufferUsage>("usage");
        auto size  = t.param<uint64_t>("size");

        t.expectValidationError([&] {
            WGPUBufferDescriptor d = { .size = size, .usage = usage };
            t.createBufferTracked(d);
        }, /*shouldError=*/ size % 4 != 0);
    });

} // namespace
```

Compare the upstream equivalent — the shapes line up:

```ts
export const description = 'Validation tests for GPUDevice.createBuffer.';
export const g = makeTestGroup(GPUTest);

g.test('size_alignment')
  .desc('Buffer size must be a multiple of 4 when a MAP_* usage is requested.')
  .params(u =>
    u.combine('usage', [GPUBufferUsage.MAP_READ, GPUBufferUsage.MAP_WRITE])
     .beginSubcases()
     .combine('size', [0, 2, 4, 6, 8]))
  .fn(t => {
    const { usage, size } = t.params;
    t.expectValidationError(() => {
      t.createBufferTracked({ size, usage });
    }, size % 4 !== 0);
  });
```

---

## 2. Registration

| Upstream | Port | Notes |
|----------|------|-------|
| file location | `MakeTestGroup<F>("a,b,c", "desc")` | the path string is the query "file" segment; must match the file's path under `src/webgpu/` |
| `export const description` | 2nd arg to `MakeTestGroup` | file description |
| `makeTestGroup(GPUTest)` | `MakeTestGroup<GpuTest>(...)` | the template arg is the fixture |
| `g.test('name')` | `CTS_TEST(g, "name")` | returns a fluent builder |
| `.desc(s)` | `.desc("s")` | |
| `.params(u => ...)` | `.params([](ParamsBuilder u){ return ...; })` | a lambda returning the built `u` |
| `.paramsSubcasesOnly(u => ...)` | `.paramsSubcasesOnly([](ParamsBuilder u){ return u...; })` | subcase-only params (lambda returns the built `u`, like `.params`) |
| `.fn(t => { ... })` | `.fn([](GpuTest& t){ ... })` | the test body; the lambda's parameter type is the fixture |
| `.unimplemented()` | `.unimplemented()` | registers a TODO test that reports `skip(unimplemented)` |

`CTS_TEST(g, "name")` expands to a uniquely-named `static const` registrar so the builder chain
is a valid namespace-scope declaration. Test **names** may contain commas for nesting
(`CTS_TEST(g, "limits,maxBufferSize")`), matching upstream nested test names.

> Why a macro and not bare `g.test(...)`: a function-call statement isn't allowed at namespace
> scope; the macro wraps it in a `static const` initializer whose construction runs the chain at
> static-init time. The chain otherwise reads exactly like upstream.

---

## 3. Parameters

```cpp
// Inside the .params lambda, on a ParamsBuilder u (chained, returns the builder):
u.combine("usage", {WGPUBufferUsage_MapRead, WGPUBufferUsage_MapWrite});  // cartesian product with a new key
u.combineWithParams({ {{"format","rgba8unorm"},{"samples",4}}, {...} });  // full-object combine
u.expand("aligned", [](const ParamRecord& p){ return std::vector<Value>{...}; });  // computed
u.filter([](const ParamRecord& p){ return /* keep iff */ true; });
u.unless([](const ParamRecord& p){ return /* drop iff */ true; });
u.beginSubcases();                                       // everything after = subcase scope
```

Value literals come from `Value`'s implicit constructors, so you write `{0, 4, 8}`,
`{"a", "b"}`, `{true, false}` directly. For object params use a `ParamRecord`
(`std::map<std::string, Value>`).

Reading params:

```cpp
template <class T> T   t.param<T>(std::string_view key);  // typed; converts the stored Value
bool                   t.hasParam(std::string_view key);  // for `undefined` handling
const ParamRecord&     t.params();                        // whole merged record
```

`t.param<T>` works in `fn`, and the `ParamRecord&` passed to `expand`/`filter` is read the same
way (`p.at("size")`, helpers `getInt(p,"size")`).

**Constraint** (see [02-harness §8](02-harness.md)): params must be statically computable without
a device. If upstream computes params from device limits, port that as a runtime `skipIf*` inside
the body instead.

---

## 4. Fixtures

The fixture is the template argument to `MakeTestGroup`. Built-in fixtures (extensible):

| Fixture | Use | Upstream analog |
|---------|-----|-----------------|
| `Fixture` | non-GPU harness tests | `Fixture` |
| `GpuTest` | default for WebGPU tests; pooled device w/ default features/limits | `GPUTest` |
| `AllFeaturesMaxLimitsGpuTest` | device with all supported features + max limits | `AllFeaturesMaxLimitsGPUTest` |

A file that needs shared helpers (like upstream's per-file fixture subclasses) defines a small
subclass in the same `.spec.cpp` and uses it as the template arg:

```cpp
class CreateBufferTest : public GpuTest {
  protected:
    WGPUBuffer makeMapped(uint64_t size) { /* shared helper */ }
};
TestGroup<CreateBufferTest> g = MakeTestGroup<CreateBufferTest>("api,validation,createBuffer", "...");
```

This is the direct analog of upstream `class Foo extends GPUTest { ... }` then
`makeTestGroup(Foo)`.

### Fixture interface (`cts/gpu.h`, `GpuTest`)

```cpp
WGPUDevice   GpuTest::device();
WGPUQueue    GpuTest::queue();
WGPUInstance GpuTest::instance();
WGPUAdapter  GpuTest::adapter();

// Device selection / skipping
void GpuTest::selectDeviceOrSkip(std::span<const WGPUFeatureName> feats,
                                 const WGPULimits* limits = nullptr);
void GpuTest::skipIfFeatureUnsupported(WGPUFeatureName feature);
void GpuTest::skipIfTextureFormatUnsupported(WGPUTextureFormat format);
void GpuTest::skipIf(bool cond, std::string reason);

// Tracked resource creation (auto-released at finalize)
WGPUBuffer  GpuTest::createBufferTracked(const WGPUBufferDescriptor& d);
WGPUTexture GpuTest::createTextureTracked(const WGPUTextureDescriptor& d);
template <class H> H GpuTest::trackForCleanup(H handle, void(*release)(H));

// Buffer contents helper
WGPUBuffer  GpuTest::makeBufferWithContents(const void* data, size_t size, WGPUBufferUsage usage);
```

Operation-test helpers (readback/comparison, e.g. `expectBufferValuesEqual`) are documented in a
later phase; see [07-roadmap](07-roadmap.md).

---

## 5. Assertions

```cpp
// Generic (source location captured via std::source_location / a macro fallback)
t.expect(cond, "msg");
t.expectEq(a, b);          // values shown in the message
t.expectFloatEq(a, b, tol);
t.fail("msg");
t.warn("msg");
t.skip("reason");          // throws SkipTestCase — ends this (sub)case cleanly

// Validation error scope — closures, like upstream:
t.expectValidationError([&]{ /* body */ }, /*shouldError=*/ true);
t.expectOutOfMemoryError([&]{ /* body */ }, /*shouldError=*/ true);

// Async expectations (use the sync wrappers from doc 03 and assert on status)
t.expectAsyncStatus(actualStatus, expectedStatus, "what");
```

`skip`/`fail` throw exceptions caught at the (sub)case boundary by the runner, so one failing
subcase does not abort the others. This is the clean C++ analog of upstream `skip()` throwing
`SkipTestCase` (no `setjmp` needed).

---

## 6. Subcases

A test with subcase params is invoked **once per subcase** by the runner; the body just reads
params and asserts. There is no explicit subcase loop in author code (matching upstream, which
re-runs `fn` per subcase). Per-subcase failures are reported individually.

If a test needs case-wide setup shared by all subcases (an expensive resource), it uses the
fixture's shared state (`t.sharedState()`), mirroring upstream `SubcaseBatchState`. Most
validation tests do not need this.

---

## 7. Skipping vs failing (conformance intent)

- **Skip** when the feature/format/limit is legitimately unsupported by the backend (`skipIf*`).
  Skips are not failures.
- **Fail** only when spec-mandated behavior is violated.
- **Warn** for non-fatal diagnostics (rare; mirrors upstream `warn`).

Encode skip conditions explicitly so a port behaves identically on a backend lacking an optional
feature.

---

## 8. File/test naming → query identity

- File path = the `MakeTestGroup` path argument, e.g. `api,validation,createBuffer`.
- Test name = the `CTS_TEST` argument.
- Case params = the `.params` case scope (before `beginSubcases`).

So the example test is addressable as:

```
webgpu:api,validation,createBuffer:size_alignment:usage=4
webgpu:api,validation,createBuffer:size_alignment:usage=8
```

(`usage` is a case param; `size` is a subcase param and not in the query.) The exact value
encoding (`usage=4`) follows the parity rules in [02-harness §2/§3](02-harness.md).

---

## 9. Minimal checklist for a new test file

1. `#include "cts/test.h"` (+ `cts/gpu.h` for GPU tests); `using namespace cts;`.
2. `MakeTestGroup<GpuTest>("api,validation,createBuffer", "description")` (comma-separated path
   matching the file's location under `src/webgpu/`).
3. For each test: `CTS_TEST(g, "name").desc(...).params([]{...}).fn([](GpuTest& t){...});`.
4. Use `t.expectValidationError([&]{...}, shouldError)` / assertions; track created resources.
5. Regenerate the listing (`cmake --build build --target gen_listings -j 1`) and run via query.

See [05-porting-guide](05-porting-guide.md) for translating an upstream file near-mechanically.
