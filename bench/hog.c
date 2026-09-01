/* CPU-contention antagonist that accounts for the work it completes.
 *
 * Benchmark 2 has two competing objectives: vLLM request latency and batch
 * throughput. Measuring only latency makes the latency policy trivially best,
 * so the antagonist reports progress and its work rate becomes the second axis.
 *
 * Appends "<unix_ms> <iterations>" every 100ms; the driver slices this by phase.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

static long long ms_now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/tmp/hog.out";
	FILE *f = fopen(path, "w");
	unsigned long long iters = 0;
	long long next;
	volatile unsigned long x = 0;

	signal(SIGTERM, on_sig);
	signal(SIGINT, on_sig);
	if (!f) { perror("open"); return 1; }
	setvbuf(f, NULL, _IOLBF, 0);
	next = ms_now() + 100;

	while (!stop) {
		for (int i = 0; i < 20000; i++) x += i;
		iters++;
		if (ms_now() >= next) {
			fprintf(f, "%lld %llu\n", ms_now(), iters);
			next += 100;
		}
	}
	fprintf(f, "%lld %llu\n", ms_now(), iters);
	fclose(f);
	return 0;
}
