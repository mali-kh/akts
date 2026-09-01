/* Gate 3 loader: registers the probe congestion-control algorithm, drives a
 * loopback transfer through it, and reports which path the tail call took.
 *
 * The struct_ops link is owned by this process, so if it dies the algorithm is
 * unregistered automatically -- nothing is left attached to the system.
 */
#define _GNU_SOURCE
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#define CC_NAME "akts_probe"
#define XFER    (48 * 1024 * 1024)

static int quiet_log(enum libbpf_print_level l, const char *f, va_list a)
{
	if (l == LIBBPF_WARN || l == LIBBPF_INFO) return 0;
	return vfprintf(stderr, f, a);
}

static int srv_port;

static void *server(void *unused)
{
	int ls = socket(AF_INET, SOCK_STREAM, 0), cs;
	int one = 1;
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = 0 };
	socklen_t alen = sizeof(a);
	char *buf = malloc(1 << 16);

	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	bind(ls, (struct sockaddr *)&a, sizeof(a));
	getsockname(ls, (struct sockaddr *)&a, &alen);
	srv_port = ntohs(a.sin_port);
	listen(ls, 1);
	__atomic_store_n(&srv_port, srv_port, __ATOMIC_SEQ_CST);

	cs = accept(ls, NULL, NULL);
	while (read(cs, buf, 1 << 16) > 0) ;
	close(cs); close(ls); free(buf);
	return NULL;
}

/* returns bytes sent, or -1 if the CC algorithm could not be selected */
static long drive(void)
{
	int s = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = { .sin_family = AF_INET };
	char *buf = calloc(1, 1 << 16);
	long sent = 0;

	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = htons(srv_port);

	if (setsockopt(s, IPPROTO_TCP, TCP_CONGESTION, CC_NAME, strlen(CC_NAME))) {
		fprintf(stderr, "setsockopt(TCP_CONGESTION, %s): %s\n", CC_NAME, strerror(errno));
		close(s); free(buf); return -1;
	}
	if (connect(s, (struct sockaddr *)&a, sizeof(a))) {
		perror("connect"); close(s); free(buf); return -1;
	}
	while (sent < XFER) {
		ssize_t n = write(s, buf, 1 << 16);
		if (n <= 0) break;
		sent += n;
	}
	close(s); free(buf);
	return sent;
}

static void counters(int fd, __u64 *out)
{
	for (__u32 k = 0; k < 3; k++)
		if (bpf_map_lookup_elem(fd, &k, &out[k])) out[k] = 0;
}

int main(int argc, char **argv)
{
	struct bpf_object *obj;
	struct bpf_map *jmp, *slotm, *cntm, *opsm;
	struct bpf_program *pol;
	struct bpf_link *link;
	pthread_t th;
	__u64 before[3], after[3];
	__u32 key = 0, slot;
	int pfd, err;

	/* slot index to install: 0 = populated policy, anything else = empty slot */
	slot = (argc > 1) ? (__u32)atoi(argv[1]) : 0;

	libbpf_set_print(quiet_log);

	obj = bpf_object__open_file("build/gate3_tcp.bpf.o", NULL);
	if (!obj) { fprintf(stderr, "open failed\n"); return 1; }
	if ((err = bpf_object__load(obj))) {
		fprintf(stderr, "load failed: %d (%s)\n", err, strerror(-err));
		return 1;
	}

	jmp   = bpf_object__find_map_by_name(obj, "jmp_table");
	slotm = bpf_object__find_map_by_name(obj, "active_slot_map");
	cntm  = bpf_object__find_map_by_name(obj, "counters");
	opsm  = bpf_object__find_map_by_name(obj, "akts_ca_ops");
	pol   = bpf_object__find_program_by_name(obj, "akts_policy");
	if (!jmp || !slotm || !cntm || !opsm || !pol) {
		fprintf(stderr, "missing map or program\n"); return 1;
	}

	/* populate slot 0 with the policy module */
	pfd = bpf_program__fd(pol);
	if (bpf_map_update_elem(bpf_map__fd(jmp), &key, &pfd, BPF_ANY)) {
		perror("populate jmp_table"); return 1;
	}
	/* choose which slot the dispatcher will jump to */
	if (bpf_map_update_elem(bpf_map__fd(slotm), &key, &slot, BPF_ANY)) {
		perror("set active slot"); return 1;
	}

	link = bpf_map__attach_struct_ops(opsm);
	if (!link) { fprintf(stderr, "attach struct_ops failed: %s\n", strerror(errno)); return 1; }
	printf("registered congestion control '%s'; active slot = %u\n", CC_NAME, slot);

	pthread_create(&th, NULL, server, NULL);
	while (!__atomic_load_n(&srv_port, __ATOMIC_SEQ_CST)) ;

	counters(bpf_map__fd(cntm), before);
	long sent = drive();
	counters(bpf_map__fd(cntm), after);
	pthread_join(th, NULL);

	if (sent < 0) { bpf_link__destroy(link); return 1; }
	printf("transferred %ld MiB over loopback\n\n", sent >> 20);

	__u64 d = after[0] - before[0], p = after[1] - before[1], f = after[2] - before[2];
	printf("  dispatcher entered      : %llu\n", (unsigned long long)d);
	printf("  policy module entered   : %llu\n", (unsigned long long)p);
	printf("  fell through tail call  : %llu\n\n", (unsigned long long)f);

	if (!d)
		printf("VERDICT: dispatcher never ran -- the CC algorithm was not exercised.\n");
	else if (p && !f)
		printf("VERDICT: TAIL CALL DISPATCHED. Control transferred to the policy module.\n");
	else if (!p && f)
		printf("VERDICT: TAIL CALL FELL THROUGH. Control never reached the policy module.\n");
	else
		printf("VERDICT: MIXED (%llu dispatched, %llu fell through).\n",
		       (unsigned long long)p, (unsigned long long)f);

	bpf_link__destroy(link);
	bpf_object__close(obj);
	return 0;
}
