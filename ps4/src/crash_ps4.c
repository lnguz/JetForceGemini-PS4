// Jet Force Gemini PS4: thread stacks and crash reports.
//
// Stacks: the runtime and the port create every thread with std::thread, i.e. pthread_create with no attributes, and
// the console's default stack is far smaller than a PC's (1 MB on Windows, 8 MB on Linux), where the recompiled code's
// deep call chains run fine. The link wraps pthread_create (--wrap=pthread_create) so threads created without an explicit
// stack size get kThreadStack.
//
// Crashes: SIGSEGV/SIGBUS/SIGILL/SIGFPE write the faulting address, the instruction pointer and every code address found
// near the stack pointer to the log, all as offsets from this module's link address, so they can be looked up in
// build-ps4/jfg.elf (llvm-addr2line -f -C -e jfg.elf <offset + link address>; ps4_crash_link_anchor gives it).
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "ps4_platform.h"

enum { kThreadStack = 2 * 1024 * 1024 };
// FreeBSD signal numbers (the PS4 kernel's); OpenOrbis' headers carry Linux's (SIGBUS 7, SIGUSR1 10).
enum { kSigSegv = 11, kSigBus = 10, kSigUsr1 = 30 };

int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg);

static int g_threads_created;

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) {
    pthread_attr_t sized;
    size_t size = 0;
    int rc;
    if (attr == NULL) {
        pthread_attr_init(&sized);
        pthread_attr_setstacksize(&sized, kThreadStack);
        rc = __real_pthread_create(thread, &sized, start, arg);
        pthread_attr_destroy(&sized);
        size = kThreadStack;
    } else if (pthread_attr_getstacksize(attr, &size) == 0 && size < kThreadStack) {
        sized = *attr;
        pthread_attr_setstacksize(&sized, kThreadStack);
        rc = __real_pthread_create(thread, &sized, start, arg);
        size = kThreadStack;
    } else {
        rc = __real_pthread_create(thread, attr, start, arg);
    }
    ps4_log("thread %d created: stack %zu KB, rc %d", ++g_threads_created, size / 1024, rc);
    return rc;
}

// An address in this module whose link-time value is known (nm build-ps4/jfg.elf | grep ps4_crash_link_anchor).
void ps4_crash_link_anchor(void) {}

static int is_code(uintptr_t value, uintptr_t anchor) {
    // The executable is ~18 MB: anything within 64 MB of the anchor is taken as a code address worth printing.
    return value + 0x4000000u > anchor && value < anchor + 0x4000000u;
}

// The PS4's ucontext has 48 bytes after uc_sigmask (shadPS4: Ucontext.field1_0x10), so mcontext starts at +64.
// Inside it (FreeBSD amd64 order): mc_addr +136, mc_rip +160, mc_rsp +184.
static void report(const char *source, int sig, const void *context) {
    const uintptr_t anchor = (uintptr_t)&ps4_crash_link_anchor;
    const uint8_t *uc = (const uint8_t *)context;
    uintptr_t rip = 0, rsp = 0, addr = 0;
    if (uc) {
        memcpy(&addr, uc + 64 + 136, sizeof addr);
        memcpy(&rip, uc + 64 + 160, sizeof rip);
        memcpy(&rsp, uc + 64 + 184, sizeof rsp);
    }
    ps4_log("CRASH (%s): signal %d, thread %p, fault address %p", source, sig, (void *)pthread_self(), (void *)addr);
    ps4_log("CRASH: anchor %p, rip %p = anchor%+lld, rsp %p", (void *)anchor, (void *)rip, (long long)(rip - anchor),
            (void *)rsp);
    if (uc && sig != kSigUsr1) {
        // Also every code address in the saved context, in case the layout guess is off.
        for (int i = 0; i < 64; i++) {
            uintptr_t v;
            memcpy(&v, uc + i * 8, sizeof v);
            if (is_code(v, anchor)) ps4_log("CRASH:   context[%d] = anchor%+lld", i * 8, (long long)(v - anchor));
        }
    }
    if (rsp && (rsp & 7) == 0) {
        const uintptr_t *stack = (const uintptr_t *)rsp;
        int found = 0;
        for (int i = 0; i < 4096 && found < (sig == kSigUsr1 ? 16 : 40); i++) {
            if (is_code(stack[i], anchor)) {
                ps4_log("CRASH:   stack[%d] = anchor%+lld", i, (long long)(stack[i] - anchor));
                found++;
            }
        }
    }
    ps4_log("CRASH: end of report");
}

static void signal_handler(int sig, siginfo_t *info, void *context) {
    (void)info;
    report("signal", sig, context);
    _exit(1);
}

static void exception_handler(int sig, void *context) {
    if (sig == kSigUsr1) { // a sample asked for by ps4_sample_thread: report and carry on
        report("sample", sig, context);
        return;
    }
    report("exception", sig, context);
    _exit(1);
}

typedef void (*Ps4ExceptionHandler)(int, void *);
int ps4_install_exception_handler(int signum, Ps4ExceptionHandler handler) __asm__("sceKernelInstallExceptionHandler");
int ps4_raise_exception(pthread_t thread, int signum) __asm__("sceKernelRaiseException");

// Logs where a running thread is (its instruction pointer and the code addresses on its stack), without stopping it.
void ps4_sample_thread(pthread_t thread) {
    int rc = ps4_raise_exception(thread, kSigUsr1);
    if (rc != 0) ps4_log("sample: sceKernelRaiseException failed 0x%X", (unsigned)rc);
}

void ps4_crash_install(void) {
    static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    for (unsigned i = 0; i < sizeof signals / sizeof signals[0]; i++) {
        struct sigaction action;
        memset(&action, 0, sizeof action);
        action.__sa_handler.__sa_sigaction = signal_handler; // OpenOrbis' sa_sigaction macro is broken
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        if (sigaction(signals[i], &action, NULL) != 0) ps4_log("crash handler: sigaction(%d) failed", signals[i]);
    }
    // The console's own mechanism (what Sony's titles use): takes over SIGSEGV and SIGBUS.
    int rc_segv = ps4_install_exception_handler(kSigSegv, exception_handler);
    int rc_bus = ps4_install_exception_handler(kSigBus, exception_handler);
    int rc_usr1 = ps4_install_exception_handler(kSigUsr1, exception_handler);
    ps4_log("crash handlers installed (exception handler: 0x%X 0x%X 0x%X)", (unsigned)rc_segv, (unsigned)rc_bus,
            (unsigned)rc_usr1);
}
