# Spec — Un-stub `api,validation,encoding,programmable,pipeline_immediate`

## Goal

Port the real test bodies of
`src/webgpu/api/validation/encoding/programmable/pipeline_immediate.spec.cpp`,
which is currently **stubbed** (every write-path case calls
`t.skip("encoder.setImmediates not exported by any backend at this revision")`).
The `wgpuComputePassEncoderSetImmediates` / `wgpuRenderPassEncoderSetImmediates` /
`wgpuRenderBundleEncoderSetImmediates` C entry points are now **exported by
yawgpu** (Block 94; verified via `dumpbin /EXPORTS`), so the stub is stale. This
mirrors what commit `0b8ea7f` already did for `setImmediates.spec.cpp` (validation
cmds) and `command_buffer/programmable/immediate.spec.cpp` (operation).

## Authoritative upstream source

`<upstream-cts-checkout>/src/webgpu/api/validation/encoding/programmable/pipeline_immediate.spec.ts`
— this checkout matches our pin `b507bd1` (see `docs/UPSTREAM.md`). Port it
faithfully. Do **not** use the newer Dawn-tree copy (`C/dawn/third_party/...`),
which is a different revision.

## Reference implementations (already merged — copy their patterns)

- `src/webgpu/api/validation/encoding/cmds/setImmediates.spec.cpp` — provides the
  `ProgrammableEncoderContext` pattern: `makeProgrammableEncoderContext`,
  `ctxSetImmediates` (dispatches to the three C entry points by `encoderType`),
  `ctxFinish` (ends pass / finishes+executes bundle), and `validateFinish(t, ctx,
  shouldSucceed)` which mirrors upstream `validateFinishAndSubmit(shouldSucceed,
  true)`. That context does **not** set a pipeline or issue a draw/dispatch — this
  spec's tests must.
- The current stub file already has usable helpers you should keep/extend:
  `makeImmediateLayout`, `makeComputePipeline`, `makeRenderPipeline`,
  `makeRenderView`, `requiredSlotsRows`, `programmableEncoderTypes`, and
  `runUnusedImmediateNoWrites` (the `usage == "none"` no-write path).

## What to implement

Replicate all four `g.test(...)` bodies from the upstream `.ts`, calling the real
`wgpuXxxSetImmediates` entry points. **Delete** the `skipImmediateWritePath` helper
and every call to it. Keep `skipIfImmediateUnsupported` (the legitimate
`maxImmediateSize == 0 || WGPU_LIMIT_U32_UNDEFINED` guard) and apply it once at the
start of each test body (upstream does this in `PipelineImmediateTest.init()` via
`supportsImmediateData`).

1. **`required_slots_set`** — the big one. Port the WGSL codegen switch over
   `scenario` ∈ {scalar, vector, struct_padding, dynamic_indexing, mixed_types,
   multiple_variables}, the `stage` expansion (compute → `compute`; render →
   vertex/fragment/both), and the exact `setImmediates(offset, size)` byte
   sequences per `usage` ∈ {full, partial, split, overprovision} — including the
   `multiple_variables` per-stage partial branches and the `unreachable()` guards.
   `shouldSucceed = usage ∈ {full, split, overprovision}`. Preserve the
   `.unless(scenario == "scalar" && usage == "split")` exclusion already encoded in
   `requiredSlotsRows()`. Honor the `layoutSize > maxImmediateSize` skip
   (`"maxImmediateSize not large enough for overprovision test"`).
   - Order matters: upstream calls `setImmediates(...)` on the encoder **before**
     `runPass` (which does setPipeline + draw/dispatch). Preserve that order.
   - `setImmediates` payload is a zero-filled `Uint8Array(size)` — a
     `std::vector<uint8_t>(size, 0)` is the C++ analogue; pass `.data()`, `size`.
2. **`unused_variable`** — port the `not_referenced` /
   `referenced_in_unused_function` codegen, the `usage == "partial_start"` 8-byte
   `setImmediates(0, ...)`, then `runPass(..., kImmediateSize=16)`;
   `validateFinish(shouldSucceed=true)`.
3. **`overprovisioned_immediate_data`** — `kLayoutSize=16`, shader uses 4 bytes;
   `kSetSize = larger_than_layout ? 20 : 16`; one `setImmediates(0, kSetSize)`;
   `validateFinish(true)`.
4. **`render_bundle_execution_state_invalidation`** — build a bundle that sets
   immediates + draws; on a **render pass**: setPipeline, setImmediates,
   `executeBundles([bundle])` (invalidates immediate state), setPipeline, then
   conditionally re-`setImmediates` iff `resetImmediates`, draw;
   `validateFinish(shouldSucceed = resetImmediates)`. This one is render-pass-only
   and needs a real bundle executed inside the pass — model it on
   `setImmediates.spec.cpp`'s bundle path but with setPipeline+draw.

## Constraints

- **Provenance header stays** — keep the `Ported from gpuweb/cts ... @ b507bd1...`
  line; this remains a b507bd1 port, not a re-baseline.
- **MSVC `/W4 /WX` clean** and must build on the VS 2022 generator (Windows) **and**
  not break the POSIX/Metal build. Use only the WebGPU C API + existing harness
  helpers (`AllFeaturesMaxLimitsGpuTest`, `createEncoder`-style tracked-resource
  helpers, `expectValidationError`, `getLimits`, `param<T>`).
- Do **not** change the param space / case names / listing.json — only the `.fn`
  bodies (and the helpers they need). The case count (181) must be unchanged; the
  goal is to convert skips into real pass/fail.
- No `t.skip("... not exported ...")` may remain anywhere in the file.

## Acceptance criteria

1. `pipeline_immediate.spec.cpp` compiles `/W4 /WX` on Windows; `grep -c "not
   exported"` on the file == 0; `skipImmediateWritePath` is gone.
2. Every one of the 4 tests executes real bodies (Claude re-verifies:
   `cts.exe --list-cases 'webgpu:api,validation,encoding,programmable,pipeline_immediate:*'`
   still counts 181, and a yawgpu/Vulkan `--workers 8` run reports `skip` only for
   genuine capability gates, not the stub string).
3. Semantics match upstream: `required_slots_set` partial/… expect a validation
   error at finish (`shouldSucceed=false`); full/split/overprovision succeed.
4. POSIX/Metal build unaffected (no Win32-only or backend-only constructs).

## Verification split

- **Coding agent (codex):** implement; build the config it can; report exactly what
  it built/ran vs deferred.
- **Claude:** re-verify the Windows build (double-build + `--list-cases` count),
  run the yawgpu native-Vulkan per-subcase table (`--workers 8`, `--expectations
  expectations/yawgpu-vulkan.txt`, no `--isolate`), and triage any `fail`/`crash`
  (deterministic → candidate finding / expectations update; cross-check vs Dawn per
  the usual oracle rule).
