#!/bin/bash
# Benchmark 1: actuation latency -- the cost of applying a policy change,
# measured after the decision has already been made.
#
#   AKTS arm      : bpf_map_update_elem on the active-slot map
#   Baseline A arm: a /proc/sys scalar write (LumOS-style knob tuning)
#
# Baseline B (SchedCP-style recompile+reload) is not measured here; it is the
# compile/verify/load path, seconds to a minute, and is not a syscall race.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$(dirname "$0")/common.sh"; B="$ROOT/build"
echo "kernel: $(uname -r)"; echo "cpu: $(lscpu | sed -n 's/^Model name: *//p')"
echo "sched_ext BEFORE: $(cat /sys/kernel/sched_ext/state)"
rm -rf /sys/fs/bpf/akts; mkdir -p /sys/fs/bpf/akts
"$BPFTOOL" prog loadall "$B/akts_dispatch.bpf.o" /sys/fs/bpf/akts pinmaps /sys/fs/bpf/akts 2>/dev/null \
  || { echo "load failed"; exit 1; }
"$BPFTOOL" map update pinned /sys/fs/bpf/akts/jmp_table \
  key 0 0 0 0 value pinned /sys/fs/bpf/akts/akts_policy_lat 2>/dev/null && echo "prog_array populated"

echo; echo "=== ACTUATION LATENCY (nothing attached) ==="
"$B/bench_swap"

rm -rf /sys/fs/bpf/akts
echo; echo "sched_ext AFTER:  $(cat /sys/kernel/sched_ext/state)"
