# Repeated runs and model sweep: 2026-09-01, pre-submission

## Benchmark 2, seven repetitions per arm (mean +- std)

| metric | static latency | static throughput | AKTS switching |
|---|---|---|---|
| burst TTFT p50 (ms) | 225.8 +- 9.5 | 358.0 +- 4.0 | 229.0 +- 4.6 |
| burst TTFT p99 (ms) | 393.7 +- 43.0 | 411.4 +- 17.0 | 371.4 +- 28.0 |
| burst requests | 1132 +- 34 | 738 +- 13 | 1129 +- 26 |
| total batch work (Mit) | 1.789 +- 0.006 | 1.966 +- 0.018 | 1.912 +- 0.007 |

With repetitions the single-run reading "switching marginally worst on p99"
does not hold; the three arms are within run-to-run variance. All headline
claims survive: burst p50 parity with the latency policy, 97% of the
throughput policy's batch work.

## Decision routing vs model scale (n=40 per model, temperature 0)

| model | valid | correct | guided correct | p50 |
|---|---|---|---|---|
| Qwen2.5-0.5B | 40/40 | 20/40 | 20/40 | 13.3 ms |
| Qwen2.5-1.5B | 40/40 | 20/40 | 20/40 | 17.2 ms |
| Qwen2.5-3B | 40/40 | 20/40 | 20/40 | 23.9 ms |

Every model emits a constant index regardless of telemetry (0.5B always "1",
the larger two always "0"), which is chance accuracy on a two-regime task.
Constraining decoding to {0,1} changes nothing. Validity was prompt-dependent
in the earlier probe; with a careful prompt validity is 100% and the failure
is the decision itself.
