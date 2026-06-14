#!/usr/bin/env bash
# Full-suite yawgpu/Vulkan CTS run, manually sharded to dodge the two failure modes a single
# `--workers N` run hits at the current suite size (~600k subcases):
#   * OS FREEZE  — too many concurrent Vulkan devices (>~10) hangs the display driver / whole OS.
#   * DEGRADATION — too many cases per process makes later cases fail en masse with
#                   "HAL queue submission failed: vulkan" (fake fails, not findings).
# `--workers N` couples shard-count to concurrency, so it cannot satisfy both. This script keeps
# concurrency low (N) while making each shard small (M total shards). See scripts/README.md.
#
# Usage:  scripts/run_sharded.sh [M_SHARDS] [N_CONCURRENT] [EXPECTATIONS]
#   M_SHARDS      total shards          (default 48; more shards = fewer cases/process = less degradation)
#   N_CONCURRENT  shards run at once    (default 8;  keep <=8-10 or the GPU/driver freezes the OS)
#   EXPECTATIONS  expectations file     (default expectations/yawgpu-vulkan.txt)
# Env overrides: CTS (default ./build-yawgpu/Release/cts.exe), OUTDIR (default /tmp/yawgpu_shards).
# Run from the repo root. cts.exe + the backend .dll must already be current (rebuild first).
set -u
CTS=${CTS:-./build-yawgpu/Release/cts.exe}
M=${1:-48}
N=${2:-8}
EXP=${3:-expectations/yawgpu-vulkan.txt}
OUTDIR=${OUTDIR:-/tmp/yawgpu_shards}

# Derive the full file-level query set from the checked-in catalog (no hardcoded list).
mapfile -t QUERIES < <(grep -o '"path":"[^"]*"' src/webgpu/listing.json | sed 's/"path":"//;s/"//;s/$/:*/;s/^/webgpu:/')
[[ ${#QUERIES[@]} -gt 0 ]] || { echo "no queries derived from src/webgpu/listing.json (run from repo root?)" >&2; exit 2; }
echo "queries=${#QUERIES[@]}  M=$M shards  N=$N concurrent  exp=$EXP"

rm -rf "$OUTDIR"; mkdir -p "$OUTDIR"
for ((start=0; start<M; start+=N)); do
  for ((I=start; I<start+N && I<M; I++)); do      # NOTE: cts --shard is 0-INDEXED: 0/M .. (M-1)/M
    ( "$CTS" --shard "$I/$M" --expectations "$EXP" "${QUERIES[@]}" > "$OUTDIR/shard_$I.log" 2>&1
      echo "__EXIT=$?" >> "$OUTDIR/shard_$I.log" ) &
  done
  wait
  echo "wave done: shards $start..$(( start+N>M ? M-1 : start+N-1 )) / $M"
done

echo "=== AGGREGATE ==="
awk '/^summary:/ { for (i=1;i<=NF;i++) if (split($i,a,"=")==2) s[a[1]]+=a[2]; have++ }
     END { printf "shards_with_summary=%d/'"$M"'\n", have;
           printf "TOTAL pass=%d skip=%d fail=%d crash=%d xfail=%d xpass=%d\n",
                  s["pass"],s["skip"],s["fail"],s["crash"],s["xfail"],s["xpass"] }' "$OUTDIR"/shard_*.log

echo "=== shards with NO summary (aborted mid-run) ==="
for f in "$OUTDIR"/shard_*.log; do grep -q '^summary:' "$f" || echo "  $(basename "$f"): $(tail -1 "$f")"; done

echo "=== candidate fail signatures (degradation noise excluded) ==="
echo "    WARNING: residual degradation can still inflate these. This is a SCREEN, not a verdict —"
echo "    confirm every candidate file with scripts/isolate.sh before trusting it as a finding."
grep -h '^fail ' "$OUTDIR"/shard_*.log \
  | grep -vE 'HAL queue submission failed|adapter is consumed|failed to request device' \
  | sed -E 's/^fail (webgpu:[^:]*:[^:]*):.*/\1/' | sort | uniq -c | sort -rn | head -40
echo "DONE"
