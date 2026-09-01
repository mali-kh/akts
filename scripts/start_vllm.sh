#!/bin/bash
# Start the vLLM server under the AKTS scheduler, pinned to a cpuset.
# Kept in a script so the process pattern never appears in a caller's argv --
# pkill -f on an inline command will match the invoking shell itself.
set -u
VENV=/home/ubuntu/vllmenv
CU=$VENV/lib/python3.12/site-packages/nvidia/cu13
MODEL="${MODEL:-Qwen/Qwen2.5-0.5B-Instruct}"
CPUSET="${CPUSET:-0-3}"
PORT="${PORT:-8000}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

stop() {
  [ -f /tmp/vllm.pid ] && kill "$(cat /tmp/vllm.pid)" 2>/dev/null
  pkill -f "$VENV/bin/vllm" 2>/dev/null
  sleep 2
  pkill -9 -f "$VENV/bin/vllm" 2>/dev/null
  rm -f /tmp/vllm.pid
}

case "${1:-start}" in
stop) stop; echo "vllm stopped"; exit 0;;
esac

stop
export CUDA_HOME="$CU"
export PATH="$CU/bin:$PATH"
export VLLM_USE_FLASHINFER_SAMPLER=0
export VLLM_ATTENTION_BACKEND=FLASH_ATTN
export HF_HUB_DISABLE_PROGRESS_BARS=1

nohup "$ROOT/build/runext" taskset -c "$CPUSET" "$VENV/bin/vllm" serve "$MODEL" \
      --port "$PORT" --gpu-memory-utilization 0.30 --max-model-len 2048 \
      > /tmp/vllm.log 2>&1 &
echo $! > /tmp/vllm.pid
echo "launched model=$MODEL cpuset=$CPUSET pid=$(cat /tmp/vllm.pid)"

for i in $(seq 1 120); do
  if [ "$(curl -s -m 3 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/health" 2>/dev/null)" = "200" ]; then
    echo "READY after ~$((i*5))s"; exit 0
  fi
  pgrep -f "$VENV/bin/vllm" >/dev/null || { echo "PROCESS DIED"; tail -5 /tmp/vllm.log; exit 1; }
  sleep 5
done
echo "TIMEOUT waiting for readiness"; tail -5 /tmp/vllm.log; exit 1
