# 05 — Porting Guide (TypeScript → C++)

This guide explains how to port an upstream `.spec.ts` file to a `.spec.cpp` file. Because both
sides are object-oriented, fluent, and closure-based, the port is **near-mechanical**. Read
[04-authoring-tests](04-authoring-tests.md) first.

## 0. Attribution & licensing

The upstream CTS is BSD-3-Clause. Each ported file should:

- Keep a header comment crediting the upstream source path and revision, e.g.
  `// Ported from gpuweb/cts src/webgpu/api/validation/createBuffer.spec.ts @ <commit>`.
- Preserve the upstream `description` text (as the `MakeTestGroup` description arg).
- Preserve test names and (where possible) case-param names/values so query identities match.

Record the upstream commit being ported against in [`docs/UPSTREAM.md`](UPSTREAM.md) so the suite
tracks a known CTS revision.

## 1. Structural mapping

| Upstream (`.spec.ts`) | Port (`.spec.cpp`) |
|------------------------|--------------------|
| `export const description = '...'` | 2nd arg to `MakeTestGroup<F>(path, "...")` |
| `import { makeTestGroup } ...` | `#include "cts/test.h"` (`using namespace cts;`) |
| `export const g = makeTestGroup(GPUTest)` | `TestGroup<GpuTest> g = MakeTestGroup<GpuTest>("path", "desc");` |
| file location `api/validation/createBuffer.spec.ts` | same path under `src/webgpu/` as `.spec.cpp`; path string `"api,validation,createBuffer"` |
| `class Foo extends GPUTest {...}` | `class Foo : public GpuTest {...};` then `MakeTestGroup<Foo>(...)` |
| `g.test('name')` | `CTS_TEST(g, "name")` |
| `.desc('...')` | `.desc("...")` |
| `.params(u => ...)` | `.params([](ParamsBuilder u){ return ...; })` |
| `.paramsSubcasesOnly(u => ...)` | `.paramsSubcasesOnly([](ParamsBuilder u){ return ...; })` |
| `.fn(t => { ... })` | `.fn([](GpuTest& t){ ... })` (param type = the fixture) |
| `.unimplemented()` | `.unimplemented()` |

## 2. Params mapping

| Upstream | Port |
|----------|------|
| `u.combine('k', [a,b,c])` | `u.combine("k", {a,b,c})` |
| `u.combineWithParams([{...},{...}])` | `u.combineWithParams({ {...}, {...} })` (each `{...}` is a `ParamRecord`) |
| `u.expand('k', p => [...])` | `u.expand("k", [](const ParamRecord& p){ return std::vector<Value>{...}; })` |
| `u.filter(p => cond)` | `u.filter([](const ParamRecord& p){ return cond; })` |
| `u.unless(p => cond)` | `u.unless([](const ParamRecord& p){ return cond; })` |
| `u.beginSubcases()` | `u.beginSubcases()` |
| `const { k } = t.params` / `t.params.k` | `auto k = t.param<T>("k");` |
| param computed from `device.limits.*` at build time | **NOT allowed at build time** → move into the body as `t.skipIf(...)` (see §6) |

Keep the **order** of `combine`/`expand`/`filter` identical to upstream — order affects the query
param order and therefore identity.

## 3. Expectations mapping

| Upstream | Port |
|----------|------|
| `t.expect(cond, msg?)` | `t.expect(cond, msg)` |
| `t.expectValidationError(() => {...}, shouldError)` | `t.expectValidationError([&]{ ... }, shouldError)` |
| `t.shouldReject('OperationError', p)` | call the sync wrapper, assert the returned status |
| `t.shouldThrow('TypeError', () => {...})` | usually no C analog → validation-error or precondition skip; judge per case (see note) |
| `t.skip(msg)` / `t.skipIf(cond, msg)` | `t.skip(msg)` / `t.skipIf(cond, msg)` |
| `t.warn(msg)` | `t.warn(msg)` |
| `await t.expectGPUBufferValuesEqual(...)` | `t.expectBufferValuesEqual(...)` (operation phase) |

> `shouldThrow('TypeError', ...)`: in JS, many *type* errors come from WebIDL coercion that does
> not exist when calling the C API (you cannot pass the wrong type). Such tests often have **no C
> analog** and are marked N/A (see §7). Tests that throw for *spec* reasons (out-of-range enum,
> etc.) map to validation errors or precondition checks.

## 4. WebGPU call mapping

| Upstream JS | C API |
|-------------|-------|
| `device.createBuffer(d)` | `wgpuDeviceCreateBuffer(device, &d)` |
| `device.createTexture(d)` | `wgpuDeviceCreateTexture(device, &d)` |
| `device.createCommandEncoder()` | `wgpuDeviceCreateCommandEncoder(device, nullptr)` |
| `encoder.finish()` | `wgpuCommandEncoderFinish(encoder, nullptr)` |
| `queue.submit([cb])` | `wgpuQueueSubmit(queue, 1, &cb)` |
| `queue.writeBuffer(b, off, data)` | `wgpuQueueWriteBuffer(queue, b, off, data, size)` |
| `await buffer.mapAsync(mode,off,size)` | `cts::bufferMapSync(instance, b, mode, off, size)` |
| `buffer.getMappedRange(off,size)` | `wgpuBufferGetMappedRange(b, off, size)` |
| `await device.popErrorScope()` | `cts::popErrorScopeSync(instance, device)` |
| `await gpu.requestAdapter(o)` | `cts::requestAdapterSync(instance, &o)` |
| `await adapter.requestDevice(d)` | `cts::requestDeviceSync(adapter, &d)` |

Descriptors: JS object literals become C structs (C++ designated initializers keep them readable).
Note the C API requires `nextInChain = nullptr`, counts alongside array pointers, and
`WGPUStringView` `{data,length}` for strings (label, code). A small set of inline helpers smooths
common descriptors (e.g. `cts::bufferDesc(size, usage)`), introduced as patterns repeat.

## 5. A fully worked port

### Upstream (excerpt) — `api/validation/createBuffer.spec.ts`

```ts
export const description = `Validation tests for createBuffer.`;
import { makeTestGroup } from '../../../common/framework/test_group.js';
import { GPUTest } from '../../gpu_test.js';

export const g = makeTestGroup(GPUTest);

g.test('size_alignment')
  .desc('When MAP_READ/MAP_WRITE is set, size must be a multiple of 4.')
  .params(u =>
    u.combine('usage', [GPUBufferUsage.MAP_READ, GPUBufferUsage.MAP_WRITE])
     .beginSubcases()
     .combine('size', [0, 2, 4, 6, 8]))
  .fn(t => {
    const { usage, size } = t.params;
    const valid = size % 4 === 0;
    t.expectValidationError(() => {
      t.createBufferTracked({ size, usage });
    }, !valid);
  });
```

### Port — `src/webgpu/api/validation/createBuffer.spec.cpp`

```cpp
// Ported from gpuweb/cts src/webgpu/api/validation/createBuffer.spec.ts @ <commit>
#include "cts/test.h"
#include "cts/gpu.h"
using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,createBuffer", "Validation tests for createBuffer.");

CTS_TEST(g, "size_alignment")
    .desc("When MAP_READ/MAP_WRITE is set, size must be a multiple of 4.")
    .params([](ParamsBuilder u) {
        return u.combine("usage", {BufferUsage::MapRead, BufferUsage::MapWrite})
                .beginSubcases()
                .combine("size", {0, 2, 4, 6, 8});
    })
    .fn([](GpuTest& t) {
        auto usage = t.param<WGPUBufferUsage>("usage");
        auto size  = t.param<uint64_t>("size");
        bool valid = size % 4 == 0;
        t.expectValidationError([&] {
            WGPUBufferDescriptor d = { .size = size, .usage = usage };
            t.createBufferTracked(d);
        }, /*shouldError=*/ !valid);
    });

} // namespace
```

The query identities match upstream:
`webgpu:api,validation,createBuffer:size_alignment:usage=<n>`.

## 6. Build-time limits → runtime skips

Upstream sometimes parameterizes over `device.limits.maxXxx` at *list* time. We forbid that
(params must be GPU-free; see [02-harness §8](02-harness.md)). Port pattern:

```ts
// upstream
.params(u => u.combine('size', [limits.maxBufferSize, limits.maxBufferSize + 4]))
```
becomes a representative param plus a runtime guard:
```cpp
// port: choose representative constants, then guard at runtime
.params([](ParamsBuilder u){ return u.combine("overMax", {false, true}); })
.fn([](GpuTest& t){
    WGPULimits lim{}; t.getLimits(&lim);
    bool over = t.param<bool>("overMax");
    uint64_t size = lim.maxBufferSize + (over ? 4 : 0);
    t.expectValidationError([&]{
        WGPUBufferDescriptor d = { .size = size, .usage = WGPUBufferUsage_CopyDst };
        t.createBufferTracked(d);
    }, /*shouldError=*/ over);
});
```
This changes the query identity slightly (param is `overMax`, not a numeric size), which is
acceptable and documented per file. Prefer this only where unavoidable.

## 7. Tracking coverage & non-ports

Maintain [`docs/COVERAGE.md`](COVERAGE.md) listing, per upstream file:

- **ported** (file + which tests),
- **partial** (which tests ported / skipped),
- **N/A** (no C analog — e.g. WebIDL TypeError tests, web-platform tests) with a reason,
- **deferred** (e.g. shader execution).

A `tools/coverage` step can diff our listing against an upstream listing to report gaps, mirroring
how upstream tracks `.unimplemented()` tests.

## 8. Porting workflow (per file)

1. Copy the upstream file path into `src/webgpu/...` with `.spec.cpp`.
2. Translate header/description/group.
3. Translate each test (params + body) using the tables above — mostly a syntax transliteration.
4. Replace `await`/Promise patterns with the sync wrappers ([03](03-webgpu-c-abstraction.md)).
5. Mark untranslatable tests `.unimplemented()` or omit + note in `COVERAGE.md`.
6. `gen_listings`; run `cts --list 'webgpu:<file>:*'` to confirm the case count matches upstream.
7. Run against both backends; record skips vs failures.
