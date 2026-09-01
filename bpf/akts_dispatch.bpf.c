#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
char _license[] SEC("license") = "GPL";

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

SEC("struct_ops/select_cpu")
int akts_dispatch(unsigned long long *ctx)
{
	__u32 key = 0;
	__u32 *slot = bpf_map_lookup_elem(&active_slot_map, &key);
	if (slot)
		bpf_tail_call(ctx, &jmp_table, *slot);
	return 0;
}

SEC("struct_ops/select_cpu")
int akts_policy_lat(unsigned long long *ctx) { return 0; }

/* main ops: the dispatcher */
SEC(".struct_ops.link")
struct sched_ext_ops akts_ops = {
	.select_cpu = (void *)akts_dispatch,
	.name       = "akts_disp",
};

/* second ops map purely to satisfy libbpf so the policy prog gets loaded.
   Never registered -- it exists only to give akts_policy_lat the same
   prog_type + attach_btf_id as the dispatcher. */
SEC(".struct_ops.link")
struct sched_ext_ops akts_ops_lib = {
	.select_cpu = (void *)akts_policy_lat,
	.name       = "akts_lib",
};
