// Android host layer (runtime/platform.h), arm64 only. Based on runtime/platform/linux.c: the
// guest window, memory and timing are the same; the differences are the process shape (no
// re-exec; the game code generator runs through Chaquopy and the compiler is a clang shipped in
// the APK, both wired in cpp/android/android_bridge.c) and where data and shipped resources live
// (the app's external files dir, set by the Java side as SAVAGEWIND_DATA_ROOT).
#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <sys/wait.h>
#include <spawn.h>
#include <time.h>
#include <unistd.h>

#include "../platform.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// ---- guest address space (as on Linux: everything backed from the start, MAP_NORESERVE)
bool plat_reserve_guest(uint64_t lo, uint64_t hi) {
    void *p = mmap((void *)(uintptr_t)lo, hi - lo, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) return false;
    if (p != (void *)(uintptr_t)lo) { munmap(p, hi - lo); return false; }
    return true;
}

bool plat_reset_guest(uint64_t lo, uint64_t hi) {
    return mmap((void *)(uintptr_t)lo, hi - lo, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) == (void *)(uintptr_t)lo;
}

void plat_commit(uint64_t addr, uint64_t size) { (void)addr; (void)size; }

int plat_protect(uint64_t addr, uint64_t size, int prot) {
    int p = prot == PLAT_PROT_NONE ? PROT_NONE : prot == PLAT_PROT_READ ? PROT_READ : PROT_READ | PROT_WRITE;
    return mprotect((void *)(uintptr_t)addr, size, p);
}

// ---- time
uint64_t plat_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

void plat_sleep_until_ns(uint64_t t) {
    struct timespec ts = {.tv_sec = (time_t)(t / 1000000000u), .tv_nsec = (long)(t % 1000000000u)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
}

// The vsync thread: SCHED_FIFO needs privileges an app doesn't have; the nice bump is what
// normal Linux threads get too.
void plat_thread_realtime(uint64_t period_ns) {
    (void)period_ns;
    struct sched_param sp = {.sched_priority = 1};
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0) return;
    setpriority(PRIO_PROCESS, (id_t)gettid(), -10);
}

// ---- process
// stderr goes nowhere useful inside an app: the log lands in savagewind.log in the data folder
// (SAVAGEWIND_DATA_ROOT, set by MainActivity before the runtime starts; rt_paths_init uses the
// same variable later).
static char g_log_path[1024];

static void log_to_file(void) {
    if (isatty(STDERR_FILENO)) return;  // (adb shell runs: keep the terminal output)
    const char *env = getenv("SAVAGEWIND_DATA_ROOT");
    if (!env || !*env) return;
    char dir[900];
    snprintf(dir, sizeof dir, "%s", env);
    for (char *c = dir + 1; *c; c++)
        if (*c == '/') { *c = 0; mkdir(dir, 0755); *c = '/'; }
    mkdir(dir, 0755);
    snprintf(g_log_path, sizeof g_log_path, "%s/savagewind.log", dir);
    bool append = getenv("SAVAGEWIND_LOG_APPEND") != NULL;
    if (!freopen(g_log_path, append ? "a" : "w", stderr)) { g_log_path[0] = 0; return; }
    dup2(STDERR_FILENO, STDOUT_FILENO);
    setenv("SAVAGEWIND_LOG_APPEND", "1", 1);
}

void plat_init(void) { log_to_file(); }

// rt_resource() looks in the "exe's folder": the data root, where MainActivity unpacks the
// shipped resources (icon, gamelib-src headers, the compiler's headers). rt_paths_init strips
// the last component (on the desktops that is the binary's name), so report a phantom file
// name inside the data root and g_exe_dir lands on the data root itself.
bool plat_exe_path(char *buf, size_t size) {
    const char *env = getenv("SAVAGEWIND_DATA_ROOT");
    if (!env || !*env) return false;
    snprintf(buf, size, "%s/app", env);
    return true;
}

// No process replacement inside an app. Only the guest-space constructor calls this, and on
// Android it treats the failure itself (runtime/main.c): the user just reopens the app.
void plat_exec(char *const argv[]) {
    (void)argv;
}

// ---- diagnostics
const char *plat_symbol(uint64_t pc, uint64_t *offset) {
    Dl_info di = {0};
    if (!dladdr((void *)(uintptr_t)pc, &di) || !di.dli_sname) return NULL;
    if (offset) *offset = pc - (uint64_t)(uintptr_t)di.dli_saddr;
    return di.dli_sname;
}

void plat_fatal_notice(void) {
    if (!g_log_path[0]) return;
    fflush(stderr);
    char text[1200];
    snprintf(text, sizeof text, "Savage Wind stopped because of an error.\n\nDetails: %s", g_log_path);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Savage Wind", text, NULL);
}

const char *rt_host_fn_name(uint64_t pc, uint64_t *offset);  // runtime/dispatch.c

// bionic's backtrace() only exists on newer Android versions: look it up dynamically.
void plat_print_backtrace(void) {
    static int (*bt)(void **, int);
    if (!bt) bt = (int (*)(void **, int))dlsym(RTLD_DEFAULT, "backtrace");
    if (!bt) return;
    void *stack[64];
    int n = bt(stack, 64);
    Dl_info self = {0};
    dladdr((void *)plat_print_backtrace, &self);
    for (int i = 0; i < n; i++) {
        uint64_t pc = (uint64_t)(uintptr_t)stack[i], off = 0;
        Dl_info di = {0};
        if (!dladdr(stack[i], &di)) di.dli_fname = NULL;
        bool ours = di.dli_fbase && di.dli_fbase == self.dli_fbase;
        const char *s = plat_symbol(pc, &off);
        if (!s && ours) s = rt_host_fn_name(pc, &off);
        const char *mod = di.dli_fname ? (strrchr(di.dli_fname, '/') ? strrchr(di.dli_fname, '/') + 1 : di.dli_fname) : "?";
        uint64_t moff = pc - (uint64_t)(uintptr_t)di.dli_fbase;
        if (s && ours) fprintf(stderr, "  #%-2d %016llx %s+%llx (libmain.so+%llx)\n", i, (unsigned long long)pc, s,
                               (unsigned long long)off, (unsigned long long)moff);
        else if (s) fprintf(stderr, "  #%-2d %016llx %s+%llx (%s)\n", i, (unsigned long long)pc, s, (unsigned long long)off, mod);
        else fprintf(stderr, "  #%-2d %016llx ?+%llx (%s)\n", i, (unsigned long long)pc, (unsigned long long)moff, mod);
    }
}

static void crash_handler(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = ctx;
    static volatile int depth;
    if (sig == SIGABRT && depth++ > 0) { signal(SIGABRT, SIG_DFL); abort(); }
    uint64_t fa = (uint64_t)(uintptr_t)si->si_addr;
    uint64_t pc = uc->uc_mcontext.pc, lr = uc->uc_mcontext.regs[30];
    if (rt_fault_watch(fa, pc)) return;
    char what[32];
    snprintf(what, sizeof what, "signal %d", sig);
    rt_fault_report(what, fa, pc, lr);
}

void plat_crash_handler_install(void) {
    stack_t ss = {.ss_sp = malloc(1 << 20), .ss_size = 1 << 20};
    sigaltstack(&ss, NULL);
    struct sigaction sa = {0};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

// ---- child processes
// The desktop versions spawn the frozen swgen helper and a C compiler. On Android the shapes
// change, the model doesn't:
//   - the generator: the same tools/*.py, run through Chaquopy inside the app. plat_spawn routes
//     the "gen.py" argv[0] marker (find_tools sets it, runtime/gamebuild.c) to the Java bridge,
//     which reports back through the job table. Handles are negative: -(slot + 1).
//   - the compiler: a real clang shipped in the APK (as libclang.so in nativeLibraryDir, ld.lld
//     beside it), spawned like any desktop child process through SAVAGEWIND_CC.
//   - everything else: posix_spawn, as on Linux.
#define MAX_JOBS 128
typedef struct {
    atomic_int state;  // 0 free, 1 running, 2 done
    atomic_int exit;
} PlatJob;
static PlatJob g_jobs[MAX_JOBS];

void android_job_mark_done(int slot, int exit_code) {
    PlatJob *j = &g_jobs[slot];
    int running = 1;
    if (atomic_compare_exchange_strong(&j->state, &running, 2)) atomic_store(&j->exit, exit_code);
}

static int job_alloc(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        int want = 0;
        if (atomic_compare_exchange_strong(&g_jobs[i].state, &want, 1)) {
            atomic_store(&g_jobs[i].exit, -1);
            return i;
        }
    }
    return -1;
}

// Implemented in cpp/android/android_bridge.c: dispatches gen.py into the Java side (Chaquopy).
bool android_swgen_dispatch(int slot, const char *const *argv, const char *log);

PlatProc plat_spawn(const char *const argv[], const char *log_path, bool append_log) {
    if (!argv || !argv[0]) return 0;

    if (!strcmp(argv[0], "gen.py")) {  // the game code generator (Chaquopy, in process)
        int slot = job_alloc();
        if (slot < 0) return 0;
        if (!android_swgen_dispatch(slot, argv, log_path)) {
            atomic_store(&g_jobs[slot].state, 0);
            return 0;
        }
        return -(slot + 1);
    }

    // Real child process (the compiler): stdout + stderr into log_path, own process group,
    // exactly like the Linux build does it.
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log_path ? log_path : "/dev/null",
                                     O_WRONLY | O_CREAT | (append_log ? O_APPEND : O_TRUNC), 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
#ifdef POSIX_SPAWN_SETPGROUP
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&at, 0);
#endif
    pid_t pid;
    int rc = posix_spawn(&pid, argv[0], &fa, &at, (char *const *)argv, environ);
    posix_spawnattr_destroy(&at);
    posix_spawn_file_actions_destroy(&fa);
    return rc ? 0 : (PlatProc)pid;
}

int plat_proc_wait(PlatProc p, bool block) {
    if (p < 0) {  // a generator job (Chaquopy)
        int slot = (int)(-p) - 1;
        if (slot >= MAX_JOBS) return 255;
        PlatJob *j = &g_jobs[slot];
        if (atomic_load(&j->state) != 2) return -1;
        return atomic_load(&j->exit);
    }
    int st = 0;
    pid_t r = waitpid((pid_t)p, &st, block ? 0 : WNOHANG);
    if (r == 0) return -1;
    if (r < 0) return 255;
    return WIFEXITED(st) ? WEXITSTATUS(st) : 255;
}

void plat_proc_kill(PlatProc p) {
    if (p < 0) {  // the Java side keeps going, but its late report lands on a freed slot
        int slot = (int)(-p) - 1;
        if (slot >= 0 && slot < MAX_JOBS) atomic_store(&g_jobs[slot].state, 0);
        return;
    }
    kill(-(pid_t)p, SIGKILL);
    waitpid((pid_t)p, NULL, 0);
}

#include <ftw.h>
static int rm_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
    (void)sb, (void)flag, (void)ftw;
    return remove(path);
}
bool plat_remove_tree(const char *dir) { return nftw(dir, rm_entry, 16, FTW_DEPTH | FTW_PHYS) == 0; }

void plat_terminate(void) { _exit(0); }
