#!/bin/bash
# Phase A: 7 repetitions of all three Benchmark 2 arms.
# Phase B: aggregate.
# Phase C: decision-validity sweep across model sizes, then restore 0.5B server.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENVPY=/home/ubuntu/vllmenv/bin/python
log() { echo "[$(date +%H:%M:%S)] $*"; }

pgrep -f akts_b2_loader >/dev/null || { cd "$ROOT" && nohup ./build/akts_b2_loader >/tmp/b2.log 2>&1 & sleep 2; }

for i in 1 2 3 4 5 6 7; do
  log "=== repetition $i ==="
  OUT=/tmp/bench2_rep$i "$ROOT/scripts/run_bench2.sh" >/dev/null 2>&1
done
log "aggregating"
$VENVPY "$ROOT/bench/aggregate_b2.py" /tmp/bench2_rep | tee /home/ubuntu/b2_agg.txt

log "=== model sweep ==="
: > /home/ubuntu/model_sweep.jsonl
for M in Qwen/Qwen2.5-0.5B-Instruct Qwen/Qwen2.5-1.5B-Instruct Qwen/Qwen2.5-3B-Instruct; do
  log "starting $M"
  MODEL=$M "$ROOT/scripts/start_vllm.sh" >/dev/null 2>&1 || { log "FAILED to start $M"; continue; }
  $VENVPY "$ROOT/bench/agent_sweep.py" "$M" >> /home/ubuntu/model_sweep.jsonl 2>>/home/ubuntu/sweep_err.log
  log "done $M"
done
log "restoring 0.5B server"
MODEL=Qwen/Qwen2.5-0.5B-Instruct "$ROOT/scripts/start_vllm.sh" >/dev/null 2>&1
log "ALL DONE"
