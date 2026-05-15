// One-shot: load rawaccel.bpf.o through libbpf and report whether the
// verifier accepts it. Does not attach to any device or populate maps.
// This is the artifact ctest runs to keep the BPF program verifier-clean
// on every commit.

#include <bpf/libbpf.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string g_log;

int log_collect(enum libbpf_print_level level, const char* fmt, va_list ap)
{
    char buf[2048];
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0) g_log.append(buf, std::min<int>(n, sizeof(buf) - 1));
    // Echo to stderr too so an interactive run still sees the verifier log.
    std::fprintf(stderr, "libbpf[%d]: %s", level, buf);
    return 0;
}

void usage()
{
    std::fprintf(stderr, "usage: rawaccel-bpf-verify --object PATH\n");
}

} // namespace

int main(int argc, char** argv)
{
    std::string path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--object" && i + 1 < argc) path = argv[++i];
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { usage(); return 2; }
    }
    if (path.empty()) { usage(); return 2; }

    libbpf_set_print(log_collect);

    bpf_object* obj = bpf_object__open_file(path.c_str(), nullptr);
    if (!obj) {
        std::fprintf(stderr, "bpf_object__open_file(%s): %s\n",
                     path.c_str(), std::strerror(errno));
        return 1;
    }

    // Per-program log buffer: the default is small enough that a non-trivial
    // verifier rejection gets truncated and the actual reason scrolls off.
    // 1 MiB is comfortably more than the kernel will emit. Allocated on the
    // heap so the binary stack stays small.
    static std::vector<char> verifier_log(1 << 20);
    bpf_program* p = nullptr;
    bpf_object__for_each_program(p, obj) {
        bpf_program__set_log_buf(p, verifier_log.data(), verifier_log.size());
        bpf_program__set_log_level(p, 1);
    }

    int rc = bpf_object__load(obj);
    if (rc != 0) {
        const int saved = errno;
        // EACCES is the verifier saying "your program is unsafe"; the log
        // explains why. EPERM is missing CAP_BPF / RLIMIT_MEMLOCK and the
        // program never made it to the verifier.
        if (saved != EPERM) {
            std::fprintf(stderr,
                "---- verifier log ----\n%s---- end verifier log ----\n",
                verifier_log.data());
        }
        std::fprintf(stderr, "bpf_object__load failed: rc=%d errno=%d (%s)\n",
                     rc, saved, std::strerror(saved));
        bpf_object__close(obj);
        if (saved == EPERM) {
            std::fprintf(stderr,
                "verifier check skipped: needs CAP_BPF or root. "
                "Re-run as: sudo %s --object %s\n",
                argv[0], path.c_str());
            return 77;
        }
        return 1;
    }

    std::printf("verifier accepted %s\n", path.c_str());

    // Walk programs and report each name + size for diagnostic value.
    bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, obj) {
        const char* name = bpf_program__name(prog);
        size_t insns = bpf_program__insn_cnt(prog);
        std::printf("  program: %s  insns=%zu\n", name ? name : "(anon)", insns);
    }

    bpf_object__close(obj);
    return 0;
}
