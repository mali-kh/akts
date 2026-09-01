#!/usr/bin/env python3
"""Decision validity and quality vs model size, unguided and with guided choice."""
import collections, json, sys, time, urllib.request

MODEL = sys.argv[1]
URL = "http://127.0.0.1:8000/v1/chat/completions"
N = 20
CASES = [("high", 18.4, 0.82, "0"), ("low", 2.1, 0.35, "1")]
SYS = "You are a policy router. Valid answers are exactly 0 or 1. Output only that digit."
USER = ("Telemetry: p99_latency_ms={p}, cpu_util={u}.\n"
        "Option 0: latency-boost policy (use when load or latency is high).\n"
        "Option 1: throughput policy (use when load is low).\nAnswer (0 or 1):")

def call(p, u, guided):
    body = {"model": MODEL, "max_tokens": 3, "temperature": 0.0,
            "messages": [{"role": "system", "content": SYS},
                         {"role": "user", "content": USER.format(p=p, u=u)}]}
    if guided: body["guided_choice"] = ["0", "1"]
    req = urllib.request.Request(URL, json.dumps(body).encode(),
                                 {"Content-Type": "application/json"})
    t0 = time.perf_counter()
    with urllib.request.urlopen(req, timeout=60) as f:
        d = json.load(f)
    return (time.perf_counter()-t0)*1000, d["choices"][0]["message"]["content"].strip()

res = {"model": MODEL}
for guided in (False, True):
    valid = correct = 0; lats = []
    outs = collections.Counter()
    for name, p, u, exp in CASES:
        for _ in range(N):
            ms, txt = call(p, u, guided)
            lats.append(ms); outs[f"{name}:{txt}"] += 1
            if txt in ("0","1"): valid += 1
            if txt == exp: correct += 1
    lats.sort()
    key = "guided" if guided else "unguided"
    res[key] = {"valid": f"{valid}/{2*N}", "correct": f"{correct}/{2*N}",
                "p50_ms": round(lats[len(lats)//2],1), "outputs": dict(outs)}
print(json.dumps(res))
