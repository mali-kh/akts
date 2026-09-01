#!/bin/bash
# Gate 1: does bpf_tail_call pass the verifier inside a sched_ext struct_ops program?
#
# LOAD ONLY. Nothing here attaches a scheduler, so a machine running other
# workloads is unaffected. sched_ext state is asserted unchanged at the end.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; B="$ROOT/build"
echo "kernel: $(uname -r)"
echo "sched_ext BEFORE: $(cat /sys/kernel/sched_ext/state)"

try() {
  echo; echo "=== $1 ==="
  rm -f /sys/fs/bpf/akts_test
  if bpftool prog load "$B/$2" /sys/fs/bpf/akts_test 2>/tmp/akts_err; then
    echo "LOAD SUCCEEDED (verifier accepted)"
    bpftool prog show pinned /sys/fs/bpf/akts_test | head -2
  else
    echo "LOAD FAILED"; tail -40 /tmp/akts_err
  fi
  rm -f /sys/fs/bpf/akts_test
}

try "control B: XDP + bpf_tail_call            [expect SUCCESS]" probe_xdp.bpf.o
try "control C: struct_ops, no tail call       [expect SUCCESS]" probe_structops_notc.bpf.o
try "GATE 1:    struct_ops + bpf_tail_call     [the question]"   probe_structops.bpf.o

echo; echo "sched_ext AFTER:  $(cat /sys/kernel/sched_ext/state)  (must read: disabled)"
