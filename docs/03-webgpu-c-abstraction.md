# 03 — WebGPU C API Abstraction

The upstream CTS is written with `async`/`await` over the WebGPU JS API. The WebGPU **C** API
models the same operations as `WGPUFuture` values resolved through callbacks driven by
`wgpuInstanceWaitAny` / `wgpuInstanceProcessEvents`. Straight-line C++ test code needs
**synchronous wrappers**. This document specifies those wrappers, the error-scope helpers, and
the backend shim that lets one test binary build against wgpu-native or Dawn.

This is the layer with no direct upstream analog; it is the most important thing to get right.
The harness and tests are C++17, so the wrappers use values/`std::optional`, RAII, and lambdas
— but they call the plain WebGPU **C** API underneath.

---

## 1. The async model in the C API

Asynchronous operations follow a uniform shape:

```c
WGPUFuture wgpuXxx(..., WGPUXxxCallbackInfo callbackInfo);
```

`callbackInfo` carries a `WGPUCallbackMode`, a function pointer, and two `userdata` pointers.
A future does not run the callback on its own; the application drives completion via:

- `wgpuInstanceWaitAny(instance, count, WGPUFutureWaitInfo*, timeoutNS)` — block until at least
  one of the listed futures completes (or timeout), then fire its callback.
- `wgpuInstanceProcessEvents(instance)` — fire any ready `AllowProcessEvents`/`AllowSpontaneous`
  callbacks without blocking.

Async operations the CTS uses (and we must wrap):

| C function | Callback status enum | Upstream JS |
|------------|----------------------|-------------|
| `wgpuInstanceRequestAdapter` | `WGPURequestAdapterStatus` | `gpu.requestAdapter()` |
| `wgpuAdapterRequestDevice` | `WGPURequestDeviceStatus` | `adapter.requestDevice()` |
| `wgpuBufferMapAsync` | `WGPUMapAsyncStatus` | `buffer.mapAsync()` |
| `wgpuQueueOnSubmittedWorkDone` | `WGPUQueueWorkDoneStatus` | `queue.onSubmittedWorkDone()` |
| `wgpuDevicePopErrorScope` | `WGPUPopErrorScopeStatus` + `WGPUErrorType` | `device.popErrorScope()` |
| `wgpuDeviceCreateComputePipelineAsync` | `WGPUCreatePipelineAsyncStatus` | `createComputePipelineAsync()` |
| `wgpuDeviceCreateRenderPipelineAsync` | `WGPUCreatePipelineAsyncStatus` | `createRenderPipelineAsync()` |

---

## 2. Synchronous wrappers (`cts/webgpu.h` → `src/common/webgpu/sync.cpp`)

The core primitive: block on a single future with a timeout, pumping the instance.

```cpp
namespace cts {
// Uses WaitAny when the backend supports a timeout; otherwise ProcessEvents polling.
WGPUWaitStatus waitFuture(WGPUInstance instance, WGPUFuture future, uint64_t timeoutNs);
}
```

Implementation strategy:

1. Build a single-element `WGPUFutureWaitInfo`.
2. Call `wgpuInstanceWaitAny(instance, 1, &info, timeoutNs)`.
3. If the backend returns `WGPUWaitStatus_UnsupportedTimeout` (some configurations only support
   `timeoutNs == 0`), loop: `wgpuInstanceProcessEvents(instance)` + `wgpuInstanceWaitAny(..., 0)`
   until `info.completed` or a wall-clock deadline elapses.

> **Callback mode**: all harness-issued async ops use `WGPUCallbackMode_AllowProcessEvents` (or
> `WaitAnyOnly` where the backend requires it for `WaitAny`). We avoid `AllowSpontaneous` for
> harness ops so callbacks only fire at well-defined drain points; the device-lost and
> uncaptured-error callbacks are the exception (§5).

On top of `waitFuture`, typed blocking helpers capture the callback result and return it by
value:

```cpp
namespace cts {

struct AdapterResult {
    WGPURequestAdapterStatus status;
    WGPUAdapter              adapter;   // owned; caller releases
    std::string             message;
};
AdapterResult requestAdapterSync(WGPUInstance, const WGPURequestAdapterOptions*);

struct DeviceResult {
    WGPURequestDeviceStatus status;
    WGPUDevice              device;     // owned
    std::string             message;
};
DeviceResult requestDeviceSync(WGPUAdapter, const WGPUDeviceDescriptor*);

WGPUMapAsyncStatus       bufferMapSync(WGPUInstance, WGPUBuffer, WGPUMapMode, size_t off, size_t size);
WGPUQueueWorkDoneStatus  queueWaitSync(WGPUInstance, WGPUQueue);            // onSubmittedWorkDone

struct PipelineResult { WGPUCreatePipelineAsyncStatus status; WGPUComputePipeline pipeline; std::string message; };
PipelineResult createComputePipelineAsyncSync(WGPUInstance, WGPUDevice, const WGPUComputePipelineDescriptor*);

} // namespace cts
```

Each helper:

1. Declares a small result struct on the stack.
2. Sets `callbackInfo.userdata1 = &result`.
3. The C callback (a `static`/captureless trampoline) copies status/handle/message into `result`
   (copying the `WGPUStringView` message into the `std::string`; messages are not owned by us).
4. Calls `waitFuture(...)`, then returns `result`.

These wrappers are what test code and the `GpuTest` fixture call. A test never writes a raw
callback. (RAII wrappers — e.g. a `unique_ptr`-like handle that calls `wgpuXxxRelease` — may wrap
the returned owned handles where convenient, but the raw `WGPUDevice`/`WGPUAdapter` is exposed to
match the upstream shape.)

### Timeouts and hangs

A default timeout (e.g. 5 s, configurable via `--future-timeout-ms`) guards against a backend
that never resolves a future. On timeout the helper records a harness error and the case fails
with a clear "future did not resolve" message rather than hanging the run.

---

## 3. Mapping `await` patterns

| Upstream | C++ wrapper |
|----------|-------------|
| `const adapter = await gpu.requestAdapter(opts)` | `auto a = cts::requestAdapterSync(instance, &opts);` |
| `const device = await adapter.requestDevice(desc)` | `auto d = cts::requestDeviceSync(adapter, &desc);` |
| `await buffer.mapAsync(GPUMapMode.READ, 0, size)` | `cts::bufferMapSync(instance, buffer, WGPUMapMode_Read, 0, size);` |
| `await queue.onSubmittedWorkDone()` | `cts::queueWaitSync(instance, queue);` |
| `const err = await device.popErrorScope()` | `auto r = cts::popErrorScopeSync(instance, device);` (§4) |
| `await device.createComputePipelineAsync(d)` | `cts::createComputePipelineAsyncSync(instance, device, &d);` |
| `t.shouldReject('OperationError', p)` | check the wrapper's returned status equals the expected failure |

Synchronous JS calls (`device.createBuffer`, `encoder.finish`, `queue.submit`) map 1:1 to their
`wgpu*` C calls with no wrapper.

---

## 4. Error scopes and validation expectations

Upstream's `expectValidationError(fn, shouldError)` wraps an operation in
`pushErrorScope('validation')` / `await popErrorScope()` and asserts whether an error occurred.
`popErrorScope` is async in the C API (`wgpuDevicePopErrorScope` returns a future).

### Sync error-scope helper

```cpp
namespace cts {
struct ScopeResult {
    WGPUPopErrorScopeStatus status;   // Success / Unknown / DeviceLost (delivery)
    WGPUErrorType           type;     // NoError / Validation / OutOfMemory / Internal / Unknown
    std::string             message;
};
void        pushErrorScope(WGPUDevice, WGPUErrorFilter);
ScopeResult popErrorScopeSync(WGPUInstance, WGPUDevice);
}
```

### Author-facing form: closures (exactly like upstream)

Because C++ has closures, the author form is a lambda — identical in shape to upstream:

```cpp
t.expectValidationError([&] {
    WGPUBufferDescriptor bad = { .size = 6, .usage = WGPUBufferUsage_MapRead };
    WGPUBuffer b = wgpuDeviceCreateBuffer(t.device(), &bad);   // expected to error
    t.trackForCleanup(b, wgpuBufferRelease);
}, /*shouldError=*/ true);
```

`GpuTest::expectValidationError(std::function<void()> body, bool shouldError)`:

1. `pushErrorScope(device, Validation)`,
2. runs `body()`,
3. `popErrorScopeSync(instance, device)`, then asserts:
   - if `shouldError` and `type == NoError` → fail ("expected validation error, got none");
   - if `!shouldError` and `type != NoError` → fail (reports the message);
   - records the scope message as info for debugging.

Nested calls are supported (a scope stack), mirroring nested `expectValidationError` upstream.

### OOM / internal-error scopes

Same mechanism with `WGPUErrorFilter_OutOfMemory` / `_Internal`, exposed as
`expectOutOfMemoryError` / `expectInternalError` where ported tests need them.

---

## 5. Device-lost and uncaptured-error routing

Two callbacks are set at device creation and can fire at any drain point:

- **Uncaptured error** (`WGPUUncapturedErrorCallbackInfo` on the device descriptor): an error not
  caught by any scope. Upstream treats an unexpected uncaptured error as a *test failure*. Our
  callback routes to the **currently-running fixture** (a thread-local "current test" pointer the
  runner sets around each case) and records a failure, unless the case opted in to expecting it.
- **Device lost** (`WGPUDeviceLostCallbackInfo`): expected when a test destroys the device or
  intentionally loses it; unexpected loss fails the case. Routed the same way. The `DevicePool`
  also observes loss so it can evict and recreate the pooled device.

Because devices are pooled and outlive a single case, the callbacks capture "which fixture is
current" rather than binding to one fixture at creation. The runner maintains a thread-local:

```cpp
namespace cts { void setCurrentTest(Fixture* t); }   // set by the runner around each case
```

This mirrors upstream's per-device uncaptured-error listener that `GPUTest` installs, adapted to
pooled, long-lived devices.

---

## 6. Backend shim

Both backends implement the canonical `webgpu.h`, but a few things differ at the edges:

| Concern | wgpu-native | Dawn | Shim approach |
|---------|-------------|------|---------------|
| Canonical header | `ffi/webgpu-headers/webgpu.h` | `include/webgpu/webgpu.h` (generated) | `cts/webgpu.h` includes the active backend's canonical header via an include path chosen by CMake |
| Instance creation extras | `wgpuCreateInstance` (+ `wgpu.h` native extensions) | `wgpuCreateInstance` / `dawnProcSetProcs` proc table | `cts::createInstance()` wraps creation; Dawn path sets the proc table if required |
| Native feature enums | `WGPUNativeFeature` (`wgpu.h`) | C++-only native extensions | Native-only features accessed through optional `cts/backend_wgpu.h` / `cts/backend_dawn.h`; portable tests avoid them |
| Logging/callbacks setup | `wgpuSetLogCallback` (native) | Dawn toggles | Optional, behind the backend header |
| Adapter enumeration nuances | impl-specific defaults | impl-specific defaults | normalized in `cts::requestAdapterSync` defaults |

Design:

- `include/cts/webgpu.h` re-exports the canonical `webgpu.h` and declares only spec types/calls.
- `src/common/webgpu/backend.h` declares `cts::createInstance`, `cts::backendName`, and capability
  flags (`cts::backendSupportsTimeoutWaitAny()`).
- Two implementations: `backend_wgpu.cpp`, `backend_dawn.cpp`; CMake compiles exactly one based on
  `CTS_BACKEND`.
- **Portable tests include only `cts/*.h`** and the canonical `webgpu.h`. A test that needs a
  native extension includes the backend-specific header and is compiled only for that backend
  (guarded by `#ifdef CTS_BACKEND_WGPU` etc.). Such tests are excluded from the other backend's
  run and reported as N/A.

### Conformance philosophy regarding backend differences

Where backends legitimately differ (optional features, limits, native extensions), the test
**skips** rather than fails (via `skipIf*`). A *conformance* failure is reserved for spec-mandated
behavior a backend gets wrong. The skip-vs-fail decision per test is part of porting and is
recorded in the test (mirroring upstream `skipIf*`).

---

## 7. Initialization sequence (per process)

```cpp
WGPUInstance instance = cts::createInstance();                  // §6
auto a = cts::requestAdapterSync(instance, &opts);             // §2, honors --power-preference etc.
// DevicePool lazily creates devices from a.adapter per requested feature/limit set
```

The instance and adapter are process-global; devices are pooled per feature/limit selection and
recreated on loss. Teardown releases pooled devices, the adapter, then the instance.

---

## 8. Risks / things to validate during the slice

- **WaitAny timeout support** differs between backends and configurations; the `waitFuture`
  fallback (ProcessEvents polling) must be exercised on both.
- **Message ownership**: `WGPUStringView` messages in callbacks are not owned; we copy into
  `std::string`. Confirm length handling and truncation are sane.
- **Uncaptured-error timing**: when exactly does each backend deliver uncaptured errors relative
  to `submit`/`processEvents`? The error-scope helpers must drain enough for the assertion to be
  reliable. Validate against both backends with a deliberately-invalid operation.
- **Pooled device + error scope interaction**: ensure a leftover error scope or pending error
  from a prior case cannot leak into the next (reset/drain on case boundaries).
