# AKTS — Agentic Kernel Tail-Call Stitching

Sub-microsecond kernel policy switching driven by a language-model agent, without
letting the agent write kernel code.

## Idea

An LLM cannot sit in a kernel scheduling path: inference takes 10–1000 ms, while
scheduling decisions happen every 1–10 us. Existing approaches resolve this in one
of two ways, each with a cost:

- **Knob tuning** — the agent writes a scalar to `/proc/sys`. Fast to apply, but
  it can only adjust dials the kernel already exposes; it cannot change an algorithm.
- **Code generation** — the agent writes eBPF C, which is then compiled, verified
  and loaded. Fully expressive, but that path costs seconds to a minute, and
  generated code can fail the verifier.

AKTS takes a third route. A library of policies is compiled and verified **once, at
boot**, and loaded into a `BPF_MAP_TYPE_PROG_ARRAY`. A dispatcher attached to the
kernel hook reads a single integer and `bpf_tail_call`s into whichever policy that
integer names. The agent's entire output is that integer.

Two properties follow:

1. **Unsafe actions are unrepresentable.** The agent never emits code, so no
   hallucination can produce a verifier failure. Every reachable policy was verified
   at load time.
2. **An in-kernel circuit breaker can override the agent.** A telemetry hook watching
   p99 latency can reset the index to a known-good default without waiting for the
   next agent decision.

## Status

All three structural assumptions have been validated on real hardware. Results reproduce on two platforms that differ in kernel version,
CPU vendor and virtualisation — see
[`results/2026-08-31-anrg-4.md`](results/2026-08-31-anrg-4.md) (Linux 7.0, Intel,
bare metal) and
[`results/2026-09-01-lambda-a100.md`](results/2026-09-01-lambda-a100.md)
(Linux 6.14, AMD EPYC, KVM, A100).

| | Question | Status |
|---|---|---|
| Gate 1 | Does `bpf_tail_call` verify inside a sched_ext `struct_ops` program? | **yes** |
| Gate 2 | Can a sched_ext program be inserted into a `PROG_ARRAY`? | **yes** |
| Gate 3 | Does the tail call dispatch at runtime? | **yes** — 64,160/64,160 on an attached `sched_ext` scheduler |

Measured actuation cost is at parity with a `/proc/sys` write, and orders of
magnitude below a recompile-and-reload path. The ratio replicates across both
platforms: 447/533 ns = 0.84 on Intel bare metal, 920/1110 ns = 0.83 on AMD
under KVM.

## Build

Requires `clang`, `gcc`, `bpftool`, and a kernel with BTF. `sched_ext` needs 6.12+;
check with `ls /sys/kernel/sched_ext`. libbpf's BPF-side headers are vendored
automatically, so no `libbpf-dev` package is needed.

    make

## Run

The gate and benchmark scripts **load programs but never attach a scheduler**, so
they are safe on a machine running other workloads. Each asserts that
`/sys/kernel/sched_ext/state` is unchanged on exit.

    sudo ./scripts/run_gates.sh      # gate 1 + two controls
    sudo ./scripts/run_populate.sh   # gate 2
    sudo ./scripts/run_bench.sh      # benchmark 1: actuation latency

## Layout

    bpf/     dispatcher, policy modules, and the isolated verifier probes
    bench/   actuation-latency microbenchmark (raw bpf(2), no libbpf dependency)
    scripts/ runners for the gates and the benchmark
    results/ measurement logs, with hardware and kernel provenance
