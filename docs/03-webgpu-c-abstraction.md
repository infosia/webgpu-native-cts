# 03 — WebGPU C API Abstraction

The upstream CTS is written with `async`/`await` over the WebGPU JS API. The WebGPU **C** API
models the same operations as `WGPUFuture` values resolved through callbacks driven by
`wgpuInstanceWaitAny` / `wgpuInstanceProcessEvents`. Straight-line C++ test code needs
**synchronous wrappers**. This document specifies those wrappers, the error-scope helpers, and
the backend shim that lets one test binary build against wgpu-native, yawgpu, or Dawn.

This is the layer with no direct upstream analog; it is the most important thing to get right.
The harness and tests are C++20, so the wrappers use values/`std::optional`, RAII, and lambdas
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

The core primitive blocks until an async op completes, with a timeout, pumping the instance:

```cpp
namespace cts {
// Poll via wgpuInstanceProcessEvents until a caller-owned `completed` flag (set by the op's
// callback) is true, or the deadline elapses. A bare WGPUFuture cannot be queried without
// WaitAny, so completion is observed via the flag, not the future handle.
WGPUWaitStatus pumpUntil(WGPUInstance instance, const bool* completed, uint64_t timeoutNs);
}
```

`pumpUntil` is the **single** wait path for every backend (wgpu-native, yawgpu, Dawn). We do
**not** use `wgpuInstanceWaitAny`: the current wgpu-native build leaves it unimplemented (it
*panics/aborts* across the FFI rather than returning a status — see [FINDINGS](FINDINGS.md)), while
`AllowProcessEvents` polling resolves callbacks uniformly on all three backends. A capability-gated
WaitAny path could be added later if some backend ever needs it (not needed today); an earlier
`backendSupportsTimeoutWaitAny()` flag and a `waitFuture` placeholder were removed as dead code
(see [07-roadmap](07-roadmap.md)).

> **Callback mode**: all harness-issued async ops use `WGPUCallbackMode_AllowProcessEvents`. We
> avoid `AllowSpontaneous` for harness ops so callbacks only fire at well-defined drain points; the
> device-lost and uncaptured-error callbacks are the exception (§5).

On top of `pumpUntil`, typed blocking helpers capture the callback result and return it by
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

struct ComputePipelineResult { WGPUCreatePipelineAsyncStatus status; WGPUComputePipeline pipeline; std::string message; };
struct RenderPipelineResult  { WGPUCreatePipelineAsyncStatus status; WGPURenderPipeline  pipeline; std::string message; };
ComputePipelineResult createComputePipelineAsyncSync(WGPUInstance, WGPUDevice, const WGPUComputePipelineDescriptor*);
RenderPipelineResult  createRenderPipelineAsyncSync (WGPUInstance, WGPUDevice, const WGPURenderPipelineDescriptor*);

} // namespace cts
```

Each helper:

1. Declares a small result struct on the stack.
2. Sets `callbackInfo.userdata1 = &result`.
3. The C callback (a `static`/captureless trampoline) copies status/handle/message into `result`
   (copying the `WGPUStringView` message into the `std::string`; messages are not owned by us) and
   sets `result.completed = true`.
4. Calls `pumpUntil(instance, &result.completed, timeoutNs)`, then returns `result`.

These wrappers are what test code and the `GpuTest` fixture call. A test never writes a raw
callback. (RAII wrappers — e.g. a `unique_ptr`-like handle that calls `wgpuXxxRelease` — may wrap
the returned owned handles where convenient, but the raw `WGPUDevice`/`WGPUAdapter` is exposed to
match the upstream shape.)

### Error contract: wait/delivery status vs operation status

Every wrapper separates two independent failure axes, and treats them differently:

- **Wait/delivery failure** — the future never resolved within the timeout, or the callback was
  reported with a delivery-level failure (a `WGPUWaitStatus` other than `Success`; a
  `WGPUPopErrorScopeStatus` of `Unknown`/`DeviceLost`; an unexpected device loss). This is a
  **harness failure**, not something the test reasons about. The wrapper **throws**
  `cts::AsyncFailure` (with `cts::AsyncTimeout` for the timeout case); the runner catches it at the
  (sub)case boundary and turns it into a `fail` with a clear message (e.g. "future did not resolve
  within N ms"). Tests never handle these.
- **Operation status** — the operation succeeded or failed *as the API defines it*
  (`WGPURequestAdapterStatus`, `WGPUMapAsyncStatus`, `WGPUQueueWorkDoneStatus`, or the
  `WGPUErrorType` from a popped scope). This is **returned by value** so the test can assert on it,
  because some tests legitimately expect the operation to fail. It is never thrown.

So: a non-`Success` **wait** status → the wrapper throws; a resolved future carrying a non-success
**operation** status → returned, never thrown. For `popErrorScopeSync`, `ScopeResult.status`
(delivery) follows the wait-failure axis (throws on `DeviceLost`/`Unknown`), while
`ScopeResult.type` (the error type) follows the operation axis (returned, asserted by
`expectValidationError`). The fixture's `eventually(...)` and tracked-cleanup paths convert any
escaping `AsyncFailure` during `finalize` into a case failure rather than letting it propagate out
of the runner.

### Timeouts and hangs

A default timeout (e.g. 5 s, configurable via `--future-timeout-ms`) guards against a backend
that never resolves a future. On timeout the helper throws `cts::AsyncTimeout` (see the error
contract above), which the runner turns into a clear "future did not resolve" case failure rather
than hanging the run.

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
| `await device.createRenderPipelineAsync(d)` | `cts::createRenderPipelineAsyncSync(instance, device, &d);` |
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
   - if the pop's delivery `status` is `DeviceLost`/`Unknown` → harness failure (throws
     `cts::AsyncFailure`; see §2 error contract), distinct from the error `type`;
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

> **Hard rule — never throw from a WebGPU C callback.** These callbacks fire from inside the
> implementation (e.g. during `wgpuInstanceProcessEvents` / `wgpuQueueSubmit`), across the
> C/Rust FFI boundary. Throwing a C++ exception through that frame is undefined behavior and will
> typically abort. So the uncaptured-error / device-lost callbacks (and every harness callback)
> **record** onto the current fixture — set a pending-error string / flag, never call
> `fail()`/`skip()` (which throw). The runner checks the recorded state *after* the drain point
> (after `fn()`/`finalize()`) and turns an unexpected pending error into a `Fail`. (Confirmed in
> the Phase 1 review: an earlier version threw from the callback and was reworked to record.)

---

## 6. Backend shim

All three backends implement the canonical `webgpu.h`, but a few things differ at the edges:

| Concern | wgpu-native | yawgpu | Dawn | Shim approach |
|---------|-------------|--------|------|---------------|
| Canonical header | `ffi/webgpu-headers/webgpu.h` | `ffi/webgpu-headers/webgpu.h` | `include/webgpu/webgpu.h` (generated) | `cts/webgpu.h` includes the active backend's canonical header via an include path chosen by CMake |
| Instance creation extras | `wgpuCreateInstance` (+ `wgpu.h` native extensions) | `wgpuCreateInstance` (+ `yawgpu.h`: chain `YaWGPUInstanceBackendSelect` on the instance descriptor to pick Metal/Vulkan/GLES) | `wgpuCreateInstance` / `dawnProcSetProcs` proc table | `cts::createInstance()` wraps creation; the yawgpu path **must** chain `YaWGPUInstanceBackendSelect` (an *unchained* yawgpu instance returns a **Noop** backend — confirmed in Phase 2); Dawn path sets the proc table if required |
| Native feature / vendor enums | `WGPUNativeFeature` (`wgpu.h`) | `YAWGPU_*` / `YAWGPU_STYPE_*` (`yawgpu.h`) | C++-only native extensions | Vendor-only features accessed through optional `cts/backend_wgpu.h` / `cts/backend_yawgpu.h` / `cts/backend_dawn.h`; portable tests avoid them |
| Logging/callbacks setup | `wgpuSetLogCallback` (native) | impl-specific | Dawn toggles | Optional, behind the backend header |
| Adapter enumeration nuances | impl-specific defaults | impl-specific defaults | impl-specific defaults | normalized in `cts::requestAdapterSync` defaults |

Design:

- `include/cts/webgpu.h` re-exports the canonical `webgpu.h` and declares only spec types/calls.
- `src/common/webgpu/backend.h` declares `cts::createInstance` and `cts::backendName`.
- One implementation per backend: `backend_wgpu.cpp`, `backend_yawgpu.cpp`, `backend_dawn.cpp`;
  CMake compiles exactly one based on `CTS_BACKEND`.
- **Portable tests include only `cts/*.h`** and the canonical `webgpu.h`. A test that needs a
  vendor extension includes the backend-specific header and is compiled only for that backend
  (guarded by `#ifdef CTS_BACKEND_WGPU` / `CTS_BACKEND_YAWGPU` / `CTS_BACKEND_DAWN`). Such tests are
  excluded from the other backends' runs and reported as N/A.
- yawgpu requires `cts::createInstance()` to chain `YaWGPUInstanceBackendSelect` to choose a real
  GPU backend (Metal/Vulkan/GLES); **without the chain it returns a Noop instance**. The compiled-in
  backend is also gated by yawgpu's cargo features (`metal`/`vulkan`). The shim picks a **platform
  default** — `YAWGPU_INSTANCE_BACKEND_METAL` on Apple, `YAWGPU_INSTANCE_BACKEND_VULKAN` elsewhere
  (Windows/Linux) — so yawgpu must be built with the matching feature. A runtime
  `--yawgpu-backend metal|vulkan` option is deferred until more than one backend is compiled in.

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

- **WaitAny support** differs between backends; we sidestep it entirely with `pumpUntil`
  (ProcessEvents polling), exercised on each. *Confirmed:* the current wgpu-native build leaves
  `wgpuInstanceWaitAny` unimplemented (it panics); yawgpu and Dawn resolve callbacks fine under
  ProcessEvents too, so all three use the single poll path.
- **Message ownership**: `WGPUStringView` messages in callbacks are not owned; we copy into
  `std::string`. Confirm length handling and truncation are sane.
- **Uncaptured-error timing**: when exactly does each backend deliver uncaptured errors relative
  to `submit`/`processEvents`? The error-scope helpers must drain enough for the assertion to be
  reliable. Validate against all three backends with a deliberately-invalid operation.
- **Pooled device + error scope interaction**: ensure a leftover error scope or pending error
  from a prior case cannot leak into the next (reset/drain on case boundaries).
