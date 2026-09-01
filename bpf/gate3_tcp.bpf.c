/* Gate 3: does bpf_tail_call actually DISPATCH at runtime from a struct_ops
 * program, or does it silently fall through?
 *
 * Uses tcp_congestion_ops rather than sched_ext: same struct_ops trampoline
 * mechanism, but triggering it is just a TCP transfer, and a broken program
 * cannot wedge the machine.
 *
 * counters[0] dispatcher entered
 * counters[1] policy module entered   -> tail call DISPATCHED
 * counters[2] code after the tail call -> tail call FELL THROUGH
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

#define C_DISPATCH   0
#define C_POLICY     1
#define C_FALLTHRU   2

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

/* ---- dispatcher: the program under test ---- */
SEC("struct_ops/cong_avoid")
void akts_cong_avoid(unsigned long long *ctx)
{
	__u32 key = 0;
	__u32 *slot;

	bump(C_DISPATCH);

	slot = bpf_map_lookup_elem(&active_slot_map, &key);
	if (slot)
		bpf_tail_call(ctx, &jmp_table, *slot);

	/* reached only if the tail call did not transfer control */
	bump(C_FALLTHRU);
}

/* ---- slot 0: the policy module ---- */
SEC("struct_ops/cong_avoid")
void akts_policy(unsigned long long *ctx)
{
	bump(C_POLICY);
}

/* tcp_congestion_ops requires ssthresh, cong_avoid and undo_cwnd */
SEC("struct_ops/ssthresh")
__u32 akts_ssthresh(unsigned long long *ctx) { return 1024; }

SEC("struct_ops/undo_cwnd")
__u32 akts_undo_cwnd(unsigned long long *ctx) { return 1024; }

SEC(".struct_ops.link")
struct tcp_congestion_ops akts_ca_ops = {
	.ssthresh   = (void *)akts_ssthresh,
	.cong_avoid = (void *)akts_cong_avoid,
	.undo_cwnd  = (void *)akts_undo_cwnd,
	.name       = "akts_probe",
};

/* Second map exists only so libbpf will load akts_policy, which otherwise is
 * rejected as "not referenced anywhere". Never registered. */
SEC(".struct_ops.link")
struct tcp_congestion_ops akts_ca_lib = {
	.ssthresh   = (void *)akts_ssthresh,
	.cong_avoid = (void *)akts_policy,
	.undo_cwnd  = (void *)akts_undo_cwnd,
	.name       = "akts_lib",
};
