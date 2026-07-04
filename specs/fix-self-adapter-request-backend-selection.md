# Spec — Self-requested adapters must honor the configured backend

## Goal

Fixtures and tests that create their **own** instance/adapter (instead of using
the harness-cached device) currently call `requestAdapterSync(instance, nullptr)`.
With null options, Dawn picks its **platform default adapter** — on Windows that
is **D3D12** — even when `CTS_DAWN_BACKEND=vulkan` is set. The harness itself
already routes through `cts::adapterOptions()` (`common/webgpu/backend.h`), which
on Dawn carries the configured `backendType` (`backend_dawn.cpp` —
`configuredAdapterBackendType()`); the self-requesting sites bypass it.

Observed consequence (2026-07-03, Dawn/Vulkan control run, see `REPORT.md`):
72 deterministic fails in
`webgpu:api,validation,capability_checks,limits,maxInterStageShaderVariables:createRenderPipeline,at_over:*`
with `FXC compile failed with error: E_FAIL` — FXC is the D3D shader compiler, so
those limit tests silently ran on D3D12 while the rest of the suite ran on Vulkan.
Every other self-requesting site has the same silent wrong-backend problem on
Dawn/Windows.

## The fix

Replace the `nullptr` options argument with `cts::adapterOptions()` at every
self-adapter-request site **except one** (see exclusion). Sites to change
(16 total; line numbers at CTS `e618ca5`, the pre-fix revision):

- `src/webgpu/api/operation/adapter/info.spec.cpp:90`
- `src/webgpu/api/operation/buffers/map.spec.cpp:325`
- `src/webgpu/api/operation/buffers/map_oom.spec.cpp:73`
- `src/webgpu/api/operation/device/all_limits_and_features.spec.cpp:101`
- `src/webgpu/api/operation/device/lost.spec.cpp:99`
- `src/webgpu/api/operation/shader_module/compilation_info.spec.cpp:247`
- `src/webgpu/api/operation/uncapturederror.spec.cpp:118`
- `src/webgpu/api/validation/capability_checks/features/feature_test_helpers.h:195`
- `src/webgpu/api/validation/capability_checks/limits/limit_utils.h:334`
- `src/webgpu/api/validation/error_scope.spec.cpp:79`
- `src/webgpu/api/validation/error_scope.spec.cpp:258`
- `src/webgpu/api/validation/state/device_lost/destroy.spec.cpp:109`
- `src/webgpu/shader/execution/expression/call/builtin/subgroup_util.cpp:347`
- `src/webgpu/shader/execution/shader_io/compute_builtins.spec.cpp:355`
- `src/webgpu/shader/execution/shader_io/fragment_builtins.spec.cpp:1392`
- `src/webgpu/shader/validation/shader_validation_test.h:708`

The change at each site is exactly:
`requestAdapterSync(<instance>, nullptr)` → `requestAdapterSync(<instance>, adapterOptions())`
(unqualified if the file is already inside `namespace cts` / has a using-decl for
the backend helpers — match how the same file calls `createInstance()`; otherwise
qualify as `cts::adapterOptions()`). Add `#include "common/webgpu/backend.h"` only
if the TU does not already see the declaration (every listed file already calls
`createInstance()` from that header, so in practice no new includes are expected).

### Exclusion — deliberate null options

`src/webgpu/api/operation/adapter/requestAdapter.spec.cpp:367`
(test `requestAdapter_no_parameters`, desc "request adapter with no parameters")
must **keep** `nullptr` — passing no options is the behavior under test.

## Why this is safe on the other backends

`cts::adapterOptions()` returns `nullptr` on **wgpu-native**
(`backend_wgpu.cpp:14`) and **yawgpu** (`backend_yawgpu.cpp:54` — yawgpu selects
its backend at instance creation, and the listed sites already create their
instances via `cts::createInstance()`). So the change is byte-for-byte a no-op on
those backends and only affects Dawn adapter selection.

## Constraints

- No changes to test names, param spaces, case counts, or `listing.json`.
- No changes to `src/common/` (the backend abstraction is already correct).
- No other refactoring in the touched files.

## Acceptance criteria

1. All 16 listed sites pass `adapterOptions()`; the `requestAdapter_no_parameters`
   site is unchanged (still `nullptr`).
2. `grep -rn "requestAdapterSync(.*nullptr)" src/webgpu/` returns exactly the one
   excluded site.
3. Both configs compile: `cmake --build build-dawn --config Release -j 1` and
   `cmake --build build-yawgpu --config Release -j 1` (serial, per CLAUDE.md).
4. `--list-cases webgpu:api,validation,*` count unchanged (39,724).
5. Behavior check (run by Claude post-merge): with `CTS_DAWN_BACKEND=vulkan`,
   `build-dawn/Release/cts.exe --workers 4
   webgpu:api,validation,capability_checks,limits,maxInterStageShaderVariables:*`
   no longer produces the 72 `FXC compile failed` fails.
6. yawgpu regression guard (run by Claude): the same query on
   `build-yawgpu/Release/cts.exe` (`CTS_YAWGPU_BACKEND=vulkan`) is unchanged
   from before the patch.
