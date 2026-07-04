# Spec — Dawn adapter backend selection (default Vulkan)

## Goal

Make the Dawn backend request a **Vulkan** adapter by default (matching how the
yawgpu backend defaults to Vulkan on non-Apple platforms), with a runtime
override, instead of letting Dawn pick its platform default (D3D12 on Windows).

## Background

The harness requests an adapter via `requestAdapterSync(instance, nullptr)` —
passing no options — so Dawn selects its own preferred backend (D3D12 on
Windows). Dawn's `WGPURequestAdapterOptions` carries a Dawn-specific
`backendType` field (`src/dawn/.../gen/include/dawn/webgpu.h`); setting it to
`WGPUBackendType_Vulkan` forces the Vulkan backend. The standard `webgpu.h`
that wgpu-native and yawgpu compile against does **not** have this field, so the
field may only be referenced from the Dawn shim translation unit
(`backend_dawn.cpp`, compiled only when `CTS_BACKEND=dawn`).

This mirrors the existing yawgpu pattern: `backend_yawgpu.cpp` defaults to
Vulkan on non-Apple via `CTS_YAWGPU_BACKEND`. See [`phase2b-dawn-backend.md`](phase2b-dawn-backend.md).

## Scope

**In:**
- New backend hook `const WGPURequestAdapterOptions* cts::adapterOptions();`
  declared in `src/common/webgpu/backend.h`.
- Definitions in all three shims:
  - `backend_wgpu.cpp` and `backend_yawgpu.cpp` → `return nullptr;` (behavior
    unchanged — adapter request stays default).
  - `backend_dawn.cpp` → return a pointer to a static `WGPURequestAdapterOptions`
    whose `backendType` defaults to Metal on Apple, **Vulkan** elsewhere, and is
    overridable at runtime via the `CTS_DAWN_BACKEND` environment variable
    (`vulkan` / `d3d12` / `d3d11` / `metal` / `opengl` / `opengles` / `null`;
    unknown or unset → the platform default above). Build the options with
    `WGPU_REQUEST_ADAPTER_OPTIONS_INIT` and set only `backendType`.
- Replace the four `requestAdapterSync(..., nullptr)` call sites
  (`src/common/harness.cpp:138,154,467` and `src/common/runtime/main.cpp:87`)
  with `requestAdapterSync(..., cts::adapterOptions())`.

**Out:**
- No change to wgpu-native / yawgpu runtime behavior (their hook returns
  `nullptr`).
- No device-descriptor / feature / limit changes.
- No CMake or DLL-deployment changes (runtime DLL placement is operational, see
  `docs/06-build-and-run.md`).

## Interfaces

```cpp
// src/common/webgpu/backend.h
namespace cts {
WGPUInstance createInstance();
const char* backendName();
const WGPURequestAdapterOptions* adapterOptions();   // NEW; may return nullptr
}
```

`backend_dawn.cpp` reads `CTS_DAWN_BACKEND` with the same MSVC `/WX`-safe
`std::getenv` guard already used in `backend_yawgpu.cpp` (scoped
`#pragma warning(disable:4996)`).

## Acceptance criteria

1. `cmake --build build-dawn --config Release --target cts` succeeds with the
   project's `-Werror`/`/WX` settings (Dawn backend).
2. A wgpu-native configure+build (`CTS_BACKEND=wgpu-native`) and a yawgpu
   configure+build (`CTS_BACKEND=yawgpu`) both still compile with the new hook
   (the field `backendType` must not be referenced outside `backend_dawn.cpp`).
3. With `webgpu_dawn.dll`, `vulkan-1.dll`, and `d3dcompiler_47.dll` next to
   `cts.exe`, a bare `build-dawn/Release/cts.exe` prints
   `backendType: vulkan` (was `d3d12`).
4. `CTS_DAWN_BACKEND=d3d12 cts.exe` (bare) prints `backendType: d3d12` —
   override works.
5. Smoke cases stay green on Dawn/Vulkan:
   `webgpu:api,operation,adapter,info:adapter_info:*`,
   `webgpu:api,operation,adapter,requestAdapter:requestAdapter:*`,
   `webgpu:api,operation,buffers,map:mapAsync,read:*` each report
   `fail=0 crash=0` (some `skip` is fine). (Run by Claude — GPU.)

## Verification

- Claude builds all three backends and runs the bare adapter probe + the three
  smoke queries on Dawn/Vulkan (sandbox disabled). The coding agent does not run
  GPU CTS.

## References

- `docs/06-build-and-run.md` (Dawn build/run recipe; runtime DLLs).
- `src/common/webgpu/backend_yawgpu.cpp` (the `CTS_YAWGPU_BACKEND` pattern this mirrors).
- `specs/phase2b-dawn-backend.md` (original Dawn wiring; historical phase spec, since purged from the repo — name kept for context only).
