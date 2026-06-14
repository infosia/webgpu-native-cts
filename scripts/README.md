# scripts/ — large-suite run & finding-triage helpers

Helper scripts for running the **full yawgpu/Vulkan** suite and separating real findings from
GPU-state artifacts on a single workstation. They wrap `build-yawgpu/Release/cts.exe`; see
[`docs/06-build-and-run.md`](../docs/06-build-and-run.md) for the runner itself and its options.

Run every script **from the repo root**, e.g. `bash scripts/run_sharded.sh`. They assume
`build-yawgpu/Release/cts.exe` and the backend `yawgpu.dll` beside it are **already current** —
rebuild both first if yawgpu HEAD or any `src/webgpu/**` source is newer (see the build doc and the
"VS generator double-build" note). Output logs go under `/tmp` (overridable via `OUTDIR`).

## Why these exist

At the current suite size (~234 file-level queries / ~600k subcases) a single `cts.exe --workers N`
run is **not reliable** for reading findings, because two independent failure modes appear and
`--workers` cannot dodge both at once (it ties shard count to concurrency):

| Failure mode | Cause | Symptom |
|---|---|---|
| **OS freeze** | too many **concurrent** Vulkan devices (>~10) | the display driver / whole OS hangs (hard power-cycle). `--workers 16` froze this box; `--workers 10` did not — keep concurrency **≤ 8–10**. |
| **GPU-state degradation** | too many **cases per process** | later cases fail en masse with `uncaptured error: HAL queue submission failed: vulkan` (plus collateral `adapter is consumed`, pixel mismatches on innocent cases). These are **fake fails** — the named cases pass when run alone. |

So: **shard manually** to keep concurrency low *and* each process small (`run_sharded.sh`), then
**confirm every candidate in isolation** before believing it (`isolate.sh`). Note `cts --shard I/N`
is **0-indexed** (`0/N … (N-1)/N`; `N/N` is an error).

## The scripts

### `run_sharded.sh [M_SHARDS] [N_CONCURRENT] [EXPECTATIONS]`
Full-suite run split into `M` shards (default 48) executed `N` at a time (default 8), with
`--expectations expectations/yawgpu-vulkan.txt` (default). Queries are derived from
`src/webgpu/listing.json` automatically. Prints the summed `pass/skip/fail/crash/...` totals, flags
any shard that aborted without a summary, and lists candidate fail signatures **with degradation
noise filtered out**.

```bash
bash scripts/run_sharded.sh            # 48 shards, 8 concurrent
bash scripts/run_sharded.sh 64 8       # smaller shards if degradation noise is still high
```

This is a **screen, not a verdict**: even at 48 shards some residual degradation remains, so treat
its candidate list as "files worth isolating", then verify each with `isolate.sh`. No OS freeze and
`crash=0` at the defaults.

### `isolate.sh QUERY... | -f FILE`
The core triage tool. Runs each query **alone** (single process) and prints a verdict per query.

```bash
bash scripts/isolate.sh 'webgpu:api,operation,command_buffer,image_copy:offsets_and_sizes:*'
bash scripts/isolate.sh -f /tmp/candidates.txt    # one query per line ('#' comments ok)
```

Read each verdict:
- **`fail=0`** → passes in isolation; a sharded-run "fail" here was degradation collateral. Discard.
- **`fail>0` and `degradation-noise=0`** → **real finding**; the printed signature is the defect.
- **`fail>0` and `degradation-noise>0`** → the test is itself big enough to degrade; split it into
  narrower queries (e.g. per `dimension`/`format`) and isolate those.

### `recheck.sh`
A saved regression guard: isolates the set of tests that have been yawgpu/Vulkan findings (F-103
3D + stencil8 copy, F-096/F-095 usage-scope, F-093b vertex-OOB). Run it after any yawgpu change —
every query should report `fail=0` on yawgpu ≥ `e7db246`. Edit the `WATCH` list in the file as
findings open and close.

```bash
bash scripts/recheck.sh
```

## Typical workflow

```bash
# 0. rebuild if stale (yawgpu Vulkan dll + cts.exe), copy dll beside cts.exe — see docs/06.
# 1. screen the whole suite without freezing the OS:
bash scripts/run_sharded.sh
# 2. for each concentrated candidate it prints, get a verdict:
bash scripts/isolate.sh 'webgpu:<candidate-file>:<test>:*'
# 3. after a yawgpu fix, confirm no regressions on the known set:
bash scripts/recheck.sh
```

These are workstation triage helpers (yawgpu/Vulkan on Windows, hardcoded `build-yawgpu/` +
`expectations/yawgpu-vulkan.txt` defaults; override the `CTS`/`OUTDIR` env vars or the args for
other setups), not the CI entry point. CI uses `cts.exe` directly with `--workers`/`--shard` (see
[`docs/06-build-and-run.md §7`](../docs/06-build-and-run.md)).
