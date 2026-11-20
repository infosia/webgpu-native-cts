# Port the remaining `api/operation` override tests

> See [`reference/workflow.md`](reference/workflow.md) and [`../docs/05-porting-guide.md`](../docs/05-porting-guide.md).

## Goal

`api/operation/compute_pipeline/overrides` and `api/operation/render_pipeline/overrides` register every
upstream test at `d116bcc` with upstream's parameters and real bodies, and pass on the Dawn oracle.

## Background

Both files are partial ports: only `basic` exists (both `isAsync` values since `2b48a46`).
`docs/COVERAGE.md` lists the rest as deferred. `<cts>` is the upstream checkout at `d116bcc`.

| Upstream file | Ported | Missing |
|---|---|---|
| `<cts>/src/webgpu/api/operation/compute_pipeline/overrides.spec.ts` (7 tests) | `basic` | `numeric_id`, `computed`, `precision`, `workgroup_size`, `shared_shader_module`, `multi_entry_points` |
| `<cts>/src/webgpu/api/operation/render_pipeline/overrides.spec.ts` (4 tests) | `basic` | `precision`, `shared_shader_module`, `multi_entry_points` |

## Scope

**In:**
- The nine missing tests, in upstream order, with upstream's parameters, subcases and expectations.
- Pipeline creation through the existing `create{Compute,Render}PipelineSyncOrAsync(isAsync, desc)`
  helpers wherever upstream has an `isAsync` parameter.
- Override constants through `WGPUConstantEntry` (`constants` / `constantCount` on the stage),
  numeric IDs as upstream spells them (`@id(N)` → key `"N"`).
- f16 tests gated on `shader-f16` exactly as upstream (`skipIfDeviceDoesNotHaveFeature` → runtime skip).
- Expected values computed the way upstream computes them (including f32/f16 rounding in
  `precision`); readback buffers start zeroed.

**Out:**
- Changes to `basic`, the harness, or other files, except a shared helper inside these two files.
- `docs/`, `specs/`, `listing.json`, expectations (Claude).

## Acceptance criteria

- [ ] `cmake --build build-dawn --target cts cts_unittests -j 8` succeeds with no warnings;
      `build-yawgpu` and `build-wgpu` compile.
- [ ] `build-dawn/cts_unittests` exits 0.
- [ ] `build-dawn/cts --list 'webgpu:api,operation,compute_pipeline,overrides:*'
      'webgpu:api,operation,render_pipeline,overrides:*'` prints the 11 upstream tests with upstream's
      case and subcase counts (checked with the `setup-ts-in-node.js` loader).
- [ ] No other line of `--list-cases 'webgpu:*'` changes.
- [ ] No test body is a stub or an unconditional skip.
- [ ] Dawn/Metal: both files `fail=0 crash=0` (Claude runs this).

## Verification

Coding agent: builds, unit tests, listing comparison against upstream. Claude: Dawn/Metal run of both
files, then yawgpu on request; regenerate `listing.json`; update `docs/COVERAGE.md`.

## References

- `docs/05-porting-guide.md` — params mapping, every parameter must have an effect.
- `specs/test-review-fixes-8b086b8.md` T3 — the async pipeline helpers.
