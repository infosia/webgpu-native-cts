# Runtime memory & performance — roadmap

> Planning document (not a single task). Records the 2026-09-25 investigation of avoidable
> retention, copying, and recomputation in the harness run path, and the order in which it is
> being addressed. Each step becomes its own task spec; see [`reference/workflow.md`](reference/workflow.md).

## Invariants every step must preserve

- CTS coverage, result order and counts, case identity (case queries), and crash detection.
- `--shard I/N`, `--shard-from`, worker restart positions, `--isolate`, `--crash-list`,
  `--sample-formats`, `--expectations`, JSON output, and baseline classification.

## Reference measurement (GPU-free listing)

`cts --list-cases 'webgpu:*'` on a Release yawgpu build (existing binary, not a controlled baseline):

| Metric | Result |
| --- | ---: |
| Cases (output lines) | 267,868 |
| Wall time | 1.33 s |
| User / system time | 1.08 / 0.14 s |
| Maximum RSS | ~813 MiB |

```sh
/usr/bin/time -l env DYLD_LIBRARY_PATH=<yawgpu>/target/release \
  build-yawgpu/cts --list-cases 'webgpu:*' > "$TMPDIR/cts-perf-cases.txt"
```

Notes: `/usr/bin/time -l` needs a sysctl the Bash sandbox blocks; `DYLD_LIBRARY_PATH` must be set
through `env` after `/usr/bin/time`. Max RSS includes transient expansion peaks — do not multiply it
by the worker count to estimate parallel memory.

## Steps (recommended order)

1. **Discard transmitted worker results; move instead of copy** (`src/common/runner.cpp`).
   Worker loops (`collectShardResultRuns*`) retain every result after emitting it; the parent's
   merges in `collectParallelRuns()` / `collectRuns()` copy. Task:
   [`runner-result-retention.md`](runner-result-retention.md) (bundled with step 6).
   Follow-up (separate task): share query strings via case IDs instead of per-result copies.
2. **Keep only each worker's assigned plan** (`runner.cpp`, `case_plan.{h,cpp}`). Every worker
   currently loads the whole serialized plan via `loadCasePlan()` then filters with
   `caseSelectedByShard()`. Prefer per-shard plan files that carry global case positions (so
   `--shard-from`, parent `positions`, and restart positions keep their meaning); bump the plan
   format version and reader validation.
3. **GPU pipeline cache ownership/lifetime** (`texture_sampling/texture_utils.cpp`,
   `harness.cpp`). `Sampling/Metadata/TextureLoad/TextureStorePipelineBundle` hold raw handles with
   no release; static per-device maps never evict and survive device recreation. Make bundles
   non-copyable movable RAII owners, invalidate on device teardown (also CPU caches keyed by device,
   e.g. `MipMixWeights`), then measure hit rates before deciding on clearing/capacity policy.
4. **Reduce copying / unnecessary param expansion** (`runner.cpp`, `params.cpp`,
   `format_sample.cpp`). `collectCases()` copies expanded params/subcases; `ParamsBuilder::expand()`
   filters copy records. Move results (build the query before moving params), `reserve()`, filter in
   place; longer-term incremental case generation. `--sample-formats` thresholds must be unchanged.
5. **Avoid repeated expansion in `--isolate` children** (`runSingleCase()`). Pass the parent's
   selected single-case plan to the child instead of re-expanding the whole test per child; keep
   manual `--run-case` and the `--sample-formats` fallback.
6. **Batch worker output per case** (`emitShardResults()`). One flush per case instead of per
   subcase line. Bundled into the step-1 task.

Future (separate design): streaming aggregation that avoids retaining all suite results — needs a
design for result order, JSON, baselines, and expectations.

## Measurement rules

- Compare before/after builds from a controlled source baseline, same Release config, backend,
  query, and worker count. The reference measurement above is not a pre-change baseline.
- Measure GPU-free full listing, narrow queries, sequential, parallel, and isolated runs
  separately; repeat runs and separate startup/cache effects.
- For parallel runs, sample parent + child RSS over time and sum at the same instant; never sum
  per-process maxima.

## Progress

- Steps 1 + 6 — done: [`runner-result-retention.md`](runner-result-retention.md).
- Step 2 — done: [`per-shard-case-plans.md`](per-shard-case-plans.md). `--workers 6 'webgpu:*'`
  startup: worker physical footprint ~585 MB → ~111 MB each; plan 262 MB → ~43 MB per shard.
- Step 3 — done: ownership/lifetime in [`device-scoped-pipeline-caches.md`](device-scoped-pipeline-caches.md);
  stats in [`pipeline-cache-stats.md`](pipeline-cache-stats.md) showed zero cross-file reuse, so
  [`clear-device-scoped-at-file-boundary.md`](clear-device-scoped-at-file-boundary.md) releases the
  caches at spec-file boundaries: one sequential `--sample-formats` process over the 15 texture-builtin
  files peaks at ~260 MB instead of ~907 MB (same pipeline misses, wall time within noise).
- Step 4 (localized part) — done: [`param-expansion-copies.md`](param-expansion-copies.md).
  `--list-cases 'webgpu:*'` 1.16 s / ~800 MiB → 0.86 s / ~733 MiB; `--workers 6 'webgpu:*'` parent
  footprint ~700 MB → ~177 MB. Lazy/incremental case generation remains open.
- Step 5 — done: [`isolate-single-case-plan.md`](isolate-single-case-plan.md). `--sample-formats
  --isolate --workers 6` textureSampleGrad:sampled_3d_coords ~156 s → ~100 s (back to back).
- Bug found on the way: [`fix-sample-formats-vertex-format-abort.md`](fix-sample-formats-vertex-format-abort.md).
- Worker memory (2026-09-26): full `webgpu:*` `--workers 6` on yawgpu/Metal peaked at ~9.7 GB
  (workers ~1.4–1.5 GB each; Dawn ~0.6 GB). Cause: ported tests never released render-bundle
  encoders/bundles (each pinning the buffers/pipelines it recorded) —
  [`tracked-bundle-encoders-queryset.md`](tracked-bundle-encoders-queryset.md),
  [`audit-untracked-object-leaks.md`](audit-untracked-object-leaks.md). After: workers ~0.25 GB
  each, whole-run peak ~2.3 GB, identical results. `GetBindGroupLayout` reference leaks (6 sites,
  3 files) fixed in [`audit-getbindgrouplayout-refs.md`](audit-getbindgrouplayout-refs.md).
  Note: max RSS undercounts Dawn (GPU-private allocations are outside RSS); compare backends with
  `footprint` (e.g. `createBindGroup:buffer,resource_binding_size`: yawgpu max RSS 4.1 GB vs Dawn
  53 MB, but Dawn's footprint peaks at ~7.5 GB; yawgpu returns the memory between cases).
- Linux/Vulkan re-verification (2026-09-26, yawgpu `e429a9b`, NVIDIA RTX 5060 Ti, 29 GB host):
  full sweep in 4 areas, `--workers 4`, `MemoryMax=12G`, `expectations/yawgpu-vulkan.txt`,
  `--baseline` = the pre-fix 2026-09-21 sweep of the same shape. Results identical in all areas
  (`Regressed=0 Fixed=0 New=0 Removed=0`; fail=0 crash=0). Largest single-process max RSS,
  pre-fix → post-fix, with the scope's cgroup `memory.peak`:

  | Area | Max RSS 09-21 → 09-26 | cgroup peak | Wall time 09-21 → 09-26 |
  |---|---|---|---|
  | `api,validation` | 8.70 GB → 8.68 GB | 4.8 GB | 1312 s → 983 s |
  | `api,operation` | 332 MB → 275 MB | 432 MB | 540 s → 501 s |
  | `shader,execution` | 1.79 GB → 621 MB | 2.1 GB | 1278 s → 1537 s |
  | `shader,validation` | 472 MB → 271 MB | 767 MB | 70 s → 78 s |

  Process-tree RSS in `shader,execution` plateaus at ~2.4–2.7 GB for the whole 26 min (no
  growth), where an earlier pre-fix probe climbed to ~15 GB within 42 s. The `api,validation`
  8.7 GB is `createBindGroup:buffer,resource_binding_size` alone (max-binding-size buffers): above
  6 GB for ~12 s, then back to ~0.7 GB — expected, not a leak; it is also why the Linux run cap in
  `CLAUDE.md` is 8G per process. The `shader,execution` wall-time increase is uninvestigated.
- Remaining open: lazy/incremental case generation (step 4, broad part); query-string sharing
  (step 1 follow-up); streaming result aggregation (future).
