# COVERAGE — Port Status

Tracks how much of the upstream CTS has been ported, per area and per file. Denominators are the
upstream `.spec.ts` file counts at the pinned revision (see [UPSTREAM.md](UPSTREAM.md)).

**Pinned upstream:** gpuweb/cts `b507bd1` (2026-05-28) — 683 `.spec.ts` files under `src/webgpu/`.

> This is a living document. Update it as files are ported (or deliberately skipped). The file
> table starts empty; only the area summary's denominators are pre-filled.

## Status legend

| Status | Meaning |
|--------|---------|
| **ported** | All (or all in-scope) tests in the file are ported and run on at least one backend |
| **partial** | Some tests ported; others skipped/unimplemented (list which) |
| **N/A** | No native analog (e.g. WebIDL `TypeError` coercion tests, web-platform-only); not portable |
| **deferred** | In scope eventually but intentionally postponed (e.g. shader execution) |
| **todo** | Not started (default) |

A test marked `.unimplemented()` in a ported file counts the file as **partial**.

## Area summary

| Area | Upstream files | Ported | Partial | N/A | Deferred | Todo |
|------|---------------:|-------:|--------:|----:|---------:|-----:|
| `api/validation` | 129 | 0 | 3 | 0 | 0 | 126 |
| `api/operation` | 72 | 0 | 0 | 0 | 0 | 72 |
| `shader/validation` | 207 | 0 | 0 | 0 | 207 | 0 |
| `shader/execution` | 239 | 0 | 0 | 0 | 239 | 0 |
| `compat` | 15 | 0 | 0 | 0 | 0 | 15 |
| `web_platform` | 13 | 0 | 0 | 13 | 0 | 0 |
| `idl` | 3 | 0 | 0 | 3 | 0 | 0 |
| other (root/examples/etc.) | 5 | 0 | 0 | 0 | 0 | 5 |
| **Total** | **683** | **0** | **3** | **16** | **446** | **218** |

Notes on the pre-classified rows:

- **`web_platform` (13) → N/A**: canvas, `importExternalTexture` from `HTMLVideoElement`/`VideoFrame`,
  workers — these depend on the web platform and have no native C-API analog. See
  [00-overview](00-overview.md) non-goals.
- **`idl` (3) → N/A**: WebIDL surface/constant tests; the C API has no IDL layer.
- **`shader/validation` (207) + `shader/execution` (239) → deferred**: shader validation is
  mid-term; shader execution is the largest deferred effort. See [07-roadmap](07-roadmap.md)
  phases 5–6. (Some `shader/validation` files may be reclassified to `todo` once that phase
  starts.)
- **`api/regression`, `shader/regression`**: 0 files at this revision.

These classifications are provisional; revisit per file when the area is worked.

## Phase 1 (vertical slice) — done

The slice ported one real file to prove the harness end-to-end on wgpu-native (see
[07-roadmap §Phase 1](07-roadmap.md)):

| Upstream file | Status | Tests ported | Notes |
|---------------|--------|--------------|-------|
| `api/validation/buffer/create.spec.ts` | partial | `limit`, `new_usages` | `size`/`usage`/`createBuffer_invalid_and_oom` unimplemented; uses `GpuTest` instead of `AllFeaturesMaxLimitsGPUTest` (no extra features needed for these two) |

## Full per-file table

Append rows as files are touched. Keep paths relative to upstream `src/webgpu/`. Only list files
that are no longer `todo` (the area summary covers the rest).

| File | Status | Tests ported / total | Skipped / N/A tests | Notes |
|------|--------|----------------------|---------------------|-------|
| `api/validation/buffer/create.spec.ts` | partial | 4 / 5 (`limit`, `new_usages`, `usage`, `createBuffer_invalid_and_oom`) | `size` (shouldThrow RangeError — no C analog) | green on **yawgpu** (all 4: `usage`=78 cases/156 subcases, `c_i_a_o`=8, `limit`=3, `new_usages`=10). On wgpu-native all pass **except `usage`, which aborts wgpu-native** (see [FINDINGS F-001](FINDINGS.md)). Fixture deviation `GpuTest` |
| `api/validation/buffer/destroy.spec.ts` | partial | 2 / 4 (`all_usages`, `twice`) | `error_buffer` (C error-buffer return semantics), `while_mapped` (needs mapAsync) | green on wgpu-native **and yawgpu** (`all_usages`=10, `twice`=6); first real exercise of the uncaptured-error path; fixture deviation `GpuTest` |
| `api/validation/createSampler.spec.ts` | partial | 1 / 2 (`lodMinAndMaxClamp`) | `maxAnisotropy` (WebIDL number coercion `-1`/`undefined` — no C `uint16` analog) | green on wgpu-native **and yawgpu** (`lodMinAndMaxClamp`=1 case/49 subcases — both validate lod clamp ranges). First float-`Value` test; fixture deviation `GpuTest` |

## Maintenance

- After porting a file: set its row here, update the **Area summary** counts, and confirm
  `cts --list 'webgpu:<file>:*'` matches the upstream case count.
- When re-baselining (see [UPSTREAM.md](UPSTREAM.md)): diff the new upstream listing against ours
  to find added/removed/renamed files and changed case counts; reflect them here.
- A planned `tools/coverage` step automates the listing diff and can emit a fresh area summary —
  see [05-porting-guide §7](05-porting-guide.md).
