/* Move this process into SCHED_EXT, then exec the given command.
 * Threads created afterwards inherit the policy, so launching a server through
 * this puts the whole server under the BPF scheduler while leaving the rest of
 * the system on the default one. */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#ifndef SCHED_EXT
#define SCHED_EXT 7
#endif
int main(int argc, char **argv)
{
	struct sched_param p = { .sched_priority = 0 };
	if (argc < 2) { fprintf(stderr, "usage: runext CMD [ARGS...]\n"); return 1; }
	if (sched_setscheduler(0, SCHED_EXT, &p)) {
		fprintf(stderr, "runext: SCHED_EXT: %s (is a sched_ext scheduler attached?)\n",
			strerror(errno));
		return 1;
	}
	execvp(argv[1], &argv[1]);
	perror("execvp");
	return 1;
}
