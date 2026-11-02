/* Compile the production handler with host-only RPC/context adapters. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "linux-signals.inc"

#define SIG_IGN ((void *)1)
#define SIGTSTP LINUX_SIGTSTP
#define SIGSTOP LINUX_SIGSTOP
#define __PTK_DARLING_SIGEXC_SIGNAL 1
#define LINUX_SIG_UNBLOCK 1
#define __NR_exit_group 1
#define __NR_kill 2
#define __NR_rt_sigaction 3
#define __NR_rt_sigprocmask 4
#define __NR_getpid 5
#define __NR_gettid 6
#define __NR_tgkill 7
#define kern_printf(...) ((void)0)
#define __simple_printf(...) ((void)0)
#define LINUX_SYSCALL(n, ...) fake_syscall(n, ##__VA_ARGS__)

typedef uint64_t linux_sigset_t;
typedef void linux_sig_handler(int, void *, void *);
typedef void bsd_sig_handler(int, void *, void *);
typedef int thread_t;
typedef struct { uint64_t __rip; } x86_thread_state64_t;
typedef struct { int unused; } x86_float_state64_t;
struct linux_ucontext { struct { struct { uint64_t rip; } gregs; } uc_mcontext; };
struct linux_siginfo { int si_pid, si_code; void *si_addr; };
struct linux_sigaction {
    linux_sig_handler *sa_sigaction;
    linux_sigset_t sa_mask;
    unsigned sa_flags;
    void (*sa_restorer)(void);
};
static bsd_sig_handler *sig_handlers[32];
static unsigned sig_masks[32], sig_flags[32];
static uintptr_t guard;
static unsigned enters, exits, forwarded, reraised, actions, unmasks;
static bool server_present = true;
static void * _pthread_getspecific_direct(int key) { (void)key; return (void *)guard; }
static void _pthread_setspecific_direct(int key, void *v) { (void)key; guard = (uintptr_t)v; }
static int dserver_rpc_interrupt_enter(void) { assert(server_present); ++enters; return 0; }
static int dserver_rpc_interrupt_exit(void) { ++exits; return 0; }
static void rpc_failed_in_handler(const char *what, int status)
{ (void)what; (void)status; abort(); }
static bool is_server_gone(int status) { (void)status; return false; }
static void exit_server_gone(void) { abort(); }
static int signum_linux_to_bsd(int n) { return n; }
static int signum_bsd_to_linux(int n) { return n; }
static thread_t mach_thread_self(void) { return 1; }
static void state_to_kernel(struct linux_ucontext *c, void *t, void *f)
{ (void)c; (void)t; (void)f; }
static void state_from_kernel(struct linux_ucontext *c, void *t, void *f)
{ (void)c; (void)t; (void)f; }
static int dserver_rpc_sigprocess(int b, int l, int p, int c, void *a,
    void *t, void *f, int *result)
{ (void)l; (void)p; (void)c; (void)a; (void)t; (void)f; *result = b; return 0; }
static void handler_linux_to_bsd(int n, void *i, void *c)
{ sig_handlers[n](n, i, c); }
static void __simple_abort(void) { abort(); }
static void sig_restorer(void) {}
static int fake_syscall(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    if (n == __NR_tgkill) ++reraised;
    if (n == __NR_rt_sigaction) {
        (void)va_arg(ap, int);
        struct linux_sigaction *sa = va_arg(ap, struct linux_sigaction *);
        assert(sa->sa_sigaction == NULL);
        ++actions;
    }
    if (n == __NR_rt_sigprocmask) {
        (void)va_arg(ap, int);
        linux_sigset_t *set = va_arg(ap, linux_sigset_t *);
        assert(*set != 0);
        ++unmasks;
    }
    assert(n != __NR_exit_group);
    va_end(ap);
    return 0;
}
/* A signal must never enter libc's environment machinery, even with no
 * DARLING_LOG_SIGNALS setting. Before program-vars initialization it is unusable. */
char *getenv(const char *name) { (void)name; _exit(91); }
static void caught(int n, void *i, void *c)
{ (void)n; (void)i; (void)c; ++forwarded; }

#include "handler.inc"

int main(void)
{
    struct linux_ucontext context = {0};
    struct linux_siginfo info = {0};
    const int signals[] = {LINUX_SIGWINCH, LINUX_SIGWINCH, LINUX_SIGURG,
        LINUX_SIGWINCH, LINUX_SIGTERM};
    bsd_sig_handler *dispositions[] = {SIG_IGN, NULL, SIG_IGN, caught, NULL};
    for (unsigned i = 0; i < sizeof(signals) / sizeof(*signals); ++i) {
        sig_handlers[signals[i]] = dispositions[i];
        sigexc_handler(signals[i], &info, &context);
        assert(guard == 0 && enters == i + 1 && exits == i + 1);
    }
    assert(forwarded == 1);
    /* The adapter doesn't enact Linux's default-ignore disposition. It checks
     * that SIG_DFL and SIGTERM still reach the existing re-raise path. */
    assert(reraised == 2 && actions == 2 && unmasks == 3);
    puts("PASS: ignored/default/caught signals need no getenv; RPC pairs and default actions retained");
}
