# Raw data

Everything measured on the Lambda A100 instance before it was torn down
(2026-09-01). `environment.txt` records the exact kernel, driver and package
versions.

- `bench2_raw.tar.gz`: all seven Benchmark 2 repetitions; per-arm TTFT JSON
  (with phase timestamps) and per-hog cumulative work series
- `b2_agg.json` / `b2_agg.txt`: aggregated mean and std across repetitions
  (the numbers in the paper's Table 2)
- `b2_summary.json`: the earlier single-run summary, superseded by the above
- `model_sweep.jsonl`: decision validity and latency for Qwen2.5 at 0.5B,
  1.5B and 3B, unguided and with guided choice
- `night_run.log`: timestamped log of the full run
