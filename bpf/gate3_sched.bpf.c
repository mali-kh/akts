/* Gate 3 on sched_ext proper: does the tail call dispatch from an attached
 * sched_ext scheduler?
 *
 * SCX_OPS_SWITCH_PARTIAL means only tasks explicitly moved to SCHED_EXT are
 * managed here; everything else stays on the default scheduler, so a mistake
 * cannot make the machine unschedulable.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

#define C_DISPATCH 0
#define C_POLICY   1
#define C_FALLTHRU 2

/* kfuncs */
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

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 4);
	__type(key, __u32);
	__type(value, __u64);
} counters SEC(".maps");

static __always_inline void bump(__u32 idx)
{
	__u64 *c = bpf_map_lookup_elem(&counters, &idx);
	if (c)
		__sync_fetch_and_add(c, 1);
}

/* ---- dispatcher under test ---- */
SEC("struct_ops/select_cpu")
int akts_select_cpu(unsigned long long *ctx)
{
	struct task_struct *p = (struct task_struct *)ctx[0];
	s32 prev_cpu = (s32)ctx[1];
	u64 wake_flags = ctx[2];
	bool is_idle = false;
	__u32 key = 0, *slot;

	bump(C_DISPATCH);

	slot = bpf_map_lookup_elem(&active_slot_map, &key);
	if (slot)
		bpf_tail_call(ctx, &jmp_table, *slot);

	bump(C_FALLTHRU);
	return scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
}

/* ---- slot 0: policy module ---- */
SEC("struct_ops/select_cpu")
int akts_policy(unsigned long long *ctx)
{
	struct task_struct *p = (struct task_struct *)ctx[0];
	s32 prev_cpu = (s32)ctx[1];
	u64 wake_flags = ctx[2];
	bool is_idle = false;

	bump(C_POLICY);
	return scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
}

SEC("struct_ops/enqueue")
void akts_enqueue(unsigned long long *ctx)
{
	struct task_struct *p = (struct task_struct *)ctx[0];
	u64 enq_flags = ctx[1];

	scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
}

SEC(".struct_ops.link")
struct sched_ext_ops akts_sched_ops = {
	.select_cpu = (void *)akts_select_cpu,
	.enqueue    = (void *)akts_enqueue,
	.flags      = SCX_OPS_SWITCH_PARTIAL,
	.timeout_ms = 5000,
	.name       = "akts_g3",
};

/* loaded only so akts_policy is not rejected as unreferenced; never attached */
SEC(".struct_ops.link")
struct sched_ext_ops akts_sched_lib = {
	.select_cpu = (void *)akts_policy,
	.enqueue    = (void *)akts_enqueue,
	.flags      = SCX_OPS_SWITCH_PARTIAL,
	.timeout_ms = 5000,
	.name       = "akts_lib",
};
