# Re-baseline the suite to upstream CTS `d116bcc`

> See [`reference/workflow.md`](reference/workflow.md) and
> [`../docs/UPSTREAM.md`](../docs/UPSTREAM.md) ("Re-baselining process").

## Goal

Every portable upstream test at `gpuweb/cts` `d116bcccd18` (2026-10-23) exists in the suite with
upstream's test names, parameters and expectations, and the suite passes on the Dawn oracle at
`v20261030.184100`, apart from the 2 known `index_buffer_format_dirtying` cases.

## Delta from the current pin (`b507bd117e53`)

53 commits. `<cts>` below is a local `gpuweb/cts` checkout at `d116bcc`. See one file's delta with
`git -C <cts> diff b507bd117e53 d116bcccd18 -- <path>`.

| Group | Files | Changed lines |
|---|---|---|
| Ported spec files changed upstream | 54 | 3,048 |
| New spec files | 15 | 6,520 |
| Non-spec `.ts` helpers in `src/webgpu` | see Batches | 1,986 |

98 tests are added and 2 removed. The 2 removals are renames or replacements:
`shader_io/builtins:parse` and `statement/swizzle_assignment:invalid_index_into_swizzle_view`.

## Scope

**In:**
- All 54 ported files, brought level with `d116bcc`. Upstream `pipeline_immediate.spec.ts` is already
  ported (`8dc100410ad`, F-156); only check it for remaining differences.
- All 15 new spec files.
- The helper changes they need: `buffer_view_utils.ts`, `mixed_type_aliasing_utils.ts`,
  `format_info.ts`, `capability_info.ts`, `shader/types.ts`, `shader/validation/decl/util.ts`,
  `shader/validation/shader_validation_test.ts`, `util/texture/texture_ok.ts`, and
  `binary/i32_arithmetic.cache.ts`.
- The regenerated `src/webgpu/listing.json`, and updates to `docs/UPSTREAM.md` and `docs/COVERAGE.md`
  (Claude does the docs).

**Out:**
- `web_platform/**` and `util/texture/color_space_conversions.spec.ts`: never ported (browser-only or
  no local port).
- Upstream runner and tooling changes in `src/common/tools`, `src/common/runtime`,
  `src/common/internal`, `src/common/framework/metadata.ts`, and `listing_meta.json`.
- Editing Dawn or yawgpu sources. A divergence is a finding, reported to Claude.
- yawgpu and wgpu-native result sweeps. yawgpu is being updated; it is swept after that update lands.
  Those two backends must still **compile**.

## Porting rules

Follow `docs/05-porting-guide.md`. These have each caused real defects:

- **Faithful port.** Keep upstream test names, parameter order and subcases. Never weaken a test to
  make it pass. Do not stub unless the test is impossible through the C API; if so, say why.
- **Error timing.** Object creation and begin-pass errors fire at the C API call. Encoder command
  errors are deferred to `finish()`, so wrap `finish()` or submit for those, not the command.
- **Readback buffers are zero-filled**, never pre-filled with the expected value. A test whose
  expected output equals the initial value false-passes if the pipeline silently fails.
- **MSVC `/W4 /WX`:** no `__builtin_*`, no local variable named `g` (shadows `TestGroup`), no
  braced-init macro inside a ternary, no `u8"..."`, no adjacent `\xNN\xNN` escapes.
- **All three backends must compile.** Headers differ per backend. Before using a newer enumerator,
  check it exists in each backend's header and guard it the way existing ports do (see
  `hasSwizzleAssignmentLanguageFeature` in `shader/execution/statement/swizzle_assignment.spec.cpp`).
- **Feature gating:** a test that needs a feature skips when the feature is missing, as upstream does.
  The all-features device enables `texture-compression-unaligned` when the adapter offers it, so
  tests that expect rejection must follow upstream's gating.
- Update the provenance header of each changed file to `d116bcccd18`.

## Batches

Each batch ends with Claude's review, a Dawn GPU run of the touched queries, and a commit.

| Batch | Content |
|---|---|
| B1 | Changed `api/**` ports and the helpers they use |
| B2 | Changed `shader/execution/**` ports |
| B3 | Changed `shader/validation/**` ports and `decl/util` |
| B4 | New `api/validation` specs and new `shader/validation` specs |
| B5 | New `shader/execution` specs, `buffer_view_utils`, `mixed_type_aliasing_utils` |

## Acceptance criteria

- [ ] `cts --list-cases` on each changed or new file matches the upstream case count, or the
      difference is explained.
- [ ] `cts` compiles for `dawn`, `yawgpu` and `wgpu-native`.
- [ ] Full `webgpu:*` on Dawn `v20261030.184100` (Metal): the only failures are the 2 known
      `index_buffer_format_dirtying` subcases. Any other failure has a disposition in
      `docs/FINDINGS.md`.
- [ ] The 149 stale-port regressions recorded in F-156 pass.
- [ ] New tests that skip on Dawn on Metal are listed with the reason.
- [ ] `listing.json` is regenerated; `docs/UPSTREAM.md` pins `d116bcccd18`.

## Verification

The coding agent builds and lists cases only. Claude runs all GPU verification, with the sandbox
disabled:

```bash
build-dawn/cts --workers 6 --expectations expectations/dawn.txt '<query>'
```
