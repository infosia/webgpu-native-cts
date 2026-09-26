# P-9 — Restore the wgpu-native build: `SetImmediates` compatibility shim

> See [`reference/workflow.md`](reference/workflow.md).

## Goal

`CTS_BACKEND=wgpu-native` builds again. The three ported files that call the `SetImmediates`
entry points compile on all three backends through one small shim, with no change to test
behavior on yawgpu or Dawn.

## Background

Since `78c3c76` (2026-07-02, un-stubbed the immediates tests) these files call the standard
`webgpu.h` entry points unguarded:

- `src/webgpu/api/operation/command_buffer/programmable/immediate.spec.cpp`
- `src/webgpu/api/validation/encoding/cmds/setImmediates.spec.cpp`
- `src/webgpu/api/validation/encoding/programmable/pipeline_immediate.spec.cpp`

Standard signature (yawgpu, Dawn):
`wgpu{ComputePass,RenderPass,RenderBundle}EncoderSetImmediates(encoder, uint32_t offset, void const* data, size_t size)`.

The pinned wgpu-native (`9176708`) does **not** declare them in `ffi/webgpu-headers/webgpu.h`; it
declares them in its extension header `ffi/wgpu.h` with a **different parameter order**:
`(encoder, uint32_t offset, uint32_t sizeBytes, void const* data)`. `wgpu.h` does
`#include "webgpu.h"`, so it needs `ffi/webgpu-headers` on the include path. The CTS includes
only `<webgpu-headers/webgpu.h>` (`include/cts/webgpu.h`) with include dir `ffi/`, so the build
fails with "use of undeclared identifier 'wgpu…SetImmediates'". The last good `build-wgpu`
binary predates `78c3c76`.

## Scope

**In:**
- A new header `include/cts/immediates.h` with three inline functions in `namespace cts`:
  ```cpp
  void computePassSetImmediates(WGPUComputePassEncoder e, uint32_t offset, const void* data, size_t size);
  void renderPassSetImmediates(WGPURenderPassEncoder e, uint32_t offset, const void* data, size_t size);
  void renderBundleSetImmediates(WGPURenderBundleEncoder e, uint32_t offset, const void* data, size_t size);
  ```
  - yawgpu / Dawn: forward to the standard entry points unchanged.
  - `CTS_BACKEND_WGPU`: include wgpu-native's `wgpu.h` and call its functions with the reordered
    arguments `(e, offset, static_cast<uint32_t>(size), data)`.
- `CMakeLists.txt`: for the wgpu-native backend only, also add `${CTS_WGPU_HEADER_DIR}/webgpu-headers`
  to the include path (wherever the existing wgpu header dir is added) so `wgpu.h` resolves its
  `#include "webgpu.h"` to the same header the CTS already uses. yawgpu and Dawn configure exactly
  as before.
- The three spec files: replace every direct `wgpu*SetImmediates(...)` call with the matching
  `cts::*SetImmediates(...)` (same arguments, same order) and include `cts/immediates.h`. Nothing
  else changes.
- Update the porting-notes comment in `immediate.spec.cpp` that describes the C mapping, if needed,
  so it names the shim.

**Out (non-goals):**
- Whether immediates actually *work* on wgpu-native at runtime (e.g. whether it requires
  `WGPUNativeFeature_Immediates` or reports `maxImmediateSize`): Claude checks this after the build
  is restored. Tests keep their existing `maxImmediateSize`-based skip logic.
- Any other wgpu-native build breakage beyond these three files: fix only what blocks the build.
  If another, unrelated compile error appears, stop and report it rather than guessing.

## Acceptance criteria

- [ ] `cmake --build build-wgpu --target cts cts_unittests -j 8` succeeds (it is already configured
      against `<wgpu-native>`; see `build-wgpu/CMakeCache.txt`), with no warnings.
- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeeds with no warnings, and
      `build-yawgpu/cts_unittests` exits 0.
- [ ] `--list-cases 'webgpu:*'` from `build-yawgpu` is byte-identical to before.
- [ ] MSVC `/W4 /WX` hygiene (no `__builtin_*`, no variable named `g`, no shadowing, no braced-init
      macro in a ternary; the `size_t`→`uint32_t` narrowing is an explicit `static_cast`).
- [ ] (Claude, GPU) the three immediates files on yawgpu and Dawn give JSON identical to before; the
      wgpu-native results for them are recorded.
- [ ] Changes confined to `include/cts/immediates.h`, `CMakeLists.txt`, the three spec files.
