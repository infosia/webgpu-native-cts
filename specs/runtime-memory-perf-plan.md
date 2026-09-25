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
- Next: step 4 (param expansion copies), step 5 (`--isolate` re-expansion).
