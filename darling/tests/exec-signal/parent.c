/* Public POSIX API stress: send SIGWINCH before, during and after exec. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t expired;
static void deadline(int n) { (void)n; expired = 1; }
static void caught(int n) { (void)n; }

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    int iterations = atoi(argv[2]);
    if (iterations < 1 || iterations > 10000) return 2;
    struct sigaction timer = {0};
    timer.sa_handler = deadline;
    sigemptyset(&timer.sa_mask);
    if (sigaction(SIGALRM, &timer, NULL)) return 2;
    unsigned long sent = 0;
    for (int i = 0; i < iterations; ++i) {
        int ready[2], status;
        if (pipe(ready)) return 2;
        pid_t child = fork();
        if (child < 0) return 2;
        if (child == 0) {
            close(ready[0]);
            struct sigaction sa = {0};
            sa.sa_handler = i % 4 == 1 ? caught : i % 4 == 2 ? SIG_IGN : SIG_DFL;
            sigemptyset(&sa.sa_mask);
            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGUSR1);
            if (i % 4 == 3) sigaddset(&mask, SIGWINCH);
            if (sigaction(SIGWINCH, &sa, NULL) || sigprocmask(SIG_SETMASK, &mask, NULL)) _exit(3);
            if (write(ready[1], "R", 1) != 1) _exit(4);
            close(ready[1]);
            execl(argv[1], argv[1], i % 4 == 3 ? "blocked" : "unblocked", (char *)NULL);
            _exit(5);
        }
        close(ready[1]);
        expired = 0;
        alarm(8);
        char byte;
        ssize_t count;
        do { count = read(ready[0], &byte, 1); } while (count < 0 && errno == EINTR && !expired);
        close(ready[0]);
        for (;;) {
            if (expired || count != 1) {
                kill(child, SIGKILL);
                while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
                fprintf(stderr, "FAIL iteration=%d startup/deadline\n", i);
                return 1;
            }
            pid_t result = waitpid(child, &status, WNOHANG);
            if (result == child) break;
            if (result < 0 && errno == EINTR) continue;
            if (result < 0) return 2;
            if (kill(child, SIGWINCH) == 0) ++sent;
            struct timespec pause = {0, 100000};
            nanosleep(&pause, NULL);
        }
        alarm(0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "FAIL iteration=%d mode=%d wait_status=%d\n", i, i % 4, status);
            return 1;
        }
    }
    printf("PASS execs=%d SIGWINCH_sent=%lu modes=default,caught,ignored,blocked\n", iterations, sent);
    return sent ? 0 : 1;
}
