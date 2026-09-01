/* Probe B (control): same tail call in XDP, which is known to support it. */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 8);
	__type(key, __u32);
	__type(value, __u32);
} jmp_table SEC(".maps");

SEC("xdp")
int xdp_dispatch(struct xdp_md *ctx)
{
	bpf_tail_call(ctx, &jmp_table, 0);
	return XDP_PASS;
}
