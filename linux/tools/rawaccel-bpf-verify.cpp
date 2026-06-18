// One-shot: load rawaccel.bpf.o through libbpf, report verifier acceptance.
// No device attach, no map population. ctest runs this to keep the BPF
// program verifier-clean per commit.

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
    // echo to stderr so an interactive run still sees the verifier log
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

    // Per-program log buffer: the default truncates a non-trivial rejection.
    // 1 MiB exceeds anything the kernel emits; on the heap to keep the stack small.
    static std::vector<char> verifier_log(1 << 20);
    bpf_program* p = nullptr;
    bpf_object__for_each_program(p, obj) {
        bpf_program__set_log_buf(p, verifier_log.data(), verifier_log.size());
        bpf_program__set_log_level(p, 1);
    }

    int rc = bpf_object__load(obj);
    if (rc != 0) {
        const int saved = errno;

        // Kernel built without CONFIG_HID_BPF: the hid_bpf kfuncs/struct_ops are
        // absent from BTF, so load fails at ksym resolution before the verifier.
        // That's an environment gap (generic CI kernels omit HID-BPF), not a
        // program defect -- skip rather than fail.
        const bool no_hid_bpf =
            g_log.find("not found in kernel or module BTFs") != std::string::npos &&
            g_log.find("hid_bpf") != std::string::npos;
        if (no_hid_bpf) {
            std::fprintf(stderr,
                "verifier check skipped: kernel lacks HID-BPF (CONFIG_HID_BPF); "
                "hid_bpf kfuncs absent from BTF\n");
            bpf_object__close(obj);
            return 77;
        }

        // EPERM: missing CAP_BPF / RLIMIT_MEMLOCK, never reached the verifier.
        if (saved == EPERM) {
            std::fprintf(stderr, "bpf_object__load failed: rc=%d errno=%d (%s)\n",
                         rc, saved, std::strerror(saved));
            std::fprintf(stderr,
                "verifier check skipped: needs CAP_BPF or root. "
                "Re-run as: sudo %s --object %s\n",
                argv[0], path.c_str());
            bpf_object__close(obj);
            return 77;
        }

        // EACCES etc: verifier rejected as unsafe -- the log explains why.
        std::fprintf(stderr,
            "---- verifier log ----\n%s---- end verifier log ----\n",
            verifier_log.data());
        std::fprintf(stderr, "bpf_object__load failed: rc=%d errno=%d (%s)\n",
                     rc, saved, std::strerror(saved));
        bpf_object__close(obj);
        return 1;
    }

    std::printf("verifier accepted %s\n", path.c_str());

    // report each program's name + insn count
    bpf_program* prog = nullptr;
    bpf_object__for_each_program(prog, obj) {
        const char* name = bpf_program__name(prog);
        size_t insns = bpf_program__insn_cnt(prog);
        std::printf("  program: %s  insns=%zu\n", name ? name : "(anon)", insns);
    }

    bpf_object__close(obj);
    return 0;
}
