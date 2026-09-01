# AKTS -- build eBPF probes and the actuation microbenchmark.
#
# Requires: clang, gcc, bpftool, and a kernel with BTF (>= 6.12 for sched_ext).
# libbpf's BPF-side headers are vendored into include/bpf at setup time so no
# libbpf-dev package is needed.

ARCH        := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')
BUILD       := build
LIBBPF_VER  := v1.4.5
LIBBPF_RAW  := https://raw.githubusercontent.com/libbpf/libbpf/$(LIBBPF_VER)/src
HDRS        := bpf_helpers.h bpf_helper_defs.h bpf_tracing.h bpf_core_read.h bpf_endian.h

BPF_SRCS    := $(wildcard bpf/*.bpf.c)
BPF_OBJS    := $(patsubst bpf/%.bpf.c,$(BUILD)/%.bpf.o,$(BPF_SRCS))

CLANG_FLAGS := -target bpf -D__TARGET_ARCH_$(ARCH) -O2 -g \
               -Wno-missing-declarations -Wno-compare-distinct-pointer-types \
               -I include -I $(BUILD)

.PHONY: all setup clean
all: setup $(BPF_OBJS) $(BUILD)/bench_swap $(BUILD)/gate3_loader

setup: include/bpf/bpf_helpers.h $(BUILD)/vmlinux.h

include/bpf/bpf_helpers.h:
	@mkdir -p include/bpf
	@for h in $(HDRS); do curl -sSL -o include/bpf/$$h $(LIBBPF_RAW)/$$h; done
	@echo "vendored libbpf $(LIBBPF_VER) headers -> include/bpf"

# Ubuntu's /usr/sbin/bpftool is a wrapper that only works when a matching
# linux-tools-$(uname -r) package exists. Fall back to any versioned binary --
# BTF dumping reads /sys/kernel/btf/vmlinux at runtime and is not tied to the
# bpftool build's own kernel version.
$(BUILD)/vmlinux.h:
	@mkdir -p $(BUILD)
	@for bt in bpftool $$(ls /usr/lib/linux-tools/*/bpftool 2>/dev/null); do \
		if $$bt btf dump file /sys/kernel/btf/vmlinux format c > $@.tmp 2>/dev/null && [ -s $@.tmp ]; then \
			mv $@.tmp $@; echo "vmlinux.h: $$(wc -l < $@) lines (via $$bt)"; exit 0; \
		fi; \
	done; \
	rm -f $@.tmp; \
	echo "ERROR: no working bpftool could dump BTF. Try: apt install linux-tools-$$(uname -r)"; \
	exit 1

$(BUILD)/%.bpf.o: bpf/%.bpf.c | $(BUILD)/vmlinux.h
	clang $(CLANG_FLAGS) -c $< -o $@

$(BUILD)/bench_swap: bench/bench_swap.c
	@mkdir -p $(BUILD)
	gcc -O2 -o $@ $<

# gate 3 loader needs libbpf proper (userspace), unlike the rest of the tree
$(BUILD)/gate3_loader: src/gate3_loader.c
	@mkdir -p $(BUILD)
	gcc -O2 -o $@ $< -lbpf -lpthread

clean:
	rm -rf $(BUILD)
