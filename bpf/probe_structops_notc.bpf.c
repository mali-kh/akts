/* Probe A: does bpf_tail_call verify inside a sched_ext struct_ops program? */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

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

/* raw struct_ops signature so we get a real ctx pointer for the tail call */
SEC("struct_ops/select_cpu")
int akts_select_cpu(unsigned long long *ctx)
{
	__u32 key = 0;
	__u32 *slot = bpf_map_lookup_elem(&active_slot_map, &key);

	if (slot)
		(void)slot; /* tail call removed */
	return 0;
}

SEC(".struct_ops.link")
struct sched_ext_ops akts_ops = {
	.select_cpu = (void *)akts_select_cpu,
	.name       = "akts_ctrl",
};
