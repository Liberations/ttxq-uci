// ttxq-armv8 — a standard stdin/stdout UCI engine that wraps libpikafishso.so.
//
// The original .so (from the 天天象棋 / Tencent QQ Chess app) exposes only a JNI
// interface (startEngine / engineRun / getReply / stopEngine), not a UCI main().
// This program turns it into a drop-in UCI engine binary:
//
//   * The .so is embedded in the executable at build time and loaded from a memfd
//     at runtime (no temp file on disk, nothing written next to the binary).
//   * A minimal JNIEnv vtable is synthesized so the JNI entry points can be called
//     from plain native code, with no Android runtime / JVM required.
//   * A dedicated PUMP thread polls getReply() and forwards engine output to stdout;
//     the MAIN thread only reads stdin and sends commands via engineRun(). Keeping
//     these apart is essential: getReply() can block, and must never stall input.
//   * "id name ..." is rewritten on the fly to "ttxq-armv8". The .so is unmodified.
//
// The external NNUE network file is auto-detected at startup (see find_nnue()), or
// can be set explicitly with the TTXQ_NNUE environment variable. Any later
// `setoption name EvalFile value <path>` from the GUI still overrides it.
//
// See README.md for build and usage. Licensed under GPL-3.0-or-later (see LICENSE).

#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <mutex>

// Embedded .so blob, produced by tools/embed_so.sh / build.sh via
// `ld.lld -r -b binary vendor/libpikafishso.so`.
extern "C" {
// ld.lld -r -b binary vendor/libpikafishso.so derives the symbol name from the
// full input path: dots and slashes become underscores.
extern const unsigned char _binary_vendor_libpikafishso_so_start[];
extern const unsigned char _binary_vendor_libpikafishso_so_end[];
}

// ---------------------------------------------------------------------------
// Minimal fake JNIEnv
// Offsets verified against the NDK's jni.h (struct JNINativeInterface):
//   NewStringUTF = 0x538, GetStringUTFChars = 0x548, ReleaseStringUTFChars = 0x550
// Only these three slots are used by the engine's JNI wrapper.
// ---------------------------------------------------------------------------
static char* dup_utf(const char* s) { return s ? strdup(s) : strdup(""); }
static jstring JNICALL f_NewStringUTF(JNIEnv*, const char* b) { return (jstring)dup_utf(b); }
static const char* JNICALL f_GetStringUTFChars(JNIEnv*, jstring s, jboolean* c) {
    if (c) *c = JNI_FALSE;
    return (const char*)s;
}
static void JNICALL f_ReleaseStringUTFChars(JNIEnv*, jstring, const char*) {}

static JNINativeInterface g_iface;
static JNIEnv g_env;
static void init_fake_env() {
    memset(&g_iface, 0, sizeof(g_iface));
    g_iface.NewStringUTF = f_NewStringUTF;
    g_iface.GetStringUTFChars = f_GetStringUTFChars;
    g_iface.ReleaseStringUTFChars = f_ReleaseStringUTFChars;
    g_env.functions = &g_iface;
}

typedef void     (*fn_void)(JNIEnv*, jclass);
typedef void     (*fn_run)(JNIEnv*, jclass, jstring);
typedef jstring  (*fn_str)(JNIEnv*, jclass);
static fn_void p_start, p_stop;
static fn_run  p_run;
static fn_str  p_reply;

// ---------------------------------------------------------------------------
// Load the embedded .so from a memfd (no on-disk artifact)
// ---------------------------------------------------------------------------
static int g_lib_fd = -1;
static bool load_embedded_so() {
    g_lib_fd = (int)syscall(SYS_memfd_create, "libpikafishso.so", 1U);  // MFD_CLOEXEC
    if (g_lib_fd < 0) return false;

    const unsigned char* p = _binary_vendor_libpikafishso_so_start;
    size_t left = (size_t)(_binary_vendor_libpikafishso_so_end - p);
    while (left) {
        ssize_t w = write(g_lib_fd, p, left);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w;
        left -= (size_t)w;
    }

    // Android can dlopen a file descriptor via /proc/self/fd/N.
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", g_lib_fd);
    void* h = dlopen(path, RTLD_NOW);
    if (!h) return false;

    p_start = (fn_void)dlsym(h, "Java_com_pikafish_androidso_PikafishUtils_startEngine");
    p_stop  = (fn_void)dlsym(h, "Java_com_pikafish_androidso_PikafishUtils_stopEngine");
    p_run   = (fn_run)dlsym(h, "Java_com_pikafish_androidso_PikafishUtils_engineRun");
    p_reply = (fn_str)dlsym(h, "Java_com_pikafish_androidso_PikafishUtils_getReply");
    return p_start && p_stop && p_run && p_reply;
}

// ---------------------------------------------------------------------------
// Output: rewrite the engine id line to our brand
// ---------------------------------------------------------------------------
static std::mutex g_out_mu;

static std::string rewrite_id_line(const std::string& in) {
    std::string out;
    size_t pos = 0;
    const std::string tag = "id name ";
    while (true) {
        size_t p = in.find(tag, pos);
        if (p == std::string::npos) { out += in.substr(pos); break; }
        if (p != 0 && in[p - 1] != '\n') {
            out += in.substr(pos, p - pos + tag.size());
            pos = p + tag.size();
            continue;
        }
        out += in.substr(pos, p - pos);
        out += "id name ttxq-armv8";
        size_t e = in.find('\n', p);
        if (e == std::string::npos) { pos = in.size(); break; }
        out += '\n';
        pos = e + 1;
    }
    return out;
}

static void emit(const std::string& s) {
    std::string out = rewrite_id_line(s);
    std::lock_guard<std::mutex> lk(g_out_mu);
    fwrite(out.data(), 1, out.size(), stdout);
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// PUMP thread: continuously drain getReply() -> stdout
// ---------------------------------------------------------------------------
static volatile bool g_running = true;
static void* pump_thread(void*) {
    while (g_running) {
        jstring jr = p_reply(&g_env, nullptr);
        if (jr) {
            const char* c = (const char*)jr;
            if (c && *c) emit(c);
            free((void*)jr);  // our f_NewStringUTF strdup'd it
        }
        usleep(15 * 1000);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// NNUE auto-detection
// ---------------------------------------------------------------------------
static std::string find_nnue() {
    std::vector<std::string> cand;
    if (const char* e = getenv("TTXQ_NNUE")) cand.emplace_back(e);
    cand.emplace_back("pikafish.nnue");

    // Directory containing the executable (readlink /proc/self/exe)
    char exe[4096];
    ssize_t ln = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (ln > 0) {
        exe[ln] = 0;
        std::string ep(exe);
        size_t s = ep.find_last_of('/');
        if (s != std::string::npos) cand.emplace_back(ep.substr(0, s + 1) + "pikafish.nnue");
    }
    // Common adb / Termux staging locations
    cand.emplace_back("/data/local/tmp/pikafish.nnue");
    cand.emplace_back("/sdcard/pikafish.nnue");

    for (const auto& c : cand) {
        struct stat st;
        // A real NNUE is tens of MB; the 1MB guard avoids matching stray files.
        if (stat(c.c_str(), &st) == 0 && st.st_size > 1000000) return c;
    }
    return std::string();
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // startEngine() hangs when stdin is a TTY (the native init waits on terminal
    // input). Keep the real input on fd 10 and point fd 0 at /dev/null so the
    // engine sees a non-TTY stdin; the main loop reads commands from fd 10.
    int real_input = dup(STDIN_FILENO);
    if (real_input != 10) {
        dup2(real_input, 10);
        close(real_input);
        real_input = 10;
    }
    int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        close(devnull);
    }

    init_fake_env();
    if (!load_embedded_so()) {
        fprintf(stderr, "[ttxq_uci] failed to load embedded .so: %s\n", dlerror());
        return 1;
    }

    p_start(&g_env, nullptr);

    // Set the NNUE at startup so a bare `go` works. A GUI's own EvalFile option
    // still overrides this later.
    std::string net = find_nnue();
    if (!net.empty()) {
        std::string c = "setoption name EvalFile value " + net;
        p_run(&g_env, nullptr, (jstring)c.c_str());
        usleep(1500 * 1000);  // 34 MB NNUE load needs time before `go`
    }

    pthread_t pump;
    pthread_create(&pump, nullptr, pump_thread, nullptr);

    // MAIN thread: read stdin, send commands. Never calls getReply().
    std::string pending;
    char buf[4096];
    bool quit = false;
    while (!quit) {
        ssize_t n = read(real_input, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        pending.append(buf, (size_t)n);

        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;
            p_run(&g_env, nullptr, (jstring)line.c_str());
            if (line == "quit") { quit = true; break; }
        }
    }

    // Give the engine a moment to emit final output, then stop cleanly.
    usleep(800 * 1000);
    g_running = false;
    pthread_join(pump, nullptr);
    p_stop(&g_env, nullptr);
    if (g_lib_fd >= 0) close(g_lib_fd);
    return 0;
}
