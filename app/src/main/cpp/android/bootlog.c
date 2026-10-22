// bootlog.c - libbootlog.so: startup crash diagnostics.
//
// Loads BEFORE SDL3 and libmain (see MainActivity.getLibraries()). Its only job: install
// signal handlers (SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE/SIGTRAP) that record a native crash
// to the boot log file and logcat tag "swboot", including the faulting module + offset
// (from /proc/self/maps) and a frame-pointer chain backtrace.
//
// Why this exists: a native death while dlopen'ing a library is invisible to the app's own
// uid-filtered logcat capture - the "Fatal signal" report comes from the system crash_dump
// process, which those captures never include. With libbootlog the crash lands in the same
// tags the user's capture already collects, and in bootlog.txt next to the app's files.
//
// The log file path arrives through SAVAGEWIND_CRASHLOG (MainActivity sets it before the
// libraries load). After recording, the default handler is restored and the signal re-raised,
// so the system still produces its normal tombstone / crash dialog.

#include <android/log.h>

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ucontext.h>

#define TAG "swboot"

static int g_fd = -1;
static volatile int g_in_handler = 0;

static void wout(const char *s) {
    size_t n = strlen(s);
    if (g_fd >= 0) {
        ssize_t w = write(g_fd, s, n);
        (void)w;
    }
    __android_log_print(ANDROID_LOG_ERROR, TAG, "%s", s);
}

// describe(pc): fills out with "<module path> +0x<offset>" for the mapping containing pc.
// Reads /proc/self/maps with raw syscalls only (no stdio, no malloc): the handler runs on
// an already-crashed thread.
static void describe(uintptr_t pc, char *out, size_t outn) {
    static char buf[32768];
    size_t used = 0;
    int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        for (;;) {
            ssize_t n = read(fd, buf + used, sizeof buf - 1 - used);
            if (n <= 0) break;
            used += (size_t)n;
            if (used >= sizeof buf - 1) break;
        }
        close(fd);
    }
    buf[used] = 0;

    for (char *line = buf, *nl; (nl = strchr(line, '\n')) != NULL; line = nl + 1) {
        *nl = 0;
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) != 2) continue;
        if (pc >= (uintptr_t)lo && pc < (uintptr_t)hi) {
            const char *path = strchr(line, '/');
            if (path) {
                snprintf(out, outn, "%s +0x%lx", path, (unsigned long)(pc - lo));
            } else {  // anonymous mapping: give the range so the region is still identifiable
                snprintf(out, outn, "anon %lx-%lx", lo, hi);
            }
            return;
        }
    }
    snprintf(out, outn, "pc=0x%lx (not in maps)", (unsigned long)pc);
}

static void crash_handler(int sig, siginfo_t *si, void *ucv) {
    if (g_in_handler) {  // a second fault while recording: give up, die the default way
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }
    g_in_handler = 1;

    ucontext_t *uc = (ucontext_t *)ucv;
    uintptr_t pc = uc->uc_mcontext.pc;
    uintptr_t lr = uc->uc_mcontext.regs[30];
    uintptr_t sp = uc->uc_mcontext.sp;
    uintptr_t fp = uc->uc_mcontext.regs[29];

    char out[512], where[256];
    describe(pc, where, sizeof where);
    snprintf(out, sizeof out, "CRASH signal=%d addr=%p pc=0x%lx [%s] lr=0x%lx sp=0x%lx",
             sig, si ? si->si_addr : NULL, (unsigned long)pc, where,
             (unsigned long)lr, (unsigned long)sp);
    wout(out);

    // Frame-pointer chain: the NDK builds keep the frame pointer on arm64 by default.
    // 16 frames is plenty to see who was loading what.
    for (int i = 0; i < 16 && fp; i++) {
        if (fp & 7) break;
        if (fp < sp || fp - sp > (16u << 20)) break;  // not a plausible frame
        uintptr_t frame[2];
        memcpy(frame, (const void *)fp, sizeof frame);
        describe(frame[1], where, sizeof where);
        snprintf(out, sizeof out, "  bt[%d] lr=0x%lx [%s]", i,
                 (unsigned long)frame[1], where);
        wout(out);
        if (frame[0] <= fp) break;
        fp = frame[0];
    }

    wout("CRASH end (restoring default handler)");
    g_in_handler = 0;
    signal(sig, SIG_DFL);
    raise(sig);
}

__attribute__((constructor)) static void bootlog_start(void) {
    const char *p = getenv("SAVAGEWIND_CRASHLOG");
    if (p) g_fd = open(p, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    const int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
    for (size_t i = 0; i < sizeof sigs / sizeof *sigs; i++)
        if (sigaction(sigs[i], &sa, NULL) != 0)
            __android_log_print(ANDROID_LOG_WARN, TAG, "sigaction(%d) failed", sigs[i]);

    __android_log_print(ANDROID_LOG_INFO, TAG, "bootlog ready (crashlog=%s)",
                        p ? p : "(unset)");
}
