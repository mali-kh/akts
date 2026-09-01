#!/usr/bin/env python3
"""Combine Benchmark 2 arms: TTFT per phase and batch work per phase.

Each hog writes its own cumulative iteration count with timestamps, so work in
a phase window is the sum over hogs of (count at window end - count at start).
"""
import glob, json, os, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/bench2"
ARMS = ["static_latency", "static_throughput", "akts_switching"]
PHASES = ["steady_pre", "burst", "steady_post"]


def hog_work(arm, t0, t1):
    total = 0
    for path in glob.glob(os.path.join(OUT, f"{arm}.hog.*.out")):
        series = []
        for line in open(path):
            try:
                ts, it = line.split()
                series.append((float(ts), int(it)))
            except ValueError:
                continue
        if not series:
            continue
        lo = max((it for ts, it in series if ts <= t0), default=None)
        hi = max((it for ts, it in series if ts <= t1), default=None)
        if lo is None:
            lo = min(it for _, it in series)
        if hi is not None:
            total += max(0, hi - lo)
    return total


data = {}
for arm in ARMS:
    p = os.path.join(OUT, f"{arm}.json")
    if not os.path.exists(p):
        print(f"missing {p}"); sys.exit(1)
    d = json.load(open(p))
    marks = {m["phase"]: m["start_ms"] for m in d["phase_marks"]}
    order = [m["phase"] for m in d["phase_marks"]]
    row = {}
    for ph in PHASES:
        nxt = order[order.index(ph) + 1]
        row[ph] = {"ttft": d[ph], "work": hog_work(arm, marks[ph], marks[nxt])}
    row["total_work"] = sum(row[ph]["work"] for ph in PHASES)
    row["errors"] = d["errors"]
    data[arm] = row

W = 22
print(f"\n{'':<{W}}" + "".join(f"{a:>20}" for a in ARMS))
print("-" * (W + 60))
for ph in PHASES:
    print(f"{ph + '  TTFT p99 (ms)':<{W}}" +
          "".join(f"{data[a][ph]['ttft']['p99_ms']:>20.1f}" for a in ARMS))
    print(f"{'  TTFT p50 (ms)':<{W}}" +
          "".join(f"{data[a][ph]['ttft']['p50_ms']:>20.1f}" for a in ARMS))
    print(f"{'  requests':<{W}}" +
          "".join(f"{data[a][ph]['ttft']['n']:>20d}" for a in ARMS))
    print(f"{'  batch work (Mit)':<{W}}" +
          "".join(f"{data[a][ph]['work']/1e6:>20.2f}" for a in ARMS))
    print()
print(f"{'TOTAL batch (Mit)':<{W}}" +
      "".join(f"{data[a]['total_work']/1e6:>20.2f}" for a in ARMS))
print(f"{'errors':<{W}}" + "".join(f"{data[a]['errors']:>20d}" for a in ARMS))
dest = os.environ.get("B2_SUMMARY", os.path.join(OUT, "summary.json"))
try:
    json.dump(data, open(dest, "w"), indent=2)
    print(f"\nsummary written to {dest}")
except OSError as e:
    print(f"\n(could not write summary: {e})")
