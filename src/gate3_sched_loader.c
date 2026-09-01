/* Gate 3 on sched_ext: attach the AKTS dispatcher as a real scheduler, move
 * worker tasks into SCHED_EXT, and report which path the tail call took.
 *
 * Safety: the scheduler is attached with SCX_OPS_SWITCH_PARTIAL, so only the
 * workers below are managed by it. The struct_ops link is owned by this
 * process; if it exits for any reason the scheduler is detached.
 */
#define _GNU_SOURCE
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/wait.h>
#include <time.h>

#ifndef SCHED_EXT
#define SCHED_EXT 7
#endif

#define NWORKERS 8
#define RUN_SEC  5

static int quiet_log(enum libbpf_print_level l, const char *f, va_list a)
{
	if (l == LIBBPF_WARN || l == LIBBPF_INFO) return 0;
	return vfprintf(stderr, f, a);
}

static void counters(int fd, __u64 *out)
{
	for (__u32 k = 0; k < 3; k++)
		if (bpf_map_lookup_elem(fd, &k, &out[k])) out[k] = 0;
}

/* busy work that yields often, so select_cpu is exercised on every wakeup */
static void worker(void)
{
	struct sched_param prm = { .sched_priority = 0 };
	struct timespec end, now, nap = { .tv_sec = 0, .tv_nsec = 200000 };
	volatile unsigned long x = 0;

	if (sched_setscheduler(0, SCHED_EXT, &prm)) {
		fprintf(stderr, "worker: sched_setscheduler(SCHED_EXT): %s\n", strerror(errno));
		_exit(2);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	end.tv_sec += RUN_SEC;
	for (;;) {
		for (int i = 0; i < 200000; i++) x += i;
		nanosleep(&nap, NULL);          /* sleep -> wake -> select_cpu */
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (now.tv_sec > end.tv_sec) break;
	}
	_exit(0);
}

int main(int argc, char **argv)
{
	struct bpf_object *obj;
	struct bpf_map *jmp, *slotm, *cntm, *opsm;
	struct bpf_program *pol;
	struct bpf_link *link;
	__u64 before[3], after[3];
	__u32 key = 0, slot;
	int pfd, err, ok = 0, bad = 0;
	pid_t kids[NWORKERS];

	slot = (argc > 1) ? (__u32)atoi(argv[1]) : 0;
	libbpf_set_print(quiet_log);

	obj = bpf_object__open_file("build/gate3_sched.bpf.o", NULL);
	if (!obj) { fprintf(stderr, "open failed\n"); return 1; }
	if ((err = bpf_object__load(obj))) {
		fprintf(stderr, "load failed: %d (%s)\n", err, strerror(-err));
		return 1;
	}

	jmp   = bpf_object__find_map_by_name(obj, "jmp_table");
	slotm = bpf_object__find_map_by_name(obj, "active_slot_map");
	cntm  = bpf_object__find_map_by_name(obj, "counters");
	opsm  = bpf_object__find_map_by_name(obj, "akts_sched_ops");
	pol   = bpf_object__find_program_by_name(obj, "akts_policy");
	if (!jmp || !slotm || !cntm || !opsm || !pol) {
		fprintf(stderr, "missing map or program\n"); return 1;
	}

	pfd = bpf_program__fd(pol);
	if (bpf_map_update_elem(bpf_map__fd(jmp), &key, &pfd, BPF_ANY)) {
		perror("populate jmp_table"); return 1;
	}
	if (bpf_map_update_elem(bpf_map__fd(slotm), &key, &slot, BPF_ANY)) {
		perror("set active slot"); return 1;
	}

	link = bpf_map__attach_struct_ops(opsm);
	if (!link) {
		fprintf(stderr, "attach sched_ext failed: %s\n", strerror(errno));
		return 1;
	}
	printf("sched_ext scheduler 'akts_g3' ATTACHED (partial mode); active slot = %u\n", slot);
	fflush(stdout);

	counters(bpf_map__fd(cntm), before);

	for (int i = 0; i < NWORKERS; i++) {
		kids[i] = fork();
		if (kids[i] == 0) worker();
	}
	for (int i = 0; i < NWORKERS; i++) {
		int st; waitpid(kids[i], &st, 0);
		if (WIFEXITED(st) && WEXITSTATUS(st) == 0) ok++; else bad++;
	}

	counters(bpf_map__fd(cntm), after);

	printf("workers: %d completed, %d failed to enter SCHED_EXT\n\n", ok, bad);
	__u64 d = after[0]-before[0], p = after[1]-before[1], f = after[2]-before[2];
	printf("  dispatcher entered      : %llu\n", (unsigned long long)d);
	printf("  policy module entered   : %llu\n", (unsigned long long)p);
	printf("  fell through tail call  : %llu\n\n", (unsigned long long)f);

	if (!d)       printf("VERDICT: dispatcher never ran.\n");
	else if (p && !f) printf("VERDICT: TAIL CALL DISPATCHED on an attached sched_ext scheduler.\n");
	else if (!p && f) printf("VERDICT: TAIL CALL FELL THROUGH.\n");
	else          printf("VERDICT: MIXED (%llu dispatched, %llu fell through).\n",
			     (unsigned long long)p, (unsigned long long)f);

	bpf_link__destroy(link);
	printf("scheduler detached; sched_ext state now: ");
	fflush(stdout);
	(void)system("cat /sys/kernel/sched_ext/state");
	bpf_object__close(obj);
	return 0;
}
