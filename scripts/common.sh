# Resolve a working bpftool. Ubuntu's /usr/sbin/bpftool is a wrapper that fails
# when no linux-tools package matches the running kernel; any versioned binary
# works, since the operations here read BTF and maps at runtime.
resolve_bpftool() {
  for bt in bpftool $(ls /usr/lib/linux-tools/*/bpftool 2>/dev/null); do
    if $bt version >/dev/null 2>&1; then echo "$bt"; return 0; fi
  done
  echo "ERROR: no working bpftool found (try: apt install linux-tools-$(uname -r))" >&2
  return 1
}
BPFTOOL="$(resolve_bpftool)" || exit 1
