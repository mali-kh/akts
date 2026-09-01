#!/bin/bash
# CPU-contention antagonist: N busy processes pinned to the same cores vLLM
# runs on, so they actually compete for CPU rather than landing on idle ones.
#   antagonist.sh start <cpuset> <n>   e.g. antagonist.sh start 0-3 8
#   antagonist.sh stop
set -u
case "${1:-}" in
  start)
    CPUS="${2:?cpuset}"; N="${3:-8}"
    for i in $(seq 1 "$N"); do
      ${RUNEXT:+$RUNEXT} taskset -c "$CPUS" sh -c 'while :; do :; done' &
      echo $! >> /tmp/akts_antagonist.pids
    done
    echo "started $N hogs on CPUs $CPUS"
    ;;
  stop)
    [ -f /tmp/akts_antagonist.pids ] && xargs -r kill 2>/dev/null < /tmp/akts_antagonist.pids
    rm -f /tmp/akts_antagonist.pids
    echo "antagonist stopped"
    ;;
  *) echo "usage: $0 start <cpuset> <n> | stop"; exit 1;;
esac
