# P-12 — Follow wgpu-native `v29.0.1.1`: drop the immediates API mappings

> Supersedes the wgpu-native-specific parts of
> [`wgpu-native-setimmediates-shim.md`](wgpu-native-setimmediates-shim.md) (P-9) and
> [`wgpu-native-immediate-layout-shim.md`](wgpu-native-immediate-layout-shim.md) (P-10).
> See [`reference/workflow.md`](reference/workflow.md).

## Goal

The CTS builds against wgpu-native `v29.0.1.1` (`6aed509`, 2026-06-22 release, headers `673658b`,
wgpu-core/naga 29.0.3; first prototyped against trunk `250eeb6`) on
all three backends. The two immediates API mappings added for the old pin are removed, because the
new wgpu-native implements the standard API.

## Background

wgpu-native `v29.0.1.1` includes "Even easier Immediates" (#583) and "Bump webgpu-native headers to make
immediates easier again" (#592):
- `wgpu{ComputePass,RenderPass,RenderBundle}EncoderSetImmediates` are now declared in the standard
  `ffi/webgpu-headers/webgpu.h` with the standard signature `(encoder, uint32_t offset, void const*
  data, size_t size)`.
- The pipeline layout's immediate size comes from the standard `WGPUPipelineLayoutDescriptor.immediateSize`
  (`src/conv.rs:535`). `WGPUPipelineLayoutExtras` no longer exists.

The CTS wgpu build currently fails with `unknown type name 'WGPUPipelineLayoutExtras'`
(`src/common/harness.cpp`). The `include/cts/immediates.h` wgpu branch (argument reordering) would fail
next. wgpu-native still advertises the experimental `RayQuery` / `CooperativeMatrix` features with
experimental features hard-disabled, so P-11's device-request retry stays.

## Scope

**In:**
- `src/common/harness.cpp` `createPipelineLayoutTracked`: remove the `CTS_BACKEND_WGPU` branch; all
  backends call `wgpuDeviceCreatePipelineLayout(device(), &desc)` as before P-10. Keep the guarded
  `#include <wgpu.h>`, which P-11's native-feature exclusion still needs.
- Remove `include/cts/immediates.h`. In the three spec files
  (`api/operation/command_buffer/programmable/immediate.spec.cpp`,
  `api/validation/encoding/cmds/setImmediates.spec.cpp`,
  `api/validation/encoding/programmable/pipeline_immediate.spec.cpp`), restore the direct standard calls
  `wgpu*SetImmediates(e, offset, data, size)` (same arguments the `cts::` wrappers received), drop the
  `#include "cts/immediates.h"`, and revert the porting-notes wording in `immediate.spec.cpp` that
  mentions the shim.
- `CMakeLists.txt`: keep `ffi/webgpu-headers` on the wgpu-native include path (`<wgpu.h>` still does
  `#include "webgpu.h"`); update its comment so it no longer refers to `include/cts/immediates.h`.

**Out:** P-11 (unchanged); docs (Claude updates `docs/UPSTREAM.md` / FINDINGS / README).

## Acceptance criteria

- [ ] `cmake --build build-wgpu --target cts cts_unittests -j 8` and
      `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeed with no warnings.
- [ ] `build-yawgpu/cts_unittests` exits 0; yawgpu `--list-cases 'webgpu:*'` byte-identical.
- [ ] `git grep -n "immediates.h\|PipelineLayoutExtras" -- src include CMakeLists.txt` finds nothing.
- [ ] MSVC `/W4 /WX` hygiene.
- [ ] (Claude, GPU) immediates files on yawgpu/Dawn JSON identical to before; wgpu-native re-run.
