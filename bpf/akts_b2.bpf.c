/* Benchmark 2 scheduler: two real policies behind the AKTS tail-call dispatcher.
 *
 * slot 0  latency   -- seek an idle CPU on every wakeup, short slice.
 *                      Good when latency-sensitive work must start promptly.
 * slot 1  throughput-- keep the task on its previous CPU, long slice.
 *                      Good for cache locality; queues up under contention.
 *
 * Both policies are reached only through bpf_tail_call from the dispatcher, so
 * switching policy is a single integer write to active_slot_map.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

#define SLOT_LATENCY    0
#define SLOT_THROUGHPUT 1

#define SLICE_SHORT (1000ULL * 1000)        /* 1ms  */
#define SLICE_LONG  (20ULL * 1000 * 1000)   /* 20ms */

s32 scx_bpf_select_cpu_dfl(struct task_struct *p, s32 prev_cpu, u64 wake_flags,
			   bool *is_idle) __ksym;
void scx_bpf_dsq_insert(struct task_struct *p, u64 dsq_id, u64 slice,
			u64 enq_flags) __ksym;

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 8);
	__type(key, __u32);
	__type(value, __u32);
} jmp_table SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} active_slot_map SEC(".maps");

/* [0] dispatcher [1] latency policy [2] throughput policy [3] fell through */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 8);
	__type(key, __u32);
	__type(value, __u64);
} counters SEC(".maps");

static __always_inline void bump(__u32 i)
{
	__u64 *c = bpf_map_lookup_elem(&counters, &i);
	if (c)
		__sync_fetch_and_add(c, 1);
}

static __always_inline __u32 active_slot(void)
{
	__u32 k = 0, *s = bpf_map_lookup_elem(&active_slot_map, &k);
	return s ? *s : SLOT_LATENCY;
}

/* ---- dispatcher ---- */
SEC("struct_ops/select_cpu")
int akts_select_cpu(unsigned long long *ctx)
{
	struct task_struct *p = (struct task_struct *)ctx[0];
	s32 prev_cpu = (s32)ctx[1];
	u64 wake_flags = ctx[2];
	bool idle = false;
	__u32 k = 0, *slot;

	bump(0);
	slot = bpf_map_lookup_elem(&active_slot_map, &k);
	if (slot)
		bpf_tail_call(ctx, &jmp_table, *slot);

	bump(3);
	return scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &idle);
}

/* ---- slot 0: latency ---- */
SEC("struct_ops/select_cpu")
int akts_policy_latency(unsigned long long *ctx)
{
	struct task_struct *p = (struct task_struct *)ctx[0];
	s32 prev_cpu = (s32)ctx[1];
	u64 wake_flags = ctx[2];
	bool idle = false;
	s32 cpu;

	bump(1);
	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &idle);
	if (idle)   /* start immediately on the idle CPU we found */
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SLICE_SHORT, 0);
	return cpu;
}

/* ---- slot 1: throughput ---- */
SEC("struct_ops/select_cpu")
int akts_policy_throughput(unsigned long long *ctx)
{
	s32 prev_cpu = (s32)ctx[1];

	bump(2);
	return prev_cpu;        /* stay put: cache-friendly, queues under load */
}

SEC("struct_ops/enqueue")
void akts_enqueue(unsigned long long *ctx)
{
	struct task_struct *p = (struct task_struct *)ctx[0];
	u64 enq_flags = ctx[1];
	u64 slice = active_slot() == SLOT_THROUGHPUT ? SLICE_LONG : SLICE_SHORT;

	scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, slice, enq_flags);
}

SEC(".struct_ops.link")
struct sched_ext_ops akts_b2_ops = {
	.select_cpu = (void *)akts_select_cpu,
	.enqueue    = (void *)akts_enqueue,
	.flags      = SCX_OPS_SWITCH_PARTIAL,
	.timeout_ms = 10000,
	.name       = "akts_b2",
};

SEC(".struct_ops.link")
struct sched_ext_ops akts_b2_lib = {
	.select_cpu = (void *)akts_policy_latency,
	.enqueue    = (void *)akts_enqueue,
	.flags      = SCX_OPS_SWITCH_PARTIAL,
	.timeout_ms = 10000,
	.name       = "akts_b2_lib",
};

SEC(".struct_ops.link")
struct sched_ext_ops akts_b2_lib2 = {
	.select_cpu = (void *)akts_policy_throughput,
	.enqueue    = (void *)akts_enqueue,
	.flags      = SCX_OPS_SWITCH_PARTIAL,
	.timeout_ms = 10000,
	.name       = "akts_b2_lib2",
};
