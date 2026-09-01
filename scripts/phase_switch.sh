#!/bin/bash
# Oracle policy switcher: latency policy for the burst, throughput otherwise.
. "$(dirname "$0")/common.sh"
case "$1" in
  burst) SLOT=0 ;;   # protect request latency
  *)     SLOT=1 ;;   # give the CPU back to batch work
esac
"$BPFTOOL" map update pinned /sys/fs/bpf/akts_b2/active_slot_map \
   key 0 0 0 0 value $SLOT 0 0 0 2>/dev/null
