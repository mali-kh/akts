/* Actuation-latency microbenchmark: PROG_ARRAY index swap vs sysctl write.
   Raw bpf(2) syscall -- no libbpf needed. */
#define _GNU_SOURCE
#include <linux/bpf.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>

static int bpf(int cmd, union bpf_attr *attr) {
	return syscall(__NR_bpf, cmd, attr, sizeof(*attr));
}
static int obj_get(const char *path) {
	union bpf_attr a; memset(&a, 0, sizeof(a));
	a.pathname = (unsigned long long)path;
	return bpf(BPF_OBJ_GET, &a);
}
static long long ns_now(void) {
	struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}
static int cmp(const void *a, const void *b) {
	long long x = *(const long long *)a, y = *(const long long *)b;
	return (x > y) - (x < y);
}
static void report(const char *label, long long *s, int n) {
	qsort(s, n, sizeof(*s), cmp);
	double mean = 0; for (int i = 0; i < n; i++) mean += s[i];
	printf("%-34s n=%d  mean %7.0f ns | p50 %6lld | p99 %7lld | max %8lld\n",
	       label, n, mean / n, s[n/2], s[(int)(n*0.99)], s[n-1]);
}

#define N 200000
static long long s[N];

int main(void) {
	int mfd = obj_get("/sys/fs/bpf/akts/active_slot_map");
	if (mfd < 0) { perror("obj_get active_slot_map"); return 1; }

	unsigned int key = 0, val = 0;
	union bpf_attr a; memset(&a, 0, sizeof(a));
	a.map_fd = mfd;
	a.key    = (unsigned long long)&key;
	a.value  = (unsigned long long)&val;
	a.flags  = BPF_ANY;

	for (int i = 0; i < 1000; i++) { val = i & 1; bpf(BPF_MAP_UPDATE_ELEM, &a); }

	for (int i = 0; i < N; i++) {
		val = i & 1;
		long long t0 = ns_now();
		bpf(BPF_MAP_UPDATE_ELEM, &a);
		s[i] = ns_now() - t0;
	}
	report("AKTS slot swap (bpf map update)", s, N);

	/* Baseline: userspace scalar knob write, LumOS-style actuation */
	const char *sp = "/proc/sys/kernel/sched_cfs_bandwidth_slice_us";
	int fd = open(sp, O_WRONLY);
	if (fd < 0) { sp = "/proc/sys/kernel/timer_migration"; fd = open(sp, O_WRONLY); }
	if (fd >= 0) {
		char buf[8]; int M = N / 10;
		for (int i = 0; i < M; i++) {
			int n = snprintf(buf, sizeof(buf), "%d", (i & 1) ? 5000 : 5001);
			long long t0 = ns_now();
			lseek(fd, 0, SEEK_SET); ssize_t w = write(fd, buf, n); (void)w;
			s[i] = ns_now() - t0;
		}
		char lbl[96]; snprintf(lbl, sizeof(lbl), "sysctl write (%s)", sp);
		report(lbl, s, M);
		close(fd);
	} else { printf("sysctl baseline: could not open a writable knob\n"); }
	return 0;
}
