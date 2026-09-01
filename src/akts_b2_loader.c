/* Benchmark 2 loader: attach the AKTS scheduler and stay resident.
 *
 * Pins active_slot_map and counters so an external agent (or the experiment
 * driver) can switch policy with a single integer write while the workload runs.
 * Exits cleanly on SIGINT/SIGTERM, detaching the scheduler.
 */
#define _GNU_SOURCE
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>

#define PINDIR "/sys/fs/bpf/akts_b2"

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

static int quiet_log(enum libbpf_print_level l, const char *f, va_list a)
{
	if (l == LIBBPF_WARN || l == LIBBPF_INFO) return 0;
	return vfprintf(stderr, f, a);
}

int main(void)
{
	struct bpf_object *obj;
	struct bpf_map *jmp, *slotm, *cntm, *opsm;
	struct bpf_program *lat, *thr;
	struct bpf_link *link;
	__u32 k, fd0, fd1, init = 0;
	int err;

	libbpf_set_print(quiet_log);
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	obj = bpf_object__open_file("build/akts_b2.bpf.o", NULL);
	if (!obj) { fprintf(stderr, "open failed\n"); return 1; }
	if ((err = bpf_object__load(obj))) {
		fprintf(stderr, "load failed: %d (%s)\n", err, strerror(-err)); return 1;
	}

	jmp   = bpf_object__find_map_by_name(obj, "jmp_table");
	slotm = bpf_object__find_map_by_name(obj, "active_slot_map");
	cntm  = bpf_object__find_map_by_name(obj, "counters");
	opsm  = bpf_object__find_map_by_name(obj, "akts_b2_ops");
	lat   = bpf_object__find_program_by_name(obj, "akts_policy_latency");
	thr   = bpf_object__find_program_by_name(obj, "akts_policy_throughput");
	if (!jmp || !slotm || !cntm || !opsm || !lat || !thr) {
		fprintf(stderr, "missing map or program\n"); return 1;
	}

	k = 0; fd0 = bpf_program__fd(lat);
	if (bpf_map_update_elem(bpf_map__fd(jmp), &k, &fd0, BPF_ANY)) {
		perror("jmp_table[0]"); return 1; }
	k = 1; fd1 = bpf_program__fd(thr);
	if (bpf_map_update_elem(bpf_map__fd(jmp), &k, &fd1, BPF_ANY)) {
		perror("jmp_table[1]"); return 1; }
	k = 0;
	if (bpf_map_update_elem(bpf_map__fd(slotm), &k, &init, BPF_ANY)) {
		perror("active slot"); return 1; }

	mkdir(PINDIR, 0755);
	if (bpf_map__pin(slotm, PINDIR "/active_slot_map") ||
	    bpf_map__pin(cntm,  PINDIR "/counters")) {
		fprintf(stderr, "pin failed: %s\n", strerror(errno)); return 1;
	}

	link = bpf_map__attach_struct_ops(opsm);
	if (!link) { fprintf(stderr, "attach failed: %s\n", strerror(errno)); return 1; }

	printf("akts_b2 attached; slots: 0=latency 1=throughput; maps pinned at %s\n", PINDIR);
	printf("switch policy with: bpftool map update pinned %s/active_slot_map key 0 0 0 0 value N 0 0 0\n",
	       PINDIR);
	fflush(stdout);

	while (!stop) pause();

	bpf_map__unpin(slotm, PINDIR "/active_slot_map");
	bpf_map__unpin(cntm,  PINDIR "/counters");
	bpf_link__destroy(link);
	bpf_object__close(obj);
	printf("\ndetached\n");
	return 0;
}
