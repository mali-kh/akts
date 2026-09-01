#!/usr/bin/env python3
"""Aggregate repeated Benchmark 2 runs: mean and std across repetitions."""
import glob, json, math, os, sys

BASE = sys.argv[1] if len(sys.argv) > 1 else "/tmp/bench2_rep"
ARMS = ["static_latency", "static_throughput", "akts_switching"]
PHASES = ["steady_pre", "burst", "steady_post"]

def hog_work(rep_dir, arm, t0, t1):
    total = 0
    for path in glob.glob(os.path.join(rep_dir, f"{arm}.hog.*.out")):
        series = []
        for line in open(path):
            try:
                ts, it = line.split(); series.append((float(ts), int(it)))
            except ValueError: continue
        if not series: continue
        lo = max((it for ts, it in series if ts <= t0), default=min(it for _, it in series))
        hi = max((it for ts, it in series if ts <= t1), default=None)
        if hi is not None: total += max(0, hi - lo)
    return total

def stats(xs):
    n = len(xs); m = sum(xs)/n
    sd = math.sqrt(sum((x-m)**2 for x in xs)/(n-1)) if n > 1 else 0.0
    return m, sd

reps = sorted(glob.glob(BASE + "*"))
out = {}
for arm in ARMS:
    acc = {ph: {"p50": [], "p99": [], "n": []} for ph in PHASES}
    acc["work"] = []
    for rd in reps:
        p = os.path.join(rd, f"{arm}.json")
        if not os.path.exists(p): continue
        d = json.load(open(p))
        marks = {m["phase"]: m["start_ms"] for m in d["phase_marks"]}
        order = [m["phase"] for m in d["phase_marks"]]
        w = 0
        for ph in PHASES:
            acc[ph]["p50"].append(d[ph]["p50_ms"]); acc[ph]["p99"].append(d[ph]["p99_ms"])
            acc[ph]["n"].append(d[ph]["n"])
            nxt = order[order.index(ph)+1]
            w += hog_work(rd, arm, marks[ph], marks[nxt])
        acc["work"].append(w)
    out[arm] = {"reps": len(acc["work"])}
    for ph in PHASES:
        for k in ("p50","p99","n"):
            m, sd = stats(acc[ph][k]); out[arm][f"{ph}.{k}"] = [round(m,1), round(sd,1)]
    m, sd = stats(acc["work"]); out[arm]["work_Mit"] = [round(m/1e6,3), round(sd/1e6,3)]

print(f"{'metric':<24}" + "".join(f"{a:>26}" for a in ARMS))
for key in ["burst.p50","burst.p99","burst.n","steady_pre.p50","steady_post.p50","work_Mit"]:
    row = f"{key:<24}"
    for a in ARMS:
        m, sd = out[a][key]; row += f"{m:>16} ± {sd:<7}"
    print(row)
print(f"repetitions: {[out[a]['reps'] for a in ARMS]}")
json.dump(out, open("/home/ubuntu/b2_agg.json","w"), indent=2)
