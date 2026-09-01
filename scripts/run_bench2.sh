#!/bin/bash
# Benchmark 2: does switching policy beat any fixed policy?
#
# Two competing objectives under CPU contention:
#   - vLLM time-to-first-token (latency)
#   - antagonist iterations completed (batch throughput)
# Measuring only the first makes the latency policy trivially best, so both are
# reported. Arms:
#   static_latency     slot 0 throughout
#   static_throughput  slot 1 throughout
#   akts_switching     slot 1 in steady state, slot 0 during the burst
#
# The switching arm is oracle-driven (it switches on known phase boundaries),
# which bounds what any detector could achieve; it does not itself demonstrate
# that a burst can be detected online.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$(dirname "$0")/common.sh"
MODEL="${MODEL:-Qwen/Qwen2.5-0.5B-Instruct}"
VENV=/home/ubuntu/vllmenv
CPUSET="${CPUSET:-0-3}"
NHOG="${NHOG:-8}"
SEC="${SEC:-8}"
OUT="${OUT:-/tmp/bench2}"
mkdir -p "$OUT"

setslot() { "$BPFTOOL" map update pinned /sys/fs/bpf/akts_b2/active_slot_map \
             key 0 0 0 0 value "$1" 0 0 0 2>/dev/null; }

start_hogs() {   # $1 = arm name; each hog keeps its own cumulative series
  local arm="$1"
  rm -f "$OUT/$arm".hog.*.out
  for i in $(seq 1 "$NHOG"); do
    "$ROOT/build/runext" taskset -c "$CPUSET" "$ROOT/build/hog" "$OUT/$arm.hog.$i.out" &
    echo $! >> /tmp/b2hogs.pids
  done
}
stop_hogs() {
  [ -f /tmp/b2hogs.pids ] && xargs -r kill 2>/dev/null < /tmp/b2hogs.pids
  rm -f /tmp/b2hogs.pids; sleep 1
}

run_arm() {
  local name="$1" hook="$2" initial="$3"
  echo "════════ $name ════════"
  setslot "$initial"
  stop_hogs; start_hogs "$name"; sleep 2
  "$VENV/bin/python" "$ROOT/bench/ttft_client.py" --model "$MODEL" --label "$name" \
      --steady-sec "$SEC" --burst-sec "$SEC" --warmup-sec 4 \
      ${hook:+--on-phase-cmd "$hook"} --json-out "$OUT/$name.json" > /dev/null 2>&1
  stop_hogs
  echo "  done"
}

SW="$ROOT/scripts/phase_switch.sh {phase}"
run_arm static_latency    ""    0
run_arm static_throughput ""    1
run_arm akts_switching    "$SW" 1
setslot 0
echo "all arms complete -> $OUT"
