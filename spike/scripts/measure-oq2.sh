#!/usr/bin/env bash
# Spike-local OQ2 evidence sampler. This is not the project performance gate;
# the permanent harness is tests/perf/idle-footprint.sh (owned by Jack).
#
# Usage:
#   ./scripts/measure-oq2.sh [path-to-binary] [extra spike args...]
#
# Environment:
#   DURATION   measurement window in seconds (default 60)
#   MODE       manual | qt-bypass (default qt-bypass)
#   RSS_LIMIT_KB      default 61440  (60 MiB)
#   CPU_LIMIT_PERCENT default 0.3
set -euo pipefail

BIN=${1:-./build/linotch-layer-shell-spike}
shift || true
DURATION=${DURATION:-60}
MODE=${MODE:-qt-bypass}
RSS_LIMIT_KB=${RSS_LIMIT_KB:-61440}
CPU_LIMIT_PERCENT=${CPU_LIMIT_PERCENT:-0.3}
CLK_TCK=$(getconf CLK_TCK)

export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1}
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-wayland}

LOG=$(mktemp -t linotch-spike-XXXXXX.log)
SAMPLES=$(mktemp -t linotch-samples-XXXXXX.txt)
trap 'rm -f "$SAMPLES"' EXIT

echo "binary: $BIN"
echo "mode: $MODE  duration: ${DURATION}s  wayland: $WAYLAND_DISPLAY"
echo "log: $LOG"

"$BIN" --mode="$MODE" --duration="$DURATION" "$@" >"$LOG" 2>&1 &
PID=$!

cleanup() {
  if kill -0 "$PID" 2>/dev/null; then
    kill "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

# Wait for the surface to map (or the process to die).
mapped=0
for _ in $(seq 1 150); do
  if grep -q 'mapped=yes' "$LOG" 2>/dev/null; then
    mapped=1
    break
  fi
  if ! kill -0 "$PID" 2>/dev/null; then
    break
  fi
  sleep 0.1
done

if [[ "$mapped" -ne 1 ]]; then
  echo "FAIL: surface did not report mapped=yes within 15 s" >&2
  sed -n '1,120p' "$LOG" >&2
  exit 1
fi

read_ticks() {
  awk '{print $14 + $15}' "/proc/$PID/stat"
}

start_ticks=$(read_ticks)
start_ns=$(date +%s%N)
last_ticks=$start_ticks
last_ns=$start_ns
sample_count=0
rss_values=()

while kill -0 "$PID" 2>/dev/null; do
  rss=$(awk '/^Rss:/ {print $2}' "/proc/$PID/smaps_rollup" 2>/dev/null || echo "")
  now_ns=$(date +%s%N)
  ticks=$(read_ticks 2>/dev/null || echo "$last_ticks")
  if [[ -n "$rss" ]]; then
    rss_values+=("$rss")
    delta_ticks=$((ticks - last_ticks))
    delta_ns=$((now_ns - last_ns))
    # Per-second CPU percentage, kept only as a sanity trace.
    cpu=$(awk -v dt="$delta_ticks" -v ns="$delta_ns" -v hz="$CLK_TCK" \
      'BEGIN { if (ns <= 0) print 0; else printf "%.3f", (dt / hz) * 1000000000.0 / ns * 100.0 }')
    printf '%s %s %s\n' "$(date +%s)" "$rss" "$cpu" >> "$SAMPLES"
    sample_count=$((sample_count + 1))
    last_ticks=$ticks
    last_ns=$now_ns
  fi
  sleep 1
done

wait "$PID" 2>/dev/null || true
end_ticks=$last_ticks
end_ns=$(date +%s%N)

elapsed_s=$(awk -v a="$start_ns" -v b="$end_ns" 'BEGIN { printf "%.3f", (b - a) / 1e9 }')
mean_cpu=$(awk -v t="$start_ticks" -v e="$end_ticks" -v hz="$CLK_TCK" -v s="$elapsed_s" \
  'BEGIN { if (s <= 0) print 0; else printf "%.3f", ((e - t) / hz) / s * 100.0 }')

if [[ ${#rss_values[@]} -eq 0 ]]; then
  echo "FAIL: no RSS samples collected" >&2
  exit 1
fi

rss_stats=$(printf '%s\n' "${rss_values[@]}" | awk '
  { sum += $1; if (NR == 1 || $1 < min) min = $1; if ($1 > max) max = $1 }
  END { printf "%d %d %d", sum / NR, min, max }')
read -r rss_mean rss_min rss_max <<< "$rss_stats"

cpu_max=$(awk '{ if ($3 > max) max = $3 } END { printf "%.3f", max }' "$SAMPLES")
cpu_median=$(awk '{print $3}' "$SAMPLES" | sort -n | awk '{a[NR]=$1} END {if (NR) print a[int((NR+1)/2)]; else print 0}')
# Steady state excludes the first two 1 s samples, which contain process/event-loop warmup.
cpu_steady=$(awk 'NR > 2 { sum += $3; n++ } END { if (n) printf "%.3f", sum / n; else print 0 }' "$SAMPLES")

echo
echo "samples: $sample_count over ${elapsed_s}s"
echo "RSS kB: mean=$rss_mean min=$rss_min max=$rss_max  (limit $RSS_LIMIT_KB)"
echo "CPU %:  total_mean=$mean_cpu steady_mean=$cpu_steady median_sample=$cpu_median max_sample=$cpu_max  (limit $CPU_LIMIT_PERCENT)"
echo
echo "--- spike phases ---"
grep -E 'OQ1|global |layer surface|configure|mapped|frame done|backing|initial commit|qt-show|ERROR|role|Protocol error' "$LOG" || true
echo
echo "full log: $LOG"

fail=0
if (( rss_mean > RSS_LIMIT_KB )); then
  echo "FAIL: mean RSS above limit"
  fail=1
fi
if awk -v v="$mean_cpu" -v l="$CPU_LIMIT_PERCENT" 'BEGIN { exit !(v > l) }'; then
  echo "FAIL: total-mean CPU above limit"
  fail=1
fi
if (( fail == 0 )); then
  echo "PASS: within RSS and CPU limits for this window"
fi
exit "$fail"
