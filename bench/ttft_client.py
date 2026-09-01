#!/usr/bin/env python3
"""Measure time-to-first-token against a vLLM OpenAI-compatible server.

TTFT rather than end-to-end latency: end-to-end is dominated by GPU decode time,
which CPU scheduling does not control, and it would wash out the effect under
test. TTFT covers request handling, tokenisation and prefill scheduling -- the
CPU-side path that contention actually delays.

Workload has three phases so a burst can be compared against steady state:
  steady -> burst (high concurrency) -> steady
"""
import argparse, asyncio, json, os, statistics, subprocess, sys, time
import aiohttp

PROMPT = "Summarise the following in one sentence: " + ("system software " * 40)


async def one_request(session, url, model, phase, out):
    body = {"model": model, "prompt": PROMPT, "max_tokens": 24,
            "stream": True, "temperature": 0.0}
    t0 = time.perf_counter()
    try:
        async with session.post(url, json=body) as r:
            if r.status != 200:
                out.append((phase, None, await r.text()))
                return
            async for raw in r.content:
                if raw.startswith(b"data:") and b"[DONE]" not in raw:
                    out.append((phase, (time.perf_counter() - t0) * 1000.0, None))
                    return
            out.append((phase, None, "no token"))
    except Exception as e:                                    # noqa: BLE001
        out.append((phase, None, repr(e)))


async def phase(session, url, model, name, concurrency, seconds, out, hook=None,
                marks=None):
    """Hold `concurrency` requests in flight for `seconds`."""
    if hook:
        subprocess.run(hook.replace("{phase}", name), shell=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if marks is not None:
        marks.append({"phase": name, "start_ms": time.time() * 1000})
    end = time.perf_counter() + seconds
    inflight = set()
    while time.perf_counter() < end:
        while len(inflight) < concurrency:
            inflight.add(asyncio.create_task(
                one_request(session, url, model, name, out)))
        done, inflight = await asyncio.wait(
            inflight, return_when=asyncio.FIRST_COMPLETED)
    if inflight:
        await asyncio.wait(inflight)


def pct(xs, p):
    if not xs:
        return float("nan")
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(len(xs) * p))]


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8000/v1/completions")
    ap.add_argument("--model", required=True)
    ap.add_argument("--steady-conc", type=int, default=2)
    ap.add_argument("--burst-conc", type=int, default=32)
    ap.add_argument("--steady-sec", type=float, default=8)
    ap.add_argument("--burst-sec", type=float, default=8)
    ap.add_argument("--warmup-sec", type=float, default=5)
    ap.add_argument("--label", default="run")
    ap.add_argument("--json-out")
    ap.add_argument("--on-phase-cmd",
                    help="shell command run at each phase start; {phase} is substituted")
    a = ap.parse_args()

    out, marks = [], []
    timeout = aiohttp.ClientTimeout(total=300)
    h = a.on_phase_cmd
    async with aiohttp.ClientSession(timeout=timeout) as s:
        await phase(s, a.url, a.model, "warmup", a.steady_conc, a.warmup_sec, [], h, marks)
        await phase(s, a.url, a.model, "steady_pre", a.steady_conc, a.steady_sec, out, h, marks)
        await phase(s, a.url, a.model, "burst",     a.burst_conc,  a.burst_sec,  out, h, marks)
        await phase(s, a.url, a.model, "steady_post", a.steady_conc, a.steady_sec, out, h, marks)
        marks.append({"phase": "end", "start_ms": time.time() * 1000})

    errs = [e for _, v, e in out if v is None]
    res = {"label": a.label, "errors": len(errs), "sample_error": errs[0] if errs else None,
           "phase_marks": marks}
    for name in ("steady_pre", "burst", "steady_post"):
        xs = [v for ph, v, e in out if ph == name and v is not None]
        res[name] = {
            "n": len(xs),
            "mean_ms": round(statistics.fmean(xs), 2) if xs else None,
            "p50_ms": round(pct(xs, .50), 2) if xs else None,
            "p95_ms": round(pct(xs, .95), 2) if xs else None,
            "p99_ms": round(pct(xs, .99), 2) if xs else None,
            "max_ms": round(max(xs), 2) if xs else None,
        }
    print(json.dumps(res, indent=2))
    if a.json_out:
        with open(a.json_out, "w") as f:
            json.dump(res, f, indent=2)
    if res["errors"]:
        print(f"WARNING: {res['errors']} failed requests", file=sys.stderr)


if __name__ == "__main__":
    asyncio.run(main())
