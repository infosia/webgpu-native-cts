# P-8c — Release `GetBindGroupLayout` references in ported tests

> Follow-up to [`audit-untracked-object-leaks.md`](audit-untracked-object-leaks.md) (P-8b), which
> listed this as out of its enumerated call set. See [`reference/workflow.md`](reference/workflow.md).

## Goal

Every `wgpuComputePipelineGetBindGroupLayout` / `wgpuRenderPipelineGetBindGroupLayout` result in
`src/webgpu/**/*.cpp` (each returns a **new reference**) is released once the (sub)case no longer
needs it, without changing any test's API call sequence or expectations.

## Background

There are 71 call sites. P-8b spotted at least three that pass the result straight into a
descriptor and never release it: `api/operation/limits/max_combined_limits.spec.cpp:257,273`,
`api/validation/encoding/programmable/pipeline_bind_group_compat.spec.cpp:626,628` (the
`getLayout` lambda mixes owned and borrowed layouts), and
`shader/execution/expression/call/builtin/texture_sampling/texture_utils.cpp:7045,7103`. Others
store the result in a variable; whether each is released must be checked.

## Scope

**In:**
- Classify every call site: (1) already released on the normal path, (2) never released → fix,
  (3) intentional (the test is about `getBindGroupLayout` identity/lifetime, e.g.
  `api/validation/getBindGroupLayout.spec.cpp`) → leave unless it plainly leaks outside what it tests.
- Fix pattern: keep the returned handle in a local and call `wgpuBindGroupLayoutRelease` after its
  last use on the normal path. A bind group created from it keeps its own reference, so releasing
  after `createBindGroup*` is safe. For helpers/lambdas that mix owned and borrowed layouts, make
  ownership explicit (e.g. return a flag or always return an owned reference and release it) —
  whatever is smallest and clear.
- No change to call order, parameters, or expectations; no double release.

**Out:** `src/common/`, `include/` (no new helpers), exception-path leaks.

## Acceptance criteria

- [ ] `cmake --build build-yawgpu --target cts cts_unittests -j 8` succeeds with no warnings; MSVC
      `/W4 /WX` hygiene.
- [ ] `build-yawgpu/cts_unittests` exits 0; `--list-cases 'webgpu:*'` byte-identical.
- [ ] Report lists every changed site (file:line, classification, fix) and each category-3 site
      with a one-line reason.
- [ ] (Claude, GPU) `webgpu:api,*` and the texture-builtin files (fast) JSON identical to the
      pre-change binaries on yawgpu and Dawn; affected `shader,execution` files spot-checked.
- [ ] Changes confined to `src/webgpu/**/*.cpp`.
