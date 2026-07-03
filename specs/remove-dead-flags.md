# remove-dead-flags — drop `--yawgpu-backend` and `--future-timeout-ms`

## Problem

Both flags are parsed and pushed into `forwardedArgs` but consumed by nothing:

- `--yawgpu-backend`: the real mechanism is the `CTS_YAWGPU_BACKEND` env var
  (`src/common/webgpu/backend_yawgpu.cpp:30`) plus per-backend build dirs. The
  flag silently no-ops.
- `--future-timeout-ms`: the async-wait timeout is hardcoded
  (`src/common/webgpu/sync.h`, `timeoutNs = 5'000'000'000` default param);
  the flag value is never read. Silently no-ops.

Silently-ignored flags are worse than absent ones.

## Goal

Both flags are unknown options; no dead plumbing remains.

## Scope

**In:** `src/common/runtime/main.cpp` only — delete the
`else if (arg == "--yawgpu-backend" || arg == "--future-timeout-ms")` branch
(~line 285). They then fall into the existing `unknown option` error path.

**Out:** no changes to `forwardedArgs` handling for `--sample-formats` (still
forwarded); no docs edits (Claude handles those); no other flags.

## Acceptance criteria

- [ ] `cts --yawgpu-backend vulkan <q>` and `cts --future-timeout-ms 1000 <q>`
      exit non-zero with `unknown option: ...`.
- [ ] `grep -rn "yawgpu-backend\|future-timeout" src include` → only the
      `CTS_YAWGPU_BACKEND` env-var code in `backend_yawgpu.cpp` remains.
- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 1` succeeds;
      `cts_unittests` exits 0.
