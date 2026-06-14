#!/usr/bin/env bash
# Isolate-verify CTS queries one at a time (single process each) to tell REAL findings from
# DEGRADATION collateral. The core triage tool: a sharded/`--workers` run at this suite size
# leaves residual GPU-state degradation that makes innocent cases fail with assorted errors, so
# its aggregate fail list is NOT trustworthy. A finding is real only if it still fails when its
# test runs ALONE with no "HAL queue submission failed" / "adapter is consumed" noise.
#
# Usage:
#   scripts/isolate.sh 'webgpu:...:test:*' ['webgpu:...:*' ...]   # queries as args
#   scripts/isolate.sh -f queries.txt                            # one query per line
# Env override: CTS (default ./build-yawgpu/Release/cts.exe), OUTDIR (default /tmp/yawgpu_iso).
# Run from the repo root. cts.exe + the backend .dll must already be current.
#
# Read each verdict as:
#   fail=0                      -> passes in isolation (a sharded-run "fail" here was degradation collateral)
#   fail>0, degradation-noise=0 -> REAL finding; the printed signature is the defect
#   fail>0, degradation-noise>0 -> test too big to isolate cleanly; split it into smaller queries
set -u
CTS=${CTS:-./build-yawgpu/Release/cts.exe}
OUTDIR=${OUTDIR:-/tmp/yawgpu_iso}

QUERIES=()
if [[ "${1:-}" == "-f" ]]; then
  [[ -f "${2:-}" ]] || { echo "no such file: ${2:-}" >&2; exit 2; }
  mapfile -t QUERIES < "$2"
else
  QUERIES=("$@")
fi
[[ ${#QUERIES[@]} -gt 0 ]] || { echo "usage: scripts/isolate.sh QUERY... | -f FILE" >&2; exit 2; }

rm -rf "$OUTDIR"; mkdir -p "$OUTDIR"
n=0
for Q in "${QUERIES[@]}"; do
  [[ -n "${Q// }" ]] || continue          # skip blank lines
  [[ "$Q" == \#* ]] && continue            # skip comments in a -f file
  n=$((n+1)); LOG="$OUTDIR/q$n.log"
  "$CTS" "$Q" > "$LOG" 2>&1
  SUM=$(grep '^summary:' "$LOG" | tail -1)
  HAL=$(grep -c 'HAL queue submission failed' "$LOG")
  ADP=$(grep -c 'adapter is consumed\|failed to request device' "$LOG")
  echo "### $Q"
  echo "    ${SUM:-<no summary — query matched nothing or the process crashed>}"
  echo "    degradation-noise: HAL-submission=$HAL adapter-consumed=$ADP"
  grep '^fail ' "$LOG" \
    | sed -E 's/ (pixel mismatch|GPU buffer mismatch|uncaptured|EXPECTATION).*//; s/format="[^"]*"/format=*/' \
    | sort | uniq -c | sort -rn | head -3 | sed 's/^/        /'
  echo
done
echo "DONE ($n queries)"
