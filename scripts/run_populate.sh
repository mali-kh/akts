#!/bin/bash
# Gate 2: can a sched_ext struct_ops program be inserted into a PROG_ARRAY?
#
# Prog-array entries must match prog_type AND attach_btf_id. sched_ext programs
# carry an attach_btf_id bound to a specific sched_ext_ops member, so this is
# not implied by gate 1. LOAD + MAP UPDATE only; still never attaches.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; B="$ROOT/build"
echo "sched_ext BEFORE: $(cat /sys/kernel/sched_ext/state)"
rm -rf /sys/fs/bpf/akts; mkdir -p /sys/fs/bpf/akts

echo; echo "=== step 1: load object ==="
bpftool prog loadall "$B/akts_dispatch.bpf.o" /sys/fs/bpf/akts pinmaps /sys/fs/bpf/akts \
  || { echo "load failed"; exit 1; }
ls /sys/fs/bpf/akts

echo; echo "=== step 2: insert policy prog into PROG_ARRAY slot 0  [GATE 2] ==="
if bpftool map update pinned /sys/fs/bpf/akts/jmp_table \
     key 0 0 0 0 value pinned /sys/fs/bpf/akts/akts_policy_lat 2>/tmp/e2; then
  echo "INSERT SUCCEEDED -- struct_ops prog accepted into prog_array"
  bpftool map dump pinned /sys/fs/bpf/akts/jmp_table | head -6
else
  echo "INSERT REJECTED"; cat /tmp/e2
fi

rm -rf /sys/fs/bpf/akts
echo; echo "sched_ext AFTER:  $(cat /sys/kernel/sched_ext/state)"
